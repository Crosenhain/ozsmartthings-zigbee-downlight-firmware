"""SWire link quality check. Read-only.

Catches the chip (manual RST window if needed), halts the CPU, captures raw
9-sample blocks for a region twice, and reports how many samples are clean
(aligned 1 = 0x80/0xC0, aligned 0 = 0xFE/0xFC) versus drifted, plus how many
bytes the strict pvvx decoder reads differently between passes.

usage: python quality_check.py PORT [--wait 180] [--start 0x6000] [--size 0x1000]
"""
import argparse
import collections
import sys

import serial

import TLSR825xComFlasher as f
import dump_flash as d

CLEAN = {0x80, 0xC0, 0xFE, 0xFC}


def strict_value(blk):
    v, m = 0, 0x20
    for i in range(8):
        v = (v << 1) | (1 if (blk[i] & m) == 0 else 0)
        m = 0x10
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--wait", type=int, default=180)
    ap.add_argument("--start", type=lambda x: int(x, 0), default=0x6000)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x1000)
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud)
    port.reset_input_buffer()
    port.timeout = 0.1

    if f.set_sws_auto_speed(port):
        print("Chip already answering (still halted)")
    elif not d.wait_for_chip(port, args.wait) or not f.set_sws_auto_speed(port):
        print("No SWS response")
        sys.exit(1)
    halted, pc = d.halt_cpu(port)
    print(f"CPU halted: {halted}")
    div = f.sws_read_data(port, 0x00B2, 1)
    print(f"swsdiv: {div}")

    cap = []

    def rec(blk):
        if len(blk) != 9:
            return None
        cap.append(bytes(blk))
        return 0

    f.sws_decode_blk = rec
    passes = []
    for rep in range(2):
        cap.clear()
        for off in range(args.start, args.start + args.size, 0x100):
            d.read_chunk(port, off, 0x100)
        passes.append(list(cap))

    samples = collections.Counter(s for blk in passes[0] for s in blk[1:8])
    total = sum(samples.values())
    clean = sum(c for v, c in samples.items() if v in CLEAN)
    stops = collections.Counter(blk[8] for blk in passes[0])
    n = min(len(passes[0]), len(passes[1]))
    diff = sum(1 for i in range(n) if strict_value(passes[0][i]) != strict_value(passes[1][i]))
    print(f"captured {len(passes[0])}/{len(passes[1])} bytes")
    print(f"clean data samples: {clean}/{total} ({100 * clean / max(total, 1):.1f}%)")
    print("top samples:", {f"{v:02x}": c for v, c in samples.most_common(10)})
    print("stop samples:", {f"{v:02x}": c for v, c in stops.most_common(5)})
    print(f"strict-decoder bytes differing between passes: {diff}/{n}")


if __name__ == "__main__":
    main()
