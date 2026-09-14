"""Wrap a Telink TLSR825x firmware image in a Zigbee OTA upgrade file.

Layout (Zigbee Cluster Library OTA file format, little endian):
  56-byte OTA header -> 6-byte sub-element header (tag 0x0000 = upgrade image) -> Telink image

The Telink image must already be OTA-ready: 0x5D 0x02 at offset 6, image size
(including trailing CRC) at 0x18, and inverted CRC32 over everything before the
last 4 bytes appended at the end. This tool checks that rather than modifying it.

usage:
  python make_ota.py from-dump DUMP OUT [--app-offset 0x8000] [--mfr 0x1141] [--type 0xD3A3] [--version 0xFFFFFFFF]
  python make_ota.py from-bin  BIN  OUT [--mfr ...] [--type ...] [--version ...]
  python make_ota.py inspect   OTAFILE
"""
import argparse
import binascii
import struct
import sys

OTA_MAGIC = 0x0BEEF11E
HDR = struct.Struct("<IHHHHHIH32sI")  # 56 bytes, no optional fields
SUB = struct.Struct("<HI")


def check_telink_image(img):
    problems = []
    if img[6:8] != b"\x5d\x02":
        problems.append("missing Telink OTA marker 0x5D02 at offset 6")
    if img[8:12] != b"KNLT":
        problems.append("missing KNLT tag at offset 8")
    if img[0x8008:0x800C] == b"KNLT":
        # tuya_migrate stage 1 treats a memory-mapped 'KNLT' at 0x8008 as "launched by the Tuya bootloader"
        problems.append("image has 'KNLT' at offset 0x8008: tuya_migrate would misdetect a normal boot")
    size = int.from_bytes(img[0x18:0x1C], "little")
    if size != len(img):
        problems.append(f"size field 0x{size:x} != image length 0x{len(img):x}")
    stored = int.from_bytes(img[-4:], "little")
    calc = binascii.crc32(img[:-4]) ^ 0xFFFFFFFF
    if stored != calc:
        problems.append(f"CRC mismatch: stored 0x{stored:08x}, computed 0x{calc:08x}")
    return problems


def build(img, mfr, image_type, version, header_string):
    hs = header_string.encode()[:31]
    total = HDR.size + SUB.size + len(img)
    header = HDR.pack(OTA_MAGIC, 0x0100, HDR.size, 0x0000, mfr, image_type, version, 0x0002,
                      hs.ljust(32, b"\x00"), total)
    return header + SUB.pack(0x0000, len(img)) + img


def inspect(data):
    (magic, hver, hlen, fctl, mfr, itype, ver, stack, hstr, total) = HDR.unpack_from(data)
    print(f"magic        0x{magic:08x} {'ok' if magic == OTA_MAGIC else 'BAD'}")
    print(f"header       v0x{hver:04x} len {hlen} fieldctl 0x{fctl:04x}")
    print(f"manufacturer 0x{mfr:04x}  image type 0x{itype:04x}  file version 0x{ver:08x}  stack {stack}")
    header_str = hstr.rstrip(b"\x00").decode(errors="replace")  # outside the f-string: Python < 3.12
    print(f"header str   {header_str!r}")
    print(f"total size   {total} (file {len(data)}) {'ok' if total == len(data) else 'BAD'}")
    tag, sublen = SUB.unpack_from(data, hlen)
    img = data[hlen + SUB.size:hlen + SUB.size + sublen]
    print(f"sub-element  tag 0x{tag:04x} length {sublen}")
    problems = check_telink_image(img)
    print("telink image " + ("ok (marker, KNLT, size, CRC)" if not problems else "; ".join(problems)))
    return not problems and magic == OTA_MAGIC and total == len(data)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("from-dump", "from-bin"):
        p = sub.add_parser(name)
        p.add_argument("src")
        p.add_argument("out")
        p.add_argument("--app-offset", type=lambda x: int(x, 0), default=0x8000)
        p.add_argument("--mfr", type=lambda x: int(x, 0), default=0x1141)
        p.add_argument("--type", dest="image_type", type=lambda x: int(x, 0), default=0xD3A3)
        p.add_argument("--version", type=lambda x: int(x, 0), default=0xFFFFFFFF)
        p.add_argument("--header-string", default="DL41 stock repack test")
    p = sub.add_parser("inspect")
    p.add_argument("src")
    args = ap.parse_args()

    if args.cmd == "inspect":
        sys.exit(0 if inspect(open(args.src, "rb").read()) else 1)

    raw = open(args.src, "rb").read()
    if args.cmd == "from-dump":
        base = args.app_offset
        size = int.from_bytes(raw[base + 0x18:base + 0x1C], "little")
        img = raw[base:base + size]
    else:
        img = raw
    problems = check_telink_image(img)
    if problems:
        print("refusing to package: " + "; ".join(problems))
        sys.exit(1)
    data = build(img, args.mfr, args.image_type, args.version, args.header_string)
    open(args.out, "wb").write(data)
    print(f"wrote {args.out} ({len(data)} bytes)")
    sys.exit(0 if inspect(data) else 1)


if __name__ == "__main__":
    main()
