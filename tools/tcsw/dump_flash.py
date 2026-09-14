"""Robust read-only TLSR825x flash dump over a USB-UART SWire link.

Built on pvvx's TLSR825xComFlasher helpers. Differences from its `rf` command:
- no reset: syncs SWire speed, then halts the CPU so stock firmware can't
  touch the SPI flash while we drive the flash controller registers
- reads in small chunks with per-chunk retries and SWire re-sync
- appends to the output file, so an interrupted dump can resume (--resume)

Never erases or writes flash.

usage: python dump_flash.py PORT OUTFILE [--start 0] [--size 0x100000] [--chunk 0x40] [--resume]
"""
import argparse
import os
import sys
import time

import serial

import TLSR825xComFlasher as f

REG_CPU_CTRL = 0x0602  # 0x05 = stop CPU
REG_PC = 0x06BC


_strict_decode_blk = f.sws_decode_blk


def _zeros(v):
    return 8 - bin(v).count("1")


def lenient_decode_blk(blk):
    """pvvx's strict decoder first; conservative fallback for drifted samples.

    On an FT232R the UART frame is slightly longer than an SWire bit, so during
    runs of 1-bits (e.g. erased 0xFF flash) each sample shifts left
    (06 0c 18 30 60 c0) and the strict fixed-mask decoder rejects or would
    misread them. Fallback decodes by low-bit count, but only when every data
    sample is unambiguous (>=6 low bits = 1, <=2 = 0) and the stop sample has
    <=5 low bits. Measured on this link: clean 0-bits have 1-3 low bits, clean
    1-bits 3-7, stop samples 1 (4-5 when drifted).
    """
    r = _strict_decode_blk(blk)
    if r is not None or len(blk) != 9:
        return r
    z = [_zeros(b) for b in blk]
    if z[8] > 5 or any(2 < x < 6 for x in z[:8]):
        return None
    data = 0
    for x in z[:8]:
        data = (data << 1) | (1 if x >= 6 else 0)
    return data


def read_pc(port):
    pc = f.sws_read_data(port, REG_PC, 4)
    if pc is None or len(pc) != 4:
        return None
    return pc[0] | (pc[1] << 8) | (pc[2] << 16) | (pc[3] << 24)


def halt_cpu(port):
    for _ in range(3):
        f.rd_sws_wr_addr_usbcom(port, REG_CPU_CTRL, bytearray([0x05]))
        time.sleep(0.02)
    pcs = []
    for _ in range(12):  # flaky links drop reads; need two equal PC samples
        pc = read_pc(port)
        if pc is not None:
            if pc in pcs:
                return True, pc
            pcs.append(pc)
    return False, pcs[0] if pcs else None


def sync(port, swsdiv=None):
    """Auto-sync SWire speed, then optionally pin the divider (auto picks the window edge)."""
    if not f.set_sws_auto_speed(port):
        return False
    if swsdiv:
        f.rd_sws_wr_addr_usbcom(port, 0x00B2, bytearray([swsdiv]))
        back = f.sws_read_data(port, 0x00B2, 1)
        if back is None or back[0] != swsdiv:
            print(f"swsdiv {swsdiv} not accepted (read back {back}); falling back to auto")
            return f.set_sws_auto_speed(port)
    return True


def wait_for_chip(port, seconds):
    """Stream stop-CPU while the user resets the chip by hand; return once SWS answers."""
    stop_cpu = f.sws_wr_addr(REG_CPU_CTRL, bytearray([0x05]))
    swsdiv_24m = int(round(24000000 * 2 / port.baudrate))
    print(f"Waiting up to {seconds}s: ground RST for ~1 s now (repeat if needed)...", flush=True)
    t0 = time.time()
    while time.time() - t0 < seconds:
        t1 = time.time()
        while time.time() - t1 < 1.5:
            for _ in range(5):
                f.wr_usbcom_blk(port, stop_cpu)
            port.reset_input_buffer()
        time.sleep(0.01)
        port.reset_input_buffer()
        f.rd_wr_usbcom_blk(port, f.sws_code_end())
        f.rd_sws_wr_addr_usbcom(port, 0x00B2, bytearray([swsdiv_24m]))
        probe = f.sws_read_data(port, 0x00B2, 1)
        if probe is not None and probe[0] == swsdiv_24m:
            print(f"CHIP CAUGHT after {time.time() - t0:.1f}s", flush=True)
            return True
    return False


NO_SLEEP = False
PARTIAL = False


