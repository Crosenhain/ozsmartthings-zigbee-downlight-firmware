# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Custom Zigbee firmware for Oz Smart Things DL41 RGBCW downlights (Tuya ZTU module = Telink TLSR8258, Sunmoon SM2235 LED driver) for Zigbee2MQTT. User-facing procedures are in `README.md`; per-version design decisions and hardware test results are in `CHANGELOG.md` (add an entry for every firmware change). Docs, comments and identifiers use British spelling (colour, licence).

## Commands

The firmware builds only on x86-64 Linux (tc32 is an x86-64 Linux toolchain); it needs `gcc`, `make`, `python3`, `curl`, `git` and `bzip2`. The first build downloads the pinned toolchain and upstream into `~/.cache/dl41-build` and reuses them afterwards.

```bash
# Full build: host tests, fetch pinned toolchain + upstream, patch, build, image checks
bash ci/build.sh "$(cat firmware/VERSION)" out          # -> out/dl41_rgbcw_v1.0.<N>.bin/.elf
# Reuse local copies instead of the cache (toolchain path must not contain spaces; the makefile doesn't quote it)
TC32_DIR=~/dl41-build/toolchain/tc32 UPSTREAM_DIR=~/dl41-build/zigbee-light-cct bash ci/build.sh "$(cat firmware/VERSION)" /tmp/out
# Prove a build is byte-identical to a known image
REFERENCE_BIN=path/to/known.bin bash ci/build.sh 10 /tmp/out

# Host tests (plain gcc, no SDK). Each is one program printing ok/FAIL lines; exits non-zero on failure.
cd firmware/test
gcc -std=gnu99 -Wall -I../src test_color_math.c ../src/color_math.c -o /tmp/tcm && /tmp/tcm
gcc -std=gnu99 -Wall -Istub -I../src test_sm2235.c ../src/sm2235.c -o /tmp/tsm && /tmp/tsm
gcc -std=gnu99 -Wall -Istub -I../src test_light_control.c ../src/light_control.c ../src/color_math.c -lm -o /tmp/tlc && /tmp/tlc

# Packaging
python tools/make_ota.py inspect FILE.zigbee
python3 ci/package_release.py --bin out/dl41_rgbcw_v1.0.14.bin --build 14 --repo OWNER/REPO --out /tmp/rel   # --out must be empty
```

There is no linter. `firmware.yml` builds every push and pull request at `firmware/VERSION` and uploads the image as an artifact.

Release: bump `firmware/VERSION`, commit on `main`, push an **annotated** tag `v1.0.N` (N ≥ 10, because releases ship a from_tuya image; the tag message becomes the release notes via `ci/release_notes.md.tmpl`). `release.yml` refuses tags that don't match `firmware/VERSION`, aren't on `main`, or aren't the highest `v1.0.*` tag. It publishes a **prerelease**; after the build has been tested on a light, promote it with `gh release edit v1.0.N --prerelease=false --latest` — `releases/latest` index URLs don't resolve until then.

## Architecture

