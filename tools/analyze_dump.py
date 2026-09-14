"""First-pass analysis of a TLSR8258 (Tuya ZTU) flash dump. Read-only.

- finds Telink images (b"KNLT" at +8 of sector-aligned offsets), size field, CRC check
- maps used vs erased (0xFF) 4K sectors
- lists printable strings, highlighting Tuya light config / identity keywords
"""
import re
import sys
import zlib

SECTOR = 0x1000
KEYWORDS = re.compile(
    rb"(dmod|cmod|iic|2235|2335|sm22|Jsonver|pwm|_TZ3210|TS0505|klsm24op|mcm6m1ma|pdqu9pot|"
    rb"cwtype|onoffmode|pmemory|colorpfun|cagt|wfct|module|pid|ver|ZTU|1141|ota)",
    re.I,
)


def u32(b, o):
    return int.from_bytes(b[o:o + 4], "little")


def telink_images(flash):
    found = []
    for off in range(0, len(flash) - 0x20, SECTOR):
        if flash[off + 8:off + 12] == b"KNLT":
            found.append(off)
    return found


def crc_variants(data):
    c = zlib.crc32(data) & 0xFFFFFFFF
    return {"crc32": c, "~crc32": c ^ 0xFFFFFFFF}


def main(path):
    flash = open(path, "rb").read()
    print(f"{path}: {len(flash)} bytes (0x{len(flash):x})")

    print("\n## Telink images (KNLT tag)")
    for off in telink_images(flash):
        size = u32(flash, off + 0x18)
        line = f"  0x{off:06x}: size field 0x{size:x} ({size} B)"
        if 0x20 < size <= len(flash) - off:
            body, stored = flash[off:off + size - 4], u32(flash, off + size - 4)
            matches = [k for k, v in crc_variants(body).items() if v == stored]
            line += f", tail 0x{stored:08x}, crc match: {matches or 'none'}"
        print(line)

    print("\n## Sector map (# used, . erased) — 1 char = 4 KB, 64 per row = 256 KB")
    row = ""
    for i, off in enumerate(range(0, len(flash), SECTOR)):
        row += "." if flash[off:off + SECTOR].count(0xFF) == len(flash[off:off + SECTOR]) else "#"
        if (i + 1) % 64 == 0:
            print(f"  0x{off + SECTOR - 0x40000:06x} {row}")
            row = ""
    if row:
        print(f"  ...      {row}")

    print("\n## Strings of interest")
    for m in re.finditer(rb"[\x20-\x7e]{5,}", flash):
        s = m.group()
        if KEYWORDS.search(s) or s.startswith(b"{"):
            print(f"  0x{m.start():06x}: {s[:200].decode('ascii', 'replace')}")


if __name__ == "__main__":
    main(sys.argv[1])