def read_data_nosleep(port, addr, size=1):
    """pvvx's sws_read_data without the fixed 50 ms pre-read sleep."""
    port.reset_input_buffer()
    f.rd_wr_usbcom_blk(port, f.sws_rd_addr(addr))
    out = []
    for _ in range(size):
        port.write([0xFE])
        blk = port.read(9)
        if len(blk) < 9:
            blk += port.read(9 - len(blk))
        x = f.sws_decode_blk(blk)
        if x is None:
            f.rd_wr_usbcom_blk(port, f.sws_code_end())
            return out if PARTIAL else None  # bytes before the failure are complete
        out.append(x)
    f.rd_wr_usbcom_blk(port, f.sws_code_end())
    return out


def read_chunk(port, offset, size):
    f.rd_sws_wr_addr_usbcom(port, 0x0B3, bytearray([0x80]))  # SWS fifo mode
    f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x00]))   # SPI CS low
    f.rd_sws_wr_addr_usbcom(port, 0x0C, bytearray([0x03, (offset >> 16) & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF, 0]))
    f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x0A]))   # SPI auto-read
    data = read_data_nosleep(port, 0x0C, size) if NO_SLEEP else f.sws_read_data(port, 0x0C, size)
    f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x01]))   # SPI CS high
    f.rd_sws_wr_addr_usbcom(port, 0x0B3, bytearray([0x00]))  # SWS normal mode
    if data is None or (len(data) != size and not PARTIAL) or len(data) == 0:
        return None
    return bytes(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("outfile")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--start", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x100000)
    ap.add_argument("--chunk", type=lambda x: int(x, 0), default=0x10)
    ap.add_argument("--retries", type=int, default=24)
    ap.add_argument("--verify", action="store_true", help="accept a chunk only when two consecutive reads match")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--wait", type=int, default=0, help="seconds to wait for a manual RST reset")
    ap.add_argument("--lenient-stop", action="store_true", help="fallback decode for drifted samples (FT232R, runs of 0xFF)")
    ap.add_argument("--swsdiv", type=int, default=0, help="pin SWire divider after auto-sync (e.g. 52)")
    ap.add_argument("--no-sleep", action="store_true", help="skip pvvx's 50 ms sleep before each chunk read")
    ap.add_argument("--partial", action="store_true", help="keep bytes read before a mid-chunk failure")
    args = ap.parse_args()
    global NO_SLEEP, PARTIAL
    NO_SLEEP = args.no_sleep
    PARTIAL = args.partial
    if PARTIAL and args.verify:
        print("--partial and --verify can't be combined")
        sys.exit(2)
    if args.lenient_stop:
        f.sws_decode_blk = lenient_decode_blk

    port = serial.Serial(args.port, args.baud)
    port.reset_input_buffer()
    port.timeout = 0.1

    if args.wait and not wait_for_chip(port, args.wait):
        print("Timed out waiting for chip")
        sys.exit(1)
    if not sync(port, args.swsdiv):
        print("No SWS response")
        sys.exit(1)
    halted, pc = halt_cpu(port)
    print(f"CPU halted: {halted} (PC=0x{pc:06x})" if pc is not None else "CPU halt: PC unreadable")
    if not halted:
        print("Refusing to read flash while CPU may be running")
        sys.exit(1)

    end = args.start + args.size
    offset = args.start
    mode = "wb"
    if args.resume and os.path.exists(args.outfile):
        done = os.path.getsize(args.outfile) - (os.path.getsize(args.outfile) % args.chunk)
        offset = args.start + done
        with open(args.outfile, "r+b") as fh:
            fh.truncate(done)
        mode = "ab"
        print(f"Resuming at 0x{offset:06x}")

    t0 = time.time()
    first = offset
    bad_reads = mismatches = 0
    last_print = 0.0
    with open(args.outfile, mode) as out:
        while offset < end:
            size = min(args.chunk, end - offset)
            data = None
            prev = None
            for attempt in range(args.retries):
                got = read_chunk(port, offset, size)
                if got is None:
                    bad_reads += 1
                    prev = None
                    time.sleep(0.02)
                    if attempt % 4 == 3:
                        sync(port, args.swsdiv)
                    continue
                if not args.verify or got == prev:
                    data = got
                    break
                if prev is not None:
                    mismatches += 1
                prev = got
            if data is None:
                print(f"\nFailed at 0x{offset:06x} after {args.retries} tries; rerun with --resume")
                sys.exit(1)
            out.write(data)
            offset += len(data)
            if time.time() - last_print > 10 or offset >= end:
                last_print = time.time()
                out.flush()
                rate = (offset - first) / max(time.time() - t0, 1e-6)
                eta = (end - offset) / rate if rate else 0
                print(f"\r0x{offset:06x}/0x{end:06x}  {rate:,.0f} B/s  ETA {eta/60:5.1f} min  "
                      f"bad={bad_reads} mismatch={mismatches}", end="", flush=True)
    print(f"\nDone: {args.outfile} in {(time.time() - t0)/60:.1f} min  bad={bad_reads} mismatch={mismatches}")


if __name__ == "__main__":
    main()