### This repo is an overlay, not a full source tree
The build takes [nminaylov/zigbee-light-cct](https://github.com/nminaylov/zigbee-light-cct) at the pinned commit `f7441cb4` (it bundles Telink Zigbee SDK V3.7.2.0). Then:
- **`firmware/src/*`** is copied over upstream `src/`. A same-named file replaces the upstream one; every `.c` here is added to `project.mk` automatically.
- **`firmware/apply_patches.py <tree> <N>`** makes text edits to upstream files: `main.c` hooks, APP_BUILD/image type in `version_cfg.h`, Green Power and Touchlink disabled, makefile fixes. At the end it asserts marker strings; **when adding or changing a patch, update its `expected` list**, or a changed upstream will silently skip the edit.
  - It copies `firmware/src` first and patches afterwards, so a patch whose target is also one of our files edits **our** copy: `zb_ep_cfg.c`'s Touchlink guard is applied to it every build, and `hw_variants.h`'s DL41_TEST block is already present so that edit is a no-op. Editing those files can break a patch or its check.
- **Build number and versions:** N is the one-byte `APP_BUILD`. The firmware is 1.0.N, the OTA file version is `0x10 N 30 01`, and `softwareBuildID` is `v1.0.NN` in decimal.
- **Licence headers:** nine files in `firmware/src` are modified upstream copies, marked by a "Modified from nminaylov/zigbee-light-cct" header. Keep that header, and add it to any other upstream-derived file (Apache-2.0; see `NOTICE`).

### Hard image constraints (checked by `ci/build.sh` / `tools/make_ota.py`)
- **Size:** image < `0x34000` (512K OTA slot).
- **RAM code** ≤ `0x1A00`.
- **No 64-bit libgcc helpers** (`__divdi3`, `__muldi3`, …): they don't link, so keep integer maths in 32 bits. Soft-float is fine.
- **No `KNLT` bytes at image offset `0x8008`.**

### Conversion from stock (`firmware/src/tuya_migrate.c`), the fragile part
- **Install:** the Tuya bootloader installs an OTA image at `0x8000`, copies its RAM code to SRAM and soft-resets into it. The image is linked for `0x0`, so only RAM code can run at that point, and CPU address `0x0` shows our own RAM code, not the bootloader.
- **`tuya_migrate_stage1()`** runs first in `main`.
  - Its detection is a memory-mapped `0x8008 == 'KNLT'`; only then may it use SPI flash reads, which call `sleep_us` and hang unless the bootloader already started the system timer.
  - It copies the image to `0x40000`, clears the boot flags and resets.
  - **It and everything it calls must stay `_attribute_ram_code_`.** Verify with `tc32-elf-objdump` that every callee address is < `0x1900`.
- **`tuya_migrate_stage2()`** runs after `drv_platform_init()`: it wipes this firmware's NV areas and erases sector 0 as the completion marker.
- **History:** a bad detection check in 1.0.02–1.0.09 bricked a stock conversion; see CHANGELOG 1.0.10 before touching this.

### Light engine
- **`light_control.c`:** one shared 10 ms transition timer drives the level, colour-temperature and colour (hue/sat/XY in Q8 fixed point) ramps.
  - Each tick renders five channels (R, G, B, cold, warm) through `light_output_push()` to `sm2235_set()`, which sends a frame only on change.
  - **The SM2235 bus is write-only, with no ACK.** `light_output_push()` re-sends the final state 50 ms after changes stop and every 2 s while lit (`sm2235_refresh()`). On/off commands call `sm2235_invalidate()` so they always transmit. Keep that if you restructure output.
  - Colour fades are done in gamma-encoded space: HS is decoded per channel, timed XY fades use `color_blend`.
  - `light_mode_crossfade_start()` blends the last *shown* channels when switching between colour and white.
  - **On/off vs level:** the OnOff attribute and the output gate (`out_enabled`) change only through `light_on_off_update()` (upstream `zcl_onoff.c`). `Off` fades `eff_level_256` and keeps CurrentLevel. Zigbee2MQTT sends every turn-on with a brightness or transition as a lone `MoveToLevelWithOnOff`, usually to the level the light already has, so "with on/off" ramps must turn on whenever they end above the minimum level (not only when the level rises), and only a downward ramp may turn off at the minimum level (`level_dir`). CHANGELOG 1.0.15.
  - **`firmware/test/test_light_control.c`** runs the real file against a simulated timer wheel (`stub/tl_common.h`), with the upstream callers reduced to a few lines in the test. Add a case there for any change to the on/off, level or fade logic; to show a case catches a bug, compile it against `git show HEAD:firmware/src/light_control.c`.
- **`zcl_color.c`:** ZCL command handlers.
  - `light_set_color_mode()` is the single place mode switches happen. Colour ↔ CT starts a crossfade and returns transition 0 for the new ramp. HS ↔ XY converts from `light_color_shown_rgb()`, so a switch mid-fade doesn't jump.
  - The colour loop (`ColorLoopSet`) is an endless hue ramp. Any other colour/CT command halts it first, except Stop Move Step.
- **`color_math.c`:** SDK-free, so it's host-testable. Put new colour maths here with tests in `firmware/test/test_color_math.c`.
- **`zcl_scene.c`:** the scene extension fields must fit `ZCL_MAX_SCENE_EXT_FIELD_SIZE` = 24 bytes (upstream `stack_cfg.h`): OnOff 4 + Level 4 + Colour 16. Growing it changes the NV scene table layout.
- **NV:**
  - Colour state is the `ZclNvColorCtrl` record (`app.h`, saved in `zb_ep_cfg.c`; the save compares fields and rewrites on read failure).
  - Saves are debounced 200 ms after the last attribute change, so a running ramp or loop doesn't write flash.
  - Changing a record's size means old records fail to read, so the save must handle that.
- **`hw_config` / `hw_variants.h`:** pins (SCL PB4, SDA PC3), channel map, CCT range. SM2235 current codes are capped by `HW_CUR_CODE_LIMIT_*` at the stock 3/3; don't raise them (not measured).

### OTA identities
- **Custom builds:** manufacturer `0x0EBA`, image type `0x0241`.
- **Stock conversion files** (`_from_tuya`): Tuya's `0x1141` / `0xD3A3`, valid only for builds ≥ 1.0.10. That pair would match other Tuya devices, so any index entry for it is filtered to `TS0505B` / `_TZ3210_klsm24op` / version 101 (`ci/package_release.py`).
- **Versions:** Zigbee2MQTT offers only strictly higher file versions.

### Zigbee2MQTT converter (`z2m/dl41_rgbcw.mjs`)
- **`toZigbee` order:** converters listed in the definition's own `toZigbee` win over the `m.light()` extend's. This is how `effect: colorloop` is overridden to send `colorLoopSet`, and how `color_temp` / `color_temp_percent` / `color_temp_kelvin` go through one Kelvin-aware wrapper around `tz.light_colortemp`.
- **Testing the converter:** there is no test in the repo. Check changes by `npm install zigbee-herdsman-converters` in a temp folder, copying the `.mjs` there, and calling `prepareDefinition()`, then a converter's `convertSet` with a mock endpoint.
- **`onOffBrightnessWithExplicitOn`** replaces `tz.light_onoff_brightness` (same keys) and sends `On` after a turn-on with a brightness or transition, for lights whose `softwareBuildID` is below 1.0.15 (and for groups). It can go once no light runs 1.0.14 or earlier.
- **Location:** the file must live in Z2M's `external_converters/`.
- **`configureReporting: true` is required** on `m.light()`: it defaults to false, so without it Configure binds nothing, the device never reports state, and Z2M can only publish assumed state (breaks with the device's "optimistic" option off).
- **`z2m/dl41_stock_ota.mjs`** copies Z2M's built-in stock definition (`_TZ3210_klsm24op` only) and adds `ota: true`. Z2M only checks devices with `definition.ota`, and external definitions take precedence over built-ins. Keep it in sync with upstream `src/devices/ozsmartthings.ts` if that changes.
- **Update index:** `ci/package_release.py` writes `dl41_ota_index.json` (custom), `dl41_ota_index_from_tuya.json` (stock) and `dl41_ota_index_all.json` (both); releases point at the combined one because Z2M accepts only one override index. Matching is zigbee-herdsman's `Device.findMatchingOtaImage`: first entry whose imageType/manufacturerCode/min-max version/modelId/manufacturerName all match.
- **No startup colour temperature:** Z2M's "previous" value (65535) exceeds herdsman's 65279 limit, so the option isn't exposed.

## Hardware and tools

- **No hardware in the loop.** Firmware behaviour is verified by the user: an OTA upload through the Zigbee2MQTT frontend (about 35–40 min), then a report back. Don't claim hardware results that weren't reported.
- **Safety:** the light's driver is likely non-isolated. SWS/UART tools are only for a module removed from the light (or a light fully off mains); never suggest wiring a mains-connected light.
- **`tools/tcsw/*.py`** import each other by module name, so run them from `tools/tcsw/`. `tools/run_dumps.ps1` is a PowerShell wrapper (Windows only); elsewhere run `dump_flash.py` then `verify_dump.py` directly.
  - Proven link: CP2102 at 921600 baud, `--chunk 0x40 --no-sleep --partial`. FTDI FT232R adapters return silently corrupted reads.
  - Stock firmware disables SWS, so reading a stock module needs a manual RST→GND reset window (`--wait`). Custom firmware answers without a reset.
  - `diag_migration.py` is read-only (it halts the CPU). `write_flash_verified.py` writes flash: use `--dry-run` first and get the user's go-ahead.
- **Flash dumps contain per-device Tuya credentials** (at `0xFB000`). `*.bin`, `dumps/ota/`, `dumps/logs/`, `docs/` and `firmware/build/` are gitignored and must stay out of commits and releases. So must device identifiers (IEEE addresses, module serials), and any OTA file built from a stock dump (Tuya's proprietary app).
