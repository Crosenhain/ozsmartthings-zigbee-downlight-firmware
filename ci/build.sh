#!/usr/bin/env bash
# Build the DL41 RGBCW firmware from pinned inputs, check the image, and run the host tests.
#
# usage: bash ci/build.sh BUILD_NUMBER [OUT_DIR=out]     (BUILD_NUMBER 0..255: 11 -> firmware 1.0.11)
#
# optional environment:
#   TC32_DIR       extracted toolchain containing bin/tc32-elf-gcc (default: download + SHA-256 verify)
#   UPSTREAM_DIR   git clone of nminaylov/zigbee-light-cct containing UPSTREAM_SHA (default: shallow fetch)
#   CACHE_DIR      download cache (default ~/.cache/dl41-build)
#   JOBS           parallel make jobs (default: nproc)
#   REFERENCE_BIN  if set, the built image must be byte-identical to this file
#
# Local WSL example:
#   TC32_DIR=~/dl41-build/toolchain/tc32 UPSTREAM_DIR=~/dl41-build/zigbee-light-cct bash ci/build.sh 11 /tmp/dl41-out
set -euo pipefail

TC32_SHA256=33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430
TC32_URLS=(
  # Same tarball (same SHA-256) committed in pvvx/ZigbeeTLc, pinned to a commit; the vendor URL is the fallback.
  https://raw.githubusercontent.com/pvvx/ZigbeeTLc/377320b2af0b5aee2abfab2333c5b102170541d2/tools/linux/tc32_gcc_v2.0.tar.bz2
  https://shyboy.oss-cn-shenzhen.aliyuncs.com/readonly/tc32_gcc_v2.0.tar.bz2
)
UPSTREAM_URL=https://github.com/nminaylov/zigbee-light-cct.git
UPSTREAM_SHA=f7441cb4be0f7294e2e175a03159239ef7827f9f

die() { echo "error: $*" >&2; exit 1; }

