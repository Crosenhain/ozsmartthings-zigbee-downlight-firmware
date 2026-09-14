"""Read-only post-OTA diagnosis of the Tuya -> DL41 custom migration.

1. Try SWire sync with no reset. (Stock Tuya firmware disables SWS after boot;
   an SDK build leaves it enabled.)
2. Sample the CPU program counter a few times while running, then halt.
3. Read the boot flags, the image headers at 0x0 / 0x8000 / 0x40000, sample
   pages of the copied image against the local build, and the wipe areas.

usage: python diag_migration.py PORT BUILD_BIN [--wait 0]
"""
import argparse
import sys
import time

import serial

import TLSR825xComFlasher as f
import dump_flash as d


def rd(port, addr, n):
    for _ in range(12):
        got = d.read_chunk(port, addr, n)
        if got is not None and len(got) == n:
            return got
        d.sync(port)
    return None


def u32(b):
    return None if b is None else int.from_bytes(b[:4], "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("build_bin")
    ap.add_argument("--wait", type=int, default=0)
    args = ap.parse_args()
    d.NO_SLEEP = True
    d.PARTIAL = False

    image = open(args.build_bin, "rb").read()
    port = serial.Serial(args.port, 921600)
    port.reset_input_buffer()
    port.timeout = 0.1

    if f.set_sws_auto_speed(port):
        print("SWS answered WITHOUT reset -> non-stock code is running (or CPU already halted)")
    elif args.wait and d.wait_for_chip(port, args.wait) and f.set_sws_auto_speed(port):
        print("SWS answered only after manual reset window")
    else:
        print("No SWS response (stock firmware or bootloader running, or link problem)")
        sys.exit(1)

    pcs = []
    for _ in range(6):
        pc = d.read_pc(port)
        pcs.append("??" if pc is None else f"0x{pc:06x}")
        time.sleep(0.05)
    print("PC samples while running:", " ".join(pcs))

    halted, pc = d.halt_cpu(port)
    print(f"halted: {halted}  PC=0x{pc:06x}" if pc is not None else f"halted: {halted}")

    def show(label, addr, n=0x20):
        b = rd(port, addr, n)
        print(f"{label:28s} 0x{addr:06x}: {b.hex(' ') if b else 'READ FAILED'}")
        return b

    b0 = show("sector0 header (Tuya BL?)", 0x0000)
    b8 = show("0x8000 header (Tuya app slot)", 0x8000)
    b4 = show("0x40000 header (target slot)", 0x40000)
    print(f"flags: 0x0008={u32(b0[8:12]) if b0 else None!s:>12}  0x8008={u32(b8[8:12]) if b8 else None!s:>12}  "
          f"0x40008={u32(b4[8:12]) if b4 else None!s:>12}  (KNLT=0x544c4e4b={0x544c4e4b})")

    size = len(image)
    samples = [0x0, 0x100, size // 2 & ~0xFF, (size - 0x100) & ~0xFF]
    for off in samples:
        got = rd(port, 0x40000 + off, 0x100)
        state = "READ FAILED" if got is None else ("match" if got == image[off:off + 0x100] else "DIFFERENT")
        print(f"image @0x40000+0x{off:05x} vs build: {state}")

    for label, addr in (("NV1 0x34000", 0x34000), ("NV1 0x35000", 0x35000), ("cfg 0x78000", 0x78000),
                        ("rstcnt 0x79000", 0x79000), ("NV2 0x7A000", 0x7A000), ("staging? 0x70000", 0x70000)):
        b = rd(port, addr, 0x40)
        if b is None:
            print(f"{label:18s}: READ FAILED")
        else:
            erased = all(x == 0xFF for x in b)
            print(f"{label:18s}: {'erased' if erased else 'DATA'}  {b[:24].hex(' ')}")

    print("CPU left halted; power-cycle the module to run again.")


if __name__ == "__main__":
    main()
