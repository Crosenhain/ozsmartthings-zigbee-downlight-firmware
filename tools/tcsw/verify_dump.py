"""Second-pass verification of a flash dump. Read-only on the chip.

Re-reads every non-erased 4K sector of DUMP from the (halted) chip and compares.
Erased sectors are only checked for being all 0xFF in DUMP. For any mismatching
byte range, re-reads it until two consecutive reads agree and reports which
side (dump or chip re-read) that agreement supports. Never modifies DUMP;
writes a corrected copy to --out only if every mismatch was resolved.

usage: python verify_dump.py PORT DUMP [--out fixed.bin] [--chunk 0x40]
"""
import argparse
import sys
import time

import serial

import TLSR825xComFlasher as f
import dump_flash as d

SECTOR = 0x1000


def read_range(port, start, size, chunk):
    buf = bytearray()
    off = start
    fails = 0
    while off < start + size:
        n = min(chunk, start + size - off)
        got = d.read_chunk(port, off, n)
        if got is None:
            fails += 1
            if fails % 4 == 0:
                d.sync(port)
            if fails > 200:
                raise RuntimeError(f"giving up at 0x{off:06x}")
            continue
        buf += got
        off += len(got)
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("dump")
    ap.add_argument("--out")
    ap.add_argument("--chunk", type=lambda x: int(x, 0), default=0x40)
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--skip", action="append", default=[], help="range START-END to skip, e.g. 0x8000-0x5C000")
    args = ap.parse_args()
    d.NO_SLEEP = True
    d.PARTIAL = True

    image = bytearray(open(args.dump, "rb").read())
    used = [s for s in range(0, len(image), SECTOR) if image[s:s + SECTOR] != b"\xFF" * SECTOR]
    for rng in args.skip:
        lo, hi = (int(x, 0) for x in rng.split("-"))
        used = [s for s in used if not (lo <= s and s + SECTOR <= hi)]
        print(f"skipping 0x{lo:06x}-0x{hi:06x} (verified by other means)")
    erased = len(image) // SECTOR - len(used)
    print(f"{args.dump}: {len(used)} used sectors to re-read, {erased} erased sectors (all 0xFF in dump)")

    port = serial.Serial(args.port, args.baud)
    port.reset_input_buffer()
    port.timeout = 0.1
    if not d.sync(port):
        print("No SWS response")
        sys.exit(1)
    halted, _ = d.halt_cpu(port)
    print(f"CPU halted: {halted}")
    if not halted:
        sys.exit(1)

    t0 = time.time()
    mismatched = []
    for i, s in enumerate(used):
        chip = read_range(port, s, SECTOR, args.chunk)
        if chip != bytes(image[s:s + SECTOR]):
            idx = [j for j in range(SECTOR) if chip[j] != image[s + j]]
            mismatched.append((s, idx, chip))
            print(f"\n  sector 0x{s:06x}: {len(idx)} bytes differ", flush=True)
        if i % 8 == 7 or i == len(used) - 1:
            done = i + 1
            eta = (time.time() - t0) / done * (len(used) - done)
            print(f"\r{done}/{len(used)} sectors  mismatched={len(mismatched)}  ETA {eta/60:4.1f} min", end="", flush=True)
    print()

    unresolved = 0
    for s, idx, first in mismatched:
        lo, hi = min(idx), max(idx) + 1
        reads = [first[lo:hi]]
        for _ in range(8):
            reads.append(read_range(port, s + lo, hi - lo, args.chunk))
            if reads[-1] == reads[-2]:
                break
        agreed = reads[-1] if reads[-1] == reads[-2] else None
        if agreed is None:
            unresolved += 1
            print(f"  0x{s + lo:06x}-0x{s + hi:06x}: re-reads never agreed")
            continue
        side = "dump was right" if agreed == bytes(image[s + lo:s + hi]) else "dump was WRONG (fixed in --out)"
        print(f"  0x{s + lo:06x}-0x{s + hi:06x}: {side}")
        image[s + lo:s + hi] = agreed

    print(f"verified in {(time.time() - t0)/60:.1f} min: {len(used)} sectors, "
          f"{len(mismatched)} with differences, {unresolved} unresolved")
    if args.out and unresolved == 0:
        open(args.out, "wb").write(image)
        print(f"wrote {args.out}")
    sys.exit(0 if unresolved == 0 else 1)


if __name__ == "__main__":
    main()
