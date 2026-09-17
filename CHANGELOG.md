# Changelog

Firmware versions are `1.0.N`; the Zigbee OTA file version is `0x10 N 30 01`. Newest first.
Hardware results come from lights running each build: first on a bench module, then installed on mains and updated over the air.

## [1.0.15] — 2026-09-18

**Fixed: the light stays dark when it is turned on with a brightness** (Home Assistant `light.toggle` or `light.turn_on` with `brightness` / `brightness_pct`; turning off, and turning on without a brightness, worked).

- **Cause.**
  - Zigbee2MQTT sends a turn-on that carries a brightness or a transition as a single `MoveToLevelWithOnOff`, with no `On`.
  - `light_level_ramp_to_level()` only set OnOff and enabled the output when CurrentLevel was going to *rise* (`level_step_256 > 0`, inherited from the Telink sample light).
  - `Off` keeps CurrentLevel, so the requested brightness is usually the level the light already has: the step is 0, OnOff stays 0 and the LEDs stay dark, while Zigbee2MQTT (optimistic) shows ON.
  - Home Assistant's toggle then sees "on" and sends `Off`, so every other toggle appeared to do nothing. A fixed brightness works once (the level rises) and never again.
- **Fix (firmware):**
  - "With on/off" commands turn the light on whenever they end above the minimum level.
  - When the light was off, the ramp starts from the minimum level (or from the brightness still lit during a fade-out), so the light fades up to the target instead of popping on at the old level. `MoveWithOnOff` upwards does the same.
  - Only a *downward* ramp turns the light off at the minimum level. After an off with a transition, CurrentLevel is 1, and a slow turn-on ramp that was still at 1 on its first tick switched itself off again.
  - The per-tick remainder of a slow ramp is applied in the ramp's direction. With an integer step of 0 it went downwards: 200 → 201 over 30 s ended at 199.
  - A level ramp that takes over the 300 ms off fade gates the output off. Recalling a scene that is off sends `Off`, then a level, which left the LEDs lit with OnOff = 0.
- **Fix (converter), for lights still on 1.0.14 or earlier:** after a turn-on that carries a brightness or a transition, `dl41_rgbcw.mjs` also sends `On`. Installing the converter and restarting Zigbee2MQTT fixes those lights without an update. Lights reporting build 15 or later don't get the extra command; groups always do.
- **Tests:**
  - New `firmware/test/test_light_control.c` runs the real `light_control.c` against a simulated timer and checks OnOff, CurrentLevel and the LED output after each command sequence. The 1.0.14 code fails 26 of its checks, including the reported one.
  - The converter was checked against zigbee-herdsman-converters 26.110.0 with a mock endpoint: turn-ons with a brightness or a transition send `MoveToLevelWithOnOff` then `On` for builds below 15; `Off`, `Toggle`, a plain `On` and brightness 0 are unchanged.
- **Hardware:**
  - The converter fix is confirmed on converted lights running 1.0.14: Home Assistant's `light.toggle` with a brightness now turns them on.
  - The 1.0.15 firmware itself hasn't been tested on a light yet.

## [1.0.14] — 2026-09-16

- **Firmware:** version bump only, functionally identical to 1.0.13.
- **Fixed (converter): the light's state never came back to Zigbee2MQTT.** `m.light()` defaults to `configureReporting: false`, so Configure bound nothing and set up no reporting, leaving the Bind and Reporting tabs empty. With Zigbee2MQTT's "optimistic" option off, the published state then never changed, and Home Assistant's `light.toggle` appeared to do nothing.
  - The converter now sets `configureReporting: true`. Press **Configure** on the device after installing it.
  - Verified against zigbee-herdsman-converters 26.39.1 (the version in Zigbee2MQTT 2.9.2-dev): Configure binds genOnOff, genLevelCtrl and lightingColorCtrl to the coordinator and reports onOff (immediately), currentLevel (5 s) and the colour attributes (10 s).
  - The firmware already marks those attributes reportable and reports them at least every 120 s to bound destinations.
  - Confirmed working on a converted light: with "optimistic" off, the state now follows the light itself.

## [1.0.13] — 2026-09-16

