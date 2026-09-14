#!/usr/bin/env python3
"""Package one DL41 RGBCW build into GitHub release assets.

usage: python3 ci/package_release.py --bin BIN --build N --repo OWNER/REPO --out DIR [--changes FILE]

DIR receives (the index names are constant so /releases/latest/download/<name> always works):
  dl41_rgbcw_v1.0.N.bin               wired recovery image (bench only)
  dl41_rgbcw_v1.0.N.zigbee            OTA for lights already on this firmware (mfr 0x0EBA, type 0x0241)
  dl41_rgbcw_v1.0.N_from_tuya.zigbee  OTA stock Tuya -> custom (mfr 0x1141, type 0xD3A3)
  dl41_rgbcw.mjs                      Zigbee2MQTT external converter
  dl41_ota_index.json                 Z2M OTA index, custom -> custom only
  dl41_ota_index_from_tuya.json       Z2M OTA index, stock -> custom, restricted to the proven stock light
  LICENSE, NOTICE                     Apache-2.0 terms and attributions for the binaries
  SHA256SUMS                          `sha256sum -c` format
  RELEASE_NOTES.md                    release body (not uploaded as an asset)
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import string
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAKE_OTA = ROOT / "tools" / "make_ota.py"
CONVERTER = ROOT / "z2m" / "dl41_rgbcw.mjs"
NOTES_TEMPLATE = ROOT / "ci" / "release_notes.md.tmpl"

OTA_HDR = struct.Struct("<IHHHHHIH32sI")  # same layout as tools/make_ota.py
CUSTOM_MFR, CUSTOM_TYPE = 0x0EBA, 0x0241
TUYA_MFR, TUYA_TYPE = 0x1141, 0xD3A3
MIN_FROM_TUYA_BUILD = 10   # 1.0.02-1.0.09 stage 1 can't hand off from the Tuya bootloader
MAX_IMAGE = 0x34000        # 512K OTA slot
# zigbee-herdsman's findMatchingOtaImage honours these filters. 0x1141/0xD3A3 alone would match
# other Tuya lights, so the stock entry is pinned to the exact model, manufacturer and version it was proven on.
STOCK_FILTER = {"modelId": "TS0505B", "manufacturerName": ["_TZ3210_klsm24op"],
                "minFileVersion": 101, "maxFileVersion": 101}
CUSTOM_FILTER = {"modelId": "DL41-RGBCW"}


def die(msg):
    sys.exit(f"package_release: {msg}")


def file_version(build):
    return 0x10003001 | (build << 16)  # 0x10 <build> 30 01, as APP_RELEASE/APP_BUILD/STACK in version_cfg.h


def make_ota(src, out, mfr, image_type, version, header):
    if len(header.encode()) > 31:
        die(f"OTA header string too long: {header!r}")
    subprocess.run([sys.executable, str(MAKE_OTA), "from-bin", str(src), str(out),
                    "--mfr", f"0x{mfr:04X}", "--type", f"0x{image_type:04X}",
                    "--version", f"0x{version:08X}", "--header-string", header],
                   check=True, stdout=subprocess.DEVNULL)
    data = out.read_bytes()
    _, _, _, _, got_mfr, got_type, got_ver, _, _, total = OTA_HDR.unpack_from(data)
    if (got_mfr, got_type, got_ver, total) != (mfr, image_type, version, len(data)):
        die(f"{out.name}: header mfr/type/version/size = {(got_mfr, got_type, got_ver, total)}")
    return data


def index_json(path, data, url, filters, notes_url):
    _, _, _, _, mfr, image_type, version, _, hstr, _ = OTA_HDR.unpack_from(data)
    entry = {
        "fileName": path.name,
        "fileVersion": version,
        "fileSize": len(data),
        "url": url,
        "imageType": image_type,
        "manufacturerCode": mfr,
        "sha512": hashlib.sha512(data).hexdigest(),
        "otaHeaderString": hstr.rstrip(b"\x00").decode(),
        "releaseNotes": notes_url,
        **filters,
    }
    return json.dumps([entry], indent=2) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True, type=pathlib.Path)
    ap.add_argument("--build", required=True, type=int)
    ap.add_argument("--repo", required=True, help="OWNER/REPO")
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--changes", type=pathlib.Path, help="changelog text, e.g. the annotated tag message")
    args = ap.parse_args()

    n = args.build
    if not MIN_FROM_TUYA_BUILD <= n <= 255:
        die(f"build {n}: need {MIN_FROM_TUYA_BUILD} <= N <= 255")
    version, fv = f"1.0.{n}", file_version(n)
    tag = f"v{version}"

    img = args.bin.read_bytes()
    embedded = int.from_bytes(img[2:6], "little")
    if embedded != fv:
        die(f"{args.bin} embeds version 0x{embedded:08x}, expected 0x{fv:08x}")
    if len(img) >= MAX_IMAGE:
        die(f"image is {len(img)} bytes, must be < 0x{MAX_IMAGE:x}")

    out = args.out
    if out.exists() and any(out.iterdir()):
        die(f"{out} is not empty")
    out.mkdir(parents=True, exist_ok=True)
    base = f"https://github.com/{args.repo}/releases/download/{tag}"
    notes_url = f"https://github.com/{args.repo}/releases/tag/{tag}"

    bin_out = out / f"dl41_rgbcw_v{version}.bin"
    shutil.copyfile(args.bin, bin_out)
    ota = out / f"dl41_rgbcw_v{version}.zigbee"
    ota_tuya = out / f"dl41_rgbcw_v{version}_from_tuya.zigbee"
    ota_data = make_ota(bin_out, ota, CUSTOM_MFR, CUSTOM_TYPE, fv, f"DL41 RGBCW {version}")
    tuya_data = make_ota(bin_out, ota_tuya, TUYA_MFR, TUYA_TYPE, fv, f"DL41 RGBCW {version} from Tuya")
    shutil.copyfile(CONVERTER, out / CONVERTER.name)
    for legal in ("LICENSE", "NOTICE"):
        shutil.copyfile(ROOT / legal, out / legal)
    (out / "dl41_ota_index.json").write_text(
        index_json(ota, ota_data, f"{base}/{ota.name}", CUSTOM_FILTER, notes_url))
    (out / "dl41_ota_index_from_tuya.json").write_text(
        index_json(ota_tuya, tuya_data, f"{base}/{ota_tuya.name}", STOCK_FILTER, notes_url))

    sums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
                   for p in sorted(out.iterdir()) if p.is_file())
    (out / "SHA256SUMS").write_text(sums)

    changes = args.changes.read_text().strip() if args.changes and args.changes.exists() else ""
    notes = string.Template(NOTES_TEMPLATE.read_text()).substitute(
        version=version, tag=tag, repo=args.repo, file_version=fv, file_version_hex=f"0x{fv:08X}",
        changes=changes or "See the README for details.", sha256sums=sums.rstrip())
    (out / "RELEASE_NOTES.md").write_text(notes)
    print(f"packaged {tag} in {out}\n{sums}", end="")


if __name__ == "__main__":
    main()
