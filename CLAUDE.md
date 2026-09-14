# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Custom Zigbee firmware for Oz Smart Things DL41 RGBCW downlights (Tuya ZTU module = Telink TLSR8258, Sunmoon SM2235 LED driver) for Zigbee2MQTT. User-facing procedures are in `README.md`; per-version design decisions and hardware test results are in `CHANGELOG.md` (add an entry for every firmware change).

## Commands

The firmware builds only on x86-64 Linux (or WSL). On this Windows machine, run bash through `wsl.exe -d Ubuntu-24.04`; the repo is at `/mnt/c/Users/.../ozsmartthings-zigbee-downlight-hacking` (path contains spaces, so quote it).

```bash
# Full build: host tests, fetch pinned toolchain + upstream, patch, build, image checks
bash ci/build.sh "$(cat firmware/VERSION)" out          # -> out/dl41_rgbcw_v1.0.<N>.bin/.elf
# Faster with local copies (toolchain path must not contain spaces; the makefile doesn't quote it)
TC32_DIR=~/dl41-build/toolchain/tc32 UPSTREAM_DIR=~/dl41-build/zigbee-light-cct bash ci/build.sh 11 /tmp/out
# Prove a build is byte-identical to a known image
REFERENCE_BIN=path/to/known.bin bash ci/build.sh 10 /tmp/out

# Host tests (plain gcc, no SDK). Each is one program printing ok/FAIL lines; exits non-zero on failure.
cd firmware/test
gcc -std=gnu99 -Wall -I../src test_color_math.c ../src/color_math.c -o /tmp/tcm && /tmp/tcm
gcc -std=gnu99 -Wall -Istub -I../src test_sm2235.c ../src/sm2235.c -o /tmp/tsm && /tmp/tsm

# Packaging
python tools/make_ota.py inspect FILE.zigbee
python3 ci/package_release.py --bin out/dl41_rgbcw_v1.0.11.bin --build 11 --repo OWNER/REPO --out /tmp/rel
```

There is no linter. Release: bump `firmware/VERSION`, commit on `main`, push an annotated tag `v1.0.N`; `release.yml` refuses tags that don't match `firmware/VERSION`, aren't on `main`, or aren't the highest `v1.0.*` tag.

## Architecture

### This repo is an overlay, not a full source tree
The build takes [nminaylov/zigbee-light-cct](https://github.com/nminaylov/zigbee-light-cct) at the pinned commit `f7441cb4` (it bundles Telink Zigbee SDK V3.7.2.0). Then:
- **`firmware/src/*`** is copied over upstream `src/`. A same-named file replaces the upstream one; every `.c` here is added to `project.mk` automatically.
- **`firmware/apply_patches.py <tree> <N>`** makes text edits to upstream files: `main.c` hooks, APP_BUILD/image type in `version_cfg.h`, Green Power and Touchlink disabled, makefile fixes. At the end it asserts marker strings; **when adding or changing a patch, update its `expected` list**, or a changed upstream will silently skip the edit.
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
  - Colour fades are done in gamma-encoded space: HS is decoded per channel, timed XY fades use `color_blend`.
  - `light_mode_crossfade_start()` blends the last *shown* channels when switching between colour and white.
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
- **`toZigbee` order:** converters listed in the definition's own `toZigbee` win over the `m.light()` extend's. This is how `effect: colorloop` is overridden to send `colorLoopSet`.
- **Location:** the file must live in Z2M's `external_converters/`.
- **No startup colour temperature:** Z2M's "previous" value (65535) exceeds herdsman's 65279 limit, so the option isn't exposed.

## Hardware and tools

- **No hardware in the loop.** Firmware behaviour is verified by the user: an OTA upload through the Zigbee2MQTT frontend (about 35–40 min), then a report back. Don't claim hardware results that weren't reported.
- **Safety:** the light's driver is likely non-isolated. SWS/UART tools are only for a module removed from the light (or a light fully off mains); never suggest wiring a mains-connected light.
- **`tools/tcsw/*.py`** import each other by module name, so run them from `tools/tcsw/`.
  - Proven link: CP2102 at 921600 baud, `--chunk 0x40 --no-sleep --partial`. FTDI FT232R adapters return silently corrupted reads.
  - Stock firmware disables SWS, so reading a stock module needs a manual RST→GND reset window (`--wait`). Custom firmware answers without a reset.
  - `diag_migration.py` is read-only (it halts the CPU). `write_flash_verified.py` writes flash: use `--dry-run` first and get the user's go-ahead.
- **Flash dumps contain per-device Tuya credentials** (at `0xFB000`). `*.bin`, `dumps/ota/`, `dumps/logs/`, `docs/` and `firmware/build/` are gitignored and must stay out of commits and releases. So must device identifiers (IEEE addresses, module serials), and any OTA file built from a stock dump (Tuya's proprietary app).