**Fixed: the light sometimes stays dark after turning on** (state ON in Zigbee2MQTT, no light; seen on a light running 1.0.10).

- **Likely cause.** The SM2235 bus has no acknowledgement, and the driver sent a frame only when channel values changed, never re-sending one. If the turn-on frame was lost or corrupted, or swallowed while the chip woke from standby, the firmware believed it was delivered. The LEDs stayed dark until the values next changed. Not yet confirmed on hardware.
- **Fix:**
  - Every on/off command transmits, even if the driver thinks nothing changed (`sm2235_invalidate()`).
  - 50 ms after the output stops changing, the final state is re-sent once. While lit it is re-sent every 2 s (`sm2235_refresh()`), which also recovers from an LED driver reset. When off, clear + standby is re-sent once.
  - Interrupts are masked for each byte on the bus (~0.1 ms), so a radio interrupt can't stretch a clock pulse mid-byte.
- **Tests:** `test_sm2235.c` covers the send result, forced re-send, refresh when lit and when off, and interrupts masked around every data/ACK clock.

**Added: update notifications for converted and stock lights.**
- **`z2m/dl41_stock_ota.mjs`:** Zigbee2MQTT's built-in stock definition (`_TZ3210_klsm24op` only) plus `ota: true`, so Zigbee2MQTT checks stock lights for updates.
- **`dl41_ota_index_all.json`** is a new release asset with both index entries (Zigbee2MQTT takes one override index). Both converters are now release assets too.
- **Verified** with zigbee-herdsman-converters 26.110.0 / zigbee-herdsman 10.9.3:
  - the external stock definition wins only for `_TZ3210_klsm24op`;
  - `findMatchingOtaImage` offers each light only its own image, and rejects other manufacturer names, versions and models;
  - a GitHub release download via Node `fetch` matches the index SHA-512.

**Fixed:** `ci/package_release.py` writes `SHA256SUMS`, the indexes and the notes with LF line endings, so checksums verify when packaged on Windows.

**Added: `examples/home-assistant/dl41_ota_auto_resume.yaml`**, an example automation that re-sends a Zigbee2MQTT OTA update after a "did not start/finish firmware download" failure.
- Stock-to-custom conversions stall on weak links; one observed at 70 %, `linkquality` 53, with Zigbee2MQTT timing out after 150 s waiting for the next block request.
- The attempt count is carried in the request's `transaction` field, up to 10 attempts.
- Templates checked against real failure responses with Jinja; not tested in a live Home Assistant.

## [1.0.12] — 2026-09-15

- **Firmware:** version bump only, functionally identical to 1.0.11. Updating lights is optional.
- **Added (converter):** `color_temp_kelvin` sets and reports colour temperature in Kelvin (3000–6000 K, step 50), mapped to mireds for the device.
  - `color_temp` (mireds), `color_temp_percent` and the presets still work, and every change also publishes `color_temp_kelvin`.
  - Out-of-range values clamp to the white LEDs' 166–333 mireds.
  - Verified against zigbee-herdsman-converters 26.109.0 (exposes, set path, state, device reports); not yet run in a live Zigbee2MQTT.

## [1.0.11] — 2026-09-15

- **Fixed:** `softwareBuildID` printed the build number's hex nibbles, so builds 10+ read `v1.0.0:`. It is now decimal (`v1.0.11`).
- **Added:** CI and release pipeline.
  - `ci/build.sh` fetches the pinned toolchain and upstream, then builds, checks and tests. A clean CI build of 1.0.10 is byte-identical to the hand-built image.
  - `.github/workflows/firmware.yml` builds pushes and pull requests.
  - `.github/workflows/release.yml` and `ci/package_release.py` publish tagged releases.
- **Changed:** `apply_patches.py` fails if any upstream edit doesn't land, and takes the build number as a required argument.
- **Fixed:** `tools/make_ota.py` works on Python < 3.12, and `from-bin` exits non-zero if the result fails inspection.
- **Changed:** Apache-2.0 `LICENSE` and `NOTICE` added; files modified from upstream carry a notice.

## [1.0.10] — 2026-09-14

