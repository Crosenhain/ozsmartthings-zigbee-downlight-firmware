# Oz Smart Things DL41 Zigbee downlight firmware

Open-source replacement firmware for Oz Smart Things RGBCW Zigbee downlights for use with Zigbee2MQTT. The lights appear in Zigbee2MQTT as model `DL41-03-10-R-ZB`, Zigbee `TS0505B`, `_TZ3210_klsm24op`, and contain a Tuya ZTU module (Telink TLSR8258) driving a Sunmoon SM2235 LED driver. The firmware installs over the air from the stock Tuya firmware.

**Experimental.** Read [Safety](#safety) first. Development history and hardware test results are in [CHANGELOG.md](CHANGELOG.md).

## Features

- **Basics:** on/off, brightness (no 10 % floor), and colour temperature on the cool/warm LEDs.
- **Colour on the RGB LEDs** via XY, hue/saturation and enhanced hue.
- **Smooth transitions:** colour fades interpolated in perceptual space, and crossfades when switching between colour and white.
- **Scenes** that store and recall colour and colour loops. Zigbee2MQTT colour-temperature scenes are recalled on the white LEDs.
- **Effects:** ZCL colour loop (stopping restores the previous colour), plus identify effects (blink, breathe, okay, …).
- **State after power loss:** the last state is restored.
- **OTA:** updates between custom builds, and conversion from stock Tuya firmware (1.0.10 and later).
- **Keeps each module's own IEEE address.**

**Limitations:**
- Colour and white are separate modes; RGB + white mixing isn't implemented yet.
- LED current is fixed at the stock codes (16 mA RGB, 20 mA white).
- Colour-temperature endpoints are assumed to be 6000 K / 3000 K, not measured.
- Scenes and the colour loop (1.0.09+) haven't been tested on hardware yet.
- Conversion from stock with 1.0.10+ has been proven on a bench module (see CHANGELOG), but not yet end to end over the air on an installed light.

## Getting the firmware

Download from [GitHub Releases](../../releases):

| Purpose | Release asset |
|---|---|
| Convert a stock Tuya light | `dl41_rgbcw_v1.0.N_from_tuya.zigbee` |
| Update a light already on this firmware | `dl41_rgbcw_v1.0.N.zigbee` |
| Zigbee2MQTT converter (required) | `dl41_rgbcw.mjs` (also [`z2m/`](z2m/dl41_rgbcw.mjs)) → `<z2m data>/external_converters/` |
| Wired recovery (bench only) | `dl41_rgbcw_v1.0.N.bin` |

**Never use a from_tuya file older than 1.0.10.** Earlier builds can't complete the hand-off from the stock bootloader.

## Converting a stock light

Convert one light and confirm it works before doing the rest, one at a time.

**Before you start:**
- **Check it's the same hardware.** Zigbee2MQTT should show `TS0505B` / `_TZ3210_klsm24op`, and an OTA check should report installed version `101`. If the manufacturer string or version differs, stop: the light may use a different LED driver chip.
- **Install the converter** (`<z2m data>/external_converters/`, frontend "Converters", not "Extensions") and restart Zigbee2MQTT.
- **Choose an easy-to-reach light** with a good Zigbee link.
- **Leave it installed on mains** and update it over the air only.

**Procedure:**
1. Upload `dl41_rgbcw_v1.0.N_from_tuya.zigbee` to the stock light on its OTA page in the Zigbee2MQTT frontend. It takes about 35–40 min at default speed. Keep power on; an interrupted transfer leaves the stock firmware running.
2. After "Finished update", the light relocates the new image, wipes the old settings and restarts factory-new within about 10 s.
3. Turn on permit join. The light rejoins with its own IEEE address as `DL41-RGBCW`.
4. Press **Interview** on the existing device page. This keeps the friendly name, options and Home Assistant entities. Retry if you see "can not get active endpoints". Only if the stock definition persists, force-remove the device and re-pair.
5. Re-add the light to groups, bindings and scenes; the conversion clears them.

**If it doesn't join within about 5 min:** power-cycle once at the wall and wait another 5 min. If there's still nothing, stop, open an issue, and don't convert more lights. Recovery means taking the module out and reading it over SWS (see [Tools](#tools)).

**Force-remove caveats:** it only deletes Zigbee2MQTT's record. A device that is still running stays on the network with the key, keeps its group memberships, and may rejoin unmanaged. Force-removing before "Finished update" aborts the upload. Don't tick "block".

**Factory reset (custom firmware):**
1. Power on for about 1 s, then off for about 3 s. Do this four times.
2. On the 5th power-on, leave it on.
3. After 2 s the light leaves the network and restarts factory-new: it blinks 3× while searching and 2× when it joins.

On-time must stay under 2 s, because the power-on counter clears after 2 s. Removing the device normally in Zigbee2MQTT while it's online does the same.

## Updating a converted light

- **Manual:** install the release's `dl41_rgbcw.mjs`, restart Zigbee2MQTT, then upload `dl41_rgbcw_v1.0.N.zigbee` on the device's OTA page.
- **Automatic check:** point Zigbee2MQTT at the release index:

  ```yaml
  ota:
    zigbee_ota_override_index_location: https://github.com/<owner>/<repo>/releases/latest/download/dl41_ota_index.json
  ```

  Zigbee2MQTT supports only one override index, and OTA checks fail for all devices if it can't be fetched. This route has been checked against Zigbee2MQTT's code but not yet tested live.

## Safety

- **Never connect a USB adapter, logic analyser or bench supply to any part of a light that is connected to mains.** The LED driver is likely non-isolated. Isolate at the breaker before removing or refitting a module, and let the capacitors discharge.
- Remove every programming wire before a module goes back into a light.
- The TLSR8258 isn't 5 V tolerant: use 3.3 V logic and check the adapter's TX idle voltage before connecting.
- LED current stays capped at the stock codes in firmware until current has been measured.
- Conversion removes the Tuya bootloader and pairing. No stock firmware is distributed, so treat it as one-way.
- No warranty: you flash at your own risk.

## How it works

### Base and patches

The firmware is [nminaylov/zigbee-light-cct](https://github.com/nminaylov/zigbee-light-cct) (Telink Zigbee SDK V3.7.2.0), pinned to commit `f7441cb4`, with this repository's changes:

- **`firmware/src/`** — files copied over upstream `src/`:
  - modified upstream files (marked in their headers);
  - new files: the SM2235 driver, colour maths, the relocation stub, and a Green Power stub.
- **`firmware/apply_patches.py`** — text edits to upstream:
  - calls the relocation stub from `main.c`;
  - sets the OTA image type (`0x41`) and build number;
  - disables Green Power and Touchlink so the image fits the 512K OTA slot;
  - fixes the makefile.

  It fails if any edit doesn't land.
- **OTA identity:** manufacturer `0x0EBA`, image type `0x0241` (so no public OTA index matches), file version `0x10 N 30 01`.

### Conversion from stock

1. The stock firmware accepts an unsigned OTA file with Tuya's identifiers (manufacturer `0x1141`, image type `0xD3A3`).
2. The Tuya bootloader installs it at `0x8000`, copies its RAM code to SRAM, and starts it.
3. **Stage 1** runs first, from RAM. It detects that the Tuya bootloader launched it (memory-mapped `0x8008 == 'KNLT'`), then confirms with SPI reads of physical flash. It copies the image to `0x40000` with per-page verification, clears the boot flags at `0x0008` and `0x8008`, and resets. A power cut before the flags are cleared just repeats the copy.
4. The ROM boots `0x40000`. **Stage 2** runs right after platform init: it erases the settings areas this firmware uses (`0x34000–0x40000`, `0x78000–0x80000`), then erases sector 0 as its completion marker.

After conversion, OTA updates alternate between the `0x0` and `0x40000` slots (512K layout). Tuya's settings and identity at `0xD8000` and above are left untouched, and the IEEE address is read from `0xFF000`.

**Image limits** (checked by the build):
- image < `0x34000` (OTA slot) and < `0x38000` (relocation);
- RAM code ≤ `0x1A00`, since the bootloader copies only that much;
- no `KNLT` at offset `0x8008`, which would make stage 1 misdetect a normal boot.

### Light engine

- **Output:** the SM2235 on SCL = PB4 / SDA = PC3. It sends a frame only when values change, and goes to standby when everything is off. Current codes are capped at 3/3.
- **Timing:** one 10 ms timer drives level, colour-temperature and colour ramps. Brightness uses a gamma table (0–8192 → 10-bit).
- **Colour rendering:** HS or xy → linear RGB → gains 80/60/60 % (from the stock config) → summed RGB capped at 200 % → scaled by level. The white LEDs are off in colour mode.
- **Fades** run on gamma-encoded channel values, so hue sweeps and colour-to-colour fades look even. Switching between colour and white crossfades the five channels actually shown over the transition time.
- **Scenes** store OnOff, Level and a 13-byte Colour Control set with only the active mode's fields filled. On recall, an xy on the black-body line is treated as colour temperature.
- **Colour loop** is a ZCL `ColorLoopSet`, run as an endless hue move. Any other colour command ends it; deactivating fades back to what was lit before.

### Zigbee2MQTT converter

`z2m/dl41_rgbcw.mjs`:
- light with colour temperature 166–333 mireds, XY/HS colour with enhanced hue, power-on behaviour, effects and OTA;
- `colorloop` / `stop_colorloop` send `colorLoopSet`;
- it must go in `external_converters/`.

Startup colour temperature isn't exposed: Zigbee2MQTT's "previous" value (65535) is rejected by zigbee-herdsman's 65279 limit, and the firmware doesn't implement the attribute yet.

## Hardware

| Part | Marking | Notes |
|---|---|---|
| Zigbee module | Tuya ZTU | Telink TLSR8258, 1 MB flash. SWS (pin 4) and RST (pin 18) are free side pads. |
| Mains driver | `CS-10LS56 V1.0` | 10 W; treat as **non-isolated**. |
| LED board | `SK-D82-8L2C2P-CW-8CP-RGB-V1.0` | U1 = **SM2235EGH** 5-channel constant-current driver; pads VIN, LED+, SDA, SCL, GND. |
| LEDs | 2835 | Outer ring R/G/B ("RGB-54V"), inner rings cool/warm ("CW-36V"). |

**SM2235 current:** RGB 4·(n+1) mA and white 5·(n+1) mA for code n. Stock uses 3/3, i.e. 16 mA / 20 mA (SM2235EGH datasheet; ESPHome's comment table is the SM2335's). Not yet measured.

### Stock flash layout (1 MB)

| Address | Contents |
|---|---|
| `0x000000` | Tuya/Telink bootloader, 27 KB (`KNLT` tag, size `0x6A0C`, no CRC) |
| `0x008000` | Tuya app, 341 KB (size `0x535E4`, trailing inverted CRC32), OTA file version 101 |
| `0x05C000` | Erased; Tuya stages OTA downloads around `0x70000` |
| `0x0D8000`… | Scattered used sectors: Zigbee NV and settings |
| `0x0F8000` | Light config string (below) |
| `0x0FB000` | **Tuya device identity and credentials** |
| `0x0FF000` | IEEE/MAC in Telink SDK format |

> A flash dump contains that device's Tuya credentials. Don't publish dumps; `.gitignore` excludes `*.bin`.

### Stock light config (`0xF800A`)

Full string: [dumps/stock_light_config.txt](dumps/stock_light_config.txt).

| Key | Value | Meaning |
|---|---|---|
| `cmod` / `dmod` | `rgbcw` / `7` | 5-channel light on an I²C driver chip |
| `2235ccur` / `2235wcur` | `3` / `3` | SM2235 current codes (16 mA RGB / 20 mA white) |
| `iicr/g/b/c/w` | `0 1 2 3 4` | OUT1–OUT5 = R, G, B, cool, warm (confirmed on real LEDs) |
| `iicscl` / `iicsda` | `4` / `11` | SCL = PB4, SDA = PC3 (continuity-traced). Tuya's pin index appears to run A0, A1, A7, B1, B4, B5, B6, B7, C0, C1, C2, C3, C4, D2, D3, D4, D7. B5/D2/C2 are soldered but unused. |
| `brightmin` / `colormin` | `10` / `10` | Stock 10 % dimming floor |
| `gmkr/g/b`, `gmwr/g/b` | `80/60/60`, `100/70/75` | Colour gain / white-balance factors |
| `onofftime`, `pmemory`, `rstnum` | `1000`, `1`, `5` | 1 s fade, restore state, 5× power-cycle reset |

## Tools

**Read-only (SWS):**
- `tools/tcsw/TLSR825xComFlasher.py` — pvvx's UART-SWire helpers ([TlsrComSwireWriter](https://github.com/pvvx/TlsrComSwireWriter), Unlicense, see `tools/tcsw/LICENSE`).
- `tools/tcsw/dump_flash.py` — robust dumper:
  - manual-reset wait window (`--wait`), CPU halt;
  - chunked reads with retries, resume, partial-read keep (`--partial`), no per-read sleep (`--no-sleep`);
  - FT232R drift workarounds (`--lenient-stop`, `--swsdiv`; not recommended).
- `tools/tcsw/verify_dump.py` — re-reads used sectors and resolves mismatches by repeated reads; `--skip` for CRC-proven ranges.
- `tools/tcsw/quality_check.py` — raw SWire sample-quality check for an adapter.
- `tools/tcsw/diag_migration.py PORT BUILD_BIN [--wait N]` — post-OTA diagnosis:
  - PC samples;
  - boot flags at `0x0` / `0x8000` / `0x40000`;
  - sample pages of `0x40000` against a build;
  - NV/wipe areas.

  Leaves the CPU halted; power-cycle afterwards.
- `tools/run_dumps.ps1 -Port COMx -Name NAME` — dump + verify with the proven settings.

**Writes flash (bench only, module out of the light):**
- `tools/tcsw/write_flash_verified.py PORT ADDR IMAGE [--wait N] [--dry-run] [--self-test ADDR]` — erases the covered sectors, writes and verifies every page, and writes the page holding the boot flag last. Run `--dry-run` first.

**Offline:**
- `tools/analyze_dump.py` — finds Telink images and CRCs, sector map, Tuya config strings.
- `tools/make_ota.py from-dump | from-bin | inspect` — builds or checks Zigbee OTA files (Telink marker, KNLT, size, CRC, no `KNLT` at `0x8008`).
- `tools/make_ota_index.py FILE IEEE` — device-specific Zigbee2MQTT index JSON plus `check` payload, for MQTT `ota_update/check`.
- `tools/make_ota_payload.py` — Zigbee2MQTT `ota_update/update` payloads.

### SWS wiring (bench only, module out of the light)

```
USB-UART TX ──[ 1 kΩ ]──┬── ZTU SWS  (pin 4)
USB-UART RX ────────────┘
ZTU RST (pin 18) ── momentary to GND by hand
USB-UART 3V3 ────────────── ZTU VCC  (pin 14)   3.3 V logic only
USB-UART GND ────────────── ZTU GND  (pin 13)
```

**Adapter:** use a **CP2102**, or a CH340 with genuine 3.3 V logic. **Not an FTDI FT232R:** its timing drift produced dumps that matched each other but were wrong.

**Dumping a stock module:** stock firmware disables SWS after boot, so reset by hand while the tool streams "halt CPU".

```powershell
.\tools\run_dumps.ps1 -Port COMx -Name unit1 -SkipVerify "0x8000-0x5C000"
```

- Tap RST to GND for about 1 s when prompted. Once caught, the CPU stays halted until power-cycled.
- Only skip the app range after `analyze_dump.py` confirms its CRC.
- A full 1 MB dump takes about 95 min. Custom firmware leaves SWS enabled, so no reset is needed.

## Building and releasing

**Build** on Linux x86-64 or WSL. It needs `gcc`, `make`, `python3`, `curl`, `git` and `bzip2`.

```bash
bash ci/build.sh "$(cat firmware/VERSION)" out
```

`ci/build.sh` does the following:
1. **Runs the host tests:**
   - `test_sm2235.c` — frame waveforms;
   - `test_color_math.c` — HS/XY conversions and round trips, gamma encode/decode, perceptual blends, black-body detection, hue directions.
2. **Fetches the pinned inputs:**
   - TC32 GCC v2.0 (SHA-256 `33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430`, from pvvx/ZigbeeTLc at a pinned commit, vendor URL as fallback);
   - zigbee-light-cct at `f7441cb4be0f7294e2e175a03159239ef7827f9f`.
3. **Patches and builds** `HW_VARIANT_DL41`.
4. **Checks the image:** marker, KNLT, size, CRC, RAM-code size, embedded version, no 64-bit libgcc helpers.
5. **Writes** `out/dl41_rgbcw_v1.0.<N>.bin` and `.elf`.

**Options:** set `TC32_DIR` / `UPSTREAM_DIR` to reuse local copies, and `REFERENCE_BIN` to assert a byte-identical build.

**CI:** `.github/workflows/firmware.yml` builds every push and pull request, using `firmware/VERSION` as the build number, and uploads the image as an artifact.

**Release:**
1. Bump `firmware/VERSION` to N and commit on `main`.
2. `git tag -a v1.0.N -m "<changes>"`, then push the tag.
3. `.github/workflows/release.yml` checks the tag against `firmware/VERSION`, `main` and the highest existing tag.
4. It builds and packages the release with `ci/package_release.py`, then publishes a **prerelease**. Assets:
   - both `.zigbee` files, the `.bin` and the converter;
   - `dl41_ota_index.json` and `dl41_ota_index_from_tuya.json` (the latter restricted to `TS0505B` / `_TZ3210_klsm24op` at version 101);
   - `LICENSE`, `NOTICE` and `SHA256SUMS`.
5. Test on one light, then promote the release with `gh release edit v1.0.N --prerelease=false --latest`.

Release notes come from `ci/release_notes.md.tmpl` plus the tag message.

## Roadmap

- Test scenes and the colour loop on hardware.
- Convert a stock light end to end over the air with 1.0.10+.
- Check the low end for flicker; tune the minimum level and gamma.
- RGB + white mixing, with a power cap checked against the LED driver's temperature.
- Measure LED current and real colour-temperature endpoints (bench supply, off mains), then calibrate gains and white points.
- StartUpColorTemperatureMireds attribute.

## License

Apache License 2.0; see [LICENSE](LICENSE). This project builds on zigbee-light-cct and the Telink Zigbee SDK, both Apache-2.0. See [NOTICE](NOTICE) for attributions. `tools/tcsw/TLSR825xComFlasher.py` is pvvx's and is released under the Unlicense.