[[ $# -ge 1 && $# -le 2 ]] || die "usage: $0 BUILD_NUMBER [OUT_DIR]"
[[ $1 =~ ^[0-9]+$ ]] && (( 10#$1 <= 255 )) || die "BUILD_NUMBER must be 0..255 (APP_BUILD is one byte)"
BUILD=$((10#$1))
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "${2:-out}"
OUT=$(cd "${2:-out}" && pwd)
CACHE_DIR=${CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/dl41-build}
mkdir -p "$CACHE_DIR"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# --- host tests (seconds, no toolchain needed) ---
S=$REPO/firmware/src
T=$REPO/firmware/test
gcc -std=gnu99 -Wall -I"$S" -o "$WORK/test_color_math" "$T/test_color_math.c" "$S/color_math.c"
gcc -std=gnu99 -Wall -I"$T/stub" -I"$S" -o "$WORK/test_sm2235" "$T/test_sm2235.c" "$S/sm2235.c"
gcc -std=gnu99 -Wall -I"$T/stub" -I"$S" -o "$WORK/test_light_control" "$T/test_light_control.c" "$S/light_control.c" "$S/color_math.c" -lm
"$WORK/test_color_math" | tail -n 1
"$WORK/test_sm2235" | tail -n 1
"$WORK/test_light_control" | tail -n 1

# --- toolchain (x86-64 Linux; needs only libc and zlib) ---
if [[ -z ${TC32_DIR:-} ]]; then
  tarball=$CACHE_DIR/tc32_gcc_v2.0.tar.bz2
  if ! echo "$TC32_SHA256  $tarball" | sha256sum -c --status 2>/dev/null; then
    for url in "${TC32_URLS[@]}"; do
      echo "fetching $url"
      if curl -fsSL --retry 3 --connect-timeout 30 -o "$tarball.part" "$url" &&
         echo "$TC32_SHA256  $tarball.part" | sha256sum -c --status; then
        mv "$tarball.part" "$tarball"
        break
      fi
      rm -f "$tarball.part"
      echo "  download failed or SHA-256 mismatch" >&2
    done
  fi
  echo "$TC32_SHA256  $tarball" | sha256sum -c --status 2>/dev/null || die "no verified tc32 toolchain"
  tar -xjf "$tarball" -C "$WORK"   # -> $WORK/tc32
  TC32_DIR=$WORK/tc32
fi
CROSS=$TC32_DIR/bin/tc32
[[ -x ${CROSS}-elf-gcc ]] || die "${CROSS}-elf-gcc not found"
[[ $CROSS != *[[:space:]]* ]] || die "toolchain path contains whitespace (the makefile doesn't quote COMPILE_PREFIX)"
"${CROSS}-elf-gcc" --version | sed -n 1p

# --- upstream at the pinned commit ---
if [[ -z ${UPSTREAM_DIR:-} ]]; then
  UPSTREAM_DIR=$CACHE_DIR/zigbee-light-cct
  if ! git -C "$UPSTREAM_DIR" cat-file -e "$UPSTREAM_SHA^{commit}" 2>/dev/null; then
    rm -rf "$UPSTREAM_DIR"
    git init -q "$UPSTREAM_DIR"
    git -C "$UPSTREAM_DIR" fetch -q --depth 1 "$UPSTREAM_URL" "$UPSTREAM_SHA"
  fi
fi
git -C "$UPSTREAM_DIR" cat-file -e "$UPSTREAM_SHA^{commit}" || die "$UPSTREAM_DIR lacks $UPSTREAM_SHA"
mkdir "$WORK/fw"
git -C "$UPSTREAM_DIR" archive "$UPSTREAM_SHA" | tar -x -C "$WORK/fw"

# --- patch (apply_patches.py exits non-zero if any edit didn't land) and build ---
python3 "$REPO/firmware/apply_patches.py" "$WORK/fw" "$BUILD" | tail -n 1
mk=(make -C "$WORK/fw" "COMPILE_PREFIX=$CROSS" HW_VARIANT=HW_VARIANT_DL41)
"${mk[@]}" pre-build >/dev/null                      # create out/ before the parallel build
"${mk[@]}" -j"${JOBS:-$(nproc)}" all >/dev/null
BIN=$WORK/fw/out/light_cct.bin
ELF=$WORK/fw/out/light_cct.elf

# --- post-build checks ---
PYTHONDONTWRITEBYTECODE=1 python3 - "$BIN" "$REPO/tools" "$BUILD" <<'PY'
import struct, sys
sys.path.insert(0, sys.argv[2])
from make_ota import check_telink_image  # 0x5D02 marker, KNLT at 8, no KNLT at 0x8008, size field, CRC
img = open(sys.argv[1], "rb").read()
build = int(sys.argv[3])
bad = check_telink_image(img)
ram = struct.unpack_from("<H", img, 0xC)[0] * 16
version = int.from_bytes(img[2:6], "little")
print(f"image 0x{len(img):x} bytes (limit 0x34000), RAM code 0x{ram:x} (limit 0x1a00), version 0x{version:08x}")
if len(img) >= 0x34000:
    bad.append("image too large for the 512K OTA slot")
if ram > 0x1A00:
    bad.append("RAM code too large for the Tuya bootloader to launch")
if version != (0x10003001 | (build << 16)):
    bad.append(f"embedded version 0x{version:08x} doesn't match build {build}")
if bad:
    sys.exit("post-build check failed: " + "; ".join(bad))
PY
syms=$("${CROSS}-elf-nm" "$ELF")
if grep -E '__(u?divdi3|u?moddi3|muldi3)$' <<<"$syms"; then
  die "64-bit libgcc helpers linked in (the SDK build has no libgcc for them)"
fi
if [[ -n ${REFERENCE_BIN:-} ]]; then
  cmp "$BIN" "$REFERENCE_BIN" || die "image differs from $REFERENCE_BIN"
  echo "byte-identical to $REFERENCE_BIN"
fi

# --- outputs ---
name=dl41_rgbcw_v1.0.$BUILD
cp "$BIN" "$OUT/$name.bin"
cp "$ELF" "$OUT/$name.elf"
(cd "$OUT" && sha256sum "$name.bin" "$name.elf")