**Fixed: OTA from stock firmware hangs after the update (1.0.02–1.0.09).**

**What happened.** A stock light updated with the 1.0.09 from_tuya file never came back. Its module was read over SWS with `tools/tcsw/diag_migration.py`:

- **SWS answered without a reset.** Custom code was running; stock firmware disables SWS.
- **PC stuck at `0x00d7b2`**, 2 bytes into `drv_platform_init` (linked at `0xd7b0` in 1.0.09).
- **Flash:**
  - `0x0` Tuya bootloader, flag KNLT.
  - `0x8000` the 1.0.09 image, flag KNLT, CRC ok.
  - `0x70000` Tuya's staging copy of 1.0.09.
  - `0x40000` still old stock bytes: stage 1 never copied.
- **SRAM `0x840000`** held our RAM code, header included, byte-identical to the build. Remap register `0x63e` = 0.

**Cause.** The Tuya bootloader starts the app by copying its RAM code to SRAM and soft-resetting into it. This matches the SRAM contents above, and it is what the Telink SDK bootloader does. In that mode CPU address `0x0` shows our own RAM-code header (`58 80 01 30`), not the bootloader.
- 1.0.02+ stage 1 required `*(u32*)0x0 == 0x00008041` (the bootloader's first word), so it returned.
- `main` then called flash code linked for `0x0` while the image sat at `0x8000`, and hung.
- 1.0.01 had checked with SPI reads of physical flash, which is why its stage 1 worked.

**Fix.**
- Stage 1 first checks only the memory-mapped `0x8008 == 'KNLT'`, the same check as romasku's `is_bootloader_mode()`.
- It then confirms the bootloader word and flag with SPI reads of physical flash. That's safe in this context: the bootloader leaves the system timer running, as 1.0.01 showed.
- A failed confirmation resets instead of returning.
- Stage 1's call graph is RAM code only (< `0x1900`, including `sleep_us`).
- `tools/make_ota.py` refuses images with `KNLT` at offset `0x8008`, so a normal boot can't be misdetected.

**Hardware result (bench rehearsal).** 1.0.10 was wired-written to `0x8000` of the failed module, which reproduces the exact state after Tuya installs an update. After a reset the module joined Zigbee2MQTT. SWS check:
- PC samples vary: the firmware is running.
- Sector 0 is erased (stage 2's completion marker; the Tuya bootloader is gone, as intended).
- The `0x8008` flag is cleared.
- `0x40000` matches the build.
- `0x35000`, `0x78000` and `0x79000` are erased; `0x34000` and `0x7A000` hold fresh NV written after the wipe.
- `0x70000` still holds the old staging copy (unused, harmless).

This proves the bootloader-launched stage 1 → stage 2 → join chain. The OTA download step was proven separately (1.0.01, 1.0.09).

**Do not use any from_tuya file older than 1.0.10.**

## [1.0.09] — 2026-09-14

**Added: scenes with colour, colour loop, effects.** Not yet tested on hardware.

- **Scenes** (`firmware/src/zcl_scene.c`):
  - **Store** writes OnOff + Level + the 13-byte Colour Control set. This fits the 24-byte `ZCL_MAX_SCENE_EXT_FIELD_SIZE`, so the NV scene table layout is unchanged.
    - Only the active mode's fields are filled: XY → x/y; HS → enhanced hue, saturation and loop fields; CT → mireds.
    - Hue/sat of exactly 0/0 is stored as the lit colour's xy, so it can't read back as "no colour".
  - **Recall** treats every field as optional, clamps to what the stack kept, and uses ZCL7 EnhancedColorMode when present. Otherwise it infers the mode: x/y → XY; hue/sat/loop → HS; mireds → CT.
  - **Z2M colour-temperature scenes.** `scene_add` stores colour temperature as xy. An xy within 0.01 of the black-body line (`color_xy_to_blackbody_mireds`: McCamy + Kim spline, host-tested against Z2M's kelvinToXy table) is recalled as colour temperature on the white LEDs.
  - Recall uses the scene's transition time. A new `scene_store` scene has transition 0.
- **Colour loop** (`ColorLoopSet`, capability bit 2, attributes `0x4002`–`0x4006`, not persisted):
  - An endless hue move at 65536 / ColorLoopTime units per second.
  - Starting from white uses full saturation and a 1 s crossfade.
  - Any other colour or CT command ends the loop where it stands. Stop Move Step leaves it running (ZCL).
  - Deactivating fades (1 s) back to what was lit before: stored hue, previous XY or previous colour temperature.
  - NV saves are debounced 200 ms after the last change, so a running loop doesn't write flash.
- **Converter:**
  - `effect: true` (identify blink/breathe/okay/…).
  - `colorloop` / `stop_colorloop` send `colorLoopSet` instead of Z2M's endless `moveHue`, so stopping restores the previous colour. `transition` = seconds per turn (default 15).
- **Test plan for hardware:**
  - From white, `{"effect": "colorloop", "transition": 10}` then `{"effect": "stop_colorloop"}`.
  - Start the loop, then set a colour: the loop should stop.
  - `scene_store` / `scene_recall` a colour and a white.
  - `{"scene_add": {"ID": 3, "state": "ON", "brightness": 200, "color_temp": 166, "transition": 2}}` then recall.
  - `scene_store` during a loop, then recall it.
- **Known issue:** its from_tuya file hangs (fixed in 1.0.10).

## [1.0.08] — 2026-09-14

**Fixed: colour → colour fades looked like snaps, worst between blues and reds.**

- **Cause.** The ramp maths was correct, but rendering was linear light: HS channels followed HSV linearly and gamma applied only to the level.
  - Blue → red showed ~24 % red perceptually at 5 % of the fade, and the last 24 % of blue vanished in the final 5 %.
  - Blue carries little luminance (and runs at gain 60 vs red 80), so the change reads as a jump at each end.
  - The linear-xy path lagged too.
- **Fix:** fade in gamma-encoded space, as ESPHome, Tasmota and WLED do.
  - `color_encode` / `color_decode`: gamma 2.0 (matches the level curve), integer isqrt.
  - **HS:** `color_hs_to_linear` decodes the HSV result, so hue sweeps change evenly. Mixed hues render like a colour picker (orange 30° = 1, 0.25, 0); primaries and secondaries are unchanged.
  - **XY:** timed fades render `color_blend(from, to, α)`, which interpolates encoded channels and renormalises the brightest one. The x/y attributes still ramp for reporting.
  - HS ↔ XY conversion uses the colour actually lit (`light_color_shown_rgb`), including mid-fade.
- **Hardware result:** colour ↔ colour (including blue ↔ red), in-between shades and white ↔ colour all fade smoothly.

## [1.0.07] — 2026-09-14

- **Fixed: colour ↔ white switches jumped.** `light_mode_crossfade_start()` snapshots the five channels actually shown and blends to the new mode's output over the command's transition time.
- **Fixed: occasional snaps between colours.** On HS ↔ XY switches the displayed colour is now converted first (`color_rgb_to_xy`, `color_rgb_to_hs`). Before this, the conversion was a stub, so ramps started from stale coordinates.
- **Changed:** colour ramps with transition 0 land on the next 10 ms tick.
- **Hardware result:** white ↔ red fades well, colour survives a power cycle, and some blue ↔ red fades still snapped (→ 1.0.08).

## [1.0.06] — 2026-09-14 (M2: colour)

- **Added:** hue/saturation, enhanced hue and XY on the RGB LEDs.
  - Commands: move-to, move and step for each, enhanced variants, and stop.
  - Attributes are reportable and saved in NV.
  - Colour and colour temperature are separate modes, like stock.
- **Engine:** a hue/sat/XY ramp on the shared 10 ms timer.
  - Rendering converts HS → RGB (integer HSV) and xy → linear sRGB (D65).
  - Gains 80/60/60 % (from stock `gmk*`), summed RGB capped at 200 %, white LEDs off in colour mode.
- **Converter:** adds `color: {modes: ["xy", "hs"], enhancedHue: true}`.
- **Hardware result:** colours and brightness look right, and fades within colour or within white are smooth. Colour ↔ white switches jumped (→ 1.0.07).

## [1.0.05] — 2026-09-14 (M1: white light)

- **Added: SM2235 driver** (`firmware/src/sm2235.[ch]`), bit-banged on SCL = PB4 / SDA = PC3.
  - Mode byte `0xC8`/`0xD0`/`0xD8`; zero frame then `0xC0` for standby; sends only on change.
  - Host waveform test.
- **Added: hardware config v2** (`HW_VARIANT_DL41`): current codes hard-capped at stock 3/3; OUT1–5 = R, G, B, cool, warm; nominal 6000 K / 3000 K.
- **Changed:** the CCT app's PWM output is replaced by the SM2235 (gamma 0–8192 → 10-bit, lowest non-zero level kept at 1).
- **Removed:** Green Power and Touchlink, to fit the 512K OTA slot (`src/gp_stub.c` satisfies the ZDO hook).
- **Added: Z2M converter** `z2m/dl41_rgbcw.mjs`, model `DL41-RGBCW`.
  - It must go in `external_converters/`, not `external_extensions/`.
  - Current zigbee-herdsman-converters use `ota: true`, not `m.ota()`.
  - Z2M's `color_temp_startup` "previous" (65535) is rejected by herdsman's 65279 max, so the converter doesn't expose startup colour temperature.
- **Hardware result:** installed on mains; warm, cool and mixed white work, so the cool/warm channel order is correct.

## [1.0.03] — 2026-09-14

- **Proven: custom → custom OTA** (mfr `0x0EBA`, type `0x0241`). The device swapped slots and rejoined without re-pairing, reporting `softwareBuildID` `v1.0.03`.

## [1.0.02] — 2026-09-14

- **Fixed: 1.0.01 stage 2 hung.** SPI flash helpers call `sleep_us()`, which never returns before `drv_platform_init()` starts the system timer. Stage 2 now runs right after platform init.
- **Changed:** stage 1 detects the Tuya layout with memory-mapped reads only. **This introduced the from-stock bug fixed in 1.0.10.**
- **Hardware result:** written by wire to `0x40000` with `tools/tcsw/write_flash_verified.py` (page-verified, boot-flag page last, 9.8 min). It booted, wiped and joined as `DL41-CUSTOM-TEST`.
  - The first write attempt erased nothing: command and address were sent as one non-FIFO register write, so the address went to the chip-select register. Erases are now sent byte by byte.
  - Z2M kept the cached stock definition for the same IEEE until re-interview or re-pair.

## [1.0.01] — 2026-09-14

**First custom image** (`DL41-CUSTOM-TEST`, bench variant with no pins driven).

- **Base:** nminaylov/zigbee-light-cct (Apache-2.0, Telink SDK V3.7.2.0), built with TC32 GCC v2.0.
- **Relocation stub** (`firmware/src/tuya_migrate.[ch]`):
  - **Stage 1** (RAM code) copies the image from `0x8000` to `0x40000` with per-page verify, clears the `KNLT` flags at `0x0008` and `0x8008`, and resets.
  - **Stage 2** erases `0x34000–0x40000` and `0x78000–0x80000`, then sector 0.
- **Hardware result:** stock accepted the OTA ("updated from 101 to 268513281") and stage 1 copied and verified. Stage 2 hung in `sleep_us` (SWS diagnosis) → 1.0.02.

## Groundwork — 2026-09-13/14

- **Stock flash:** one ZTU module dumped over SWS with a CP2102 and verified (app CRC, re-read of every used sector).
  - An FT232R adapter produced dumps that matched each other but had bit 6 set after any set bit 7. Don't use FTDI for SWS.
- **SM2235 bus traced:** SCL = PB4, SDA = PC3, matching Tuya config `iicscl:4` / `iicsda:11`.
- **OTA from stock proven** with a repack of the stock app (mfr `0x1141`, type `0xD3A3`, header version `0xFFFFFFFF`).
  - Stock accepts self-built, unsigned OTA files and reports OTA file version 101 afterwards.
  - About 1 h at Z2M defaults (50-byte blocks, 250 ms delay).
  - Z2M's `ota_update/check` needs an index JSON, not an image URL.
