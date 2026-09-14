"""Write an image to TLSR825x flash over a USB-UART SWire link, page-verified.

Designed for recovering a module that is halted (or whose firmware leaves SWS
enabled). Order of operations keeps the boot flag for last:
  1. sync, halt CPU, clear flash write protection
  2. erase every sector covering [ADDR, ADDR+len)
  3. write pages from offset 0x100 to the end, verifying each by read-back
  4. write page 0 (holds the KNLT boot flag) last, verify
A failure at any point leaves no half-image marked bootable at ADDR.

usage: python write_flash_verified.py PORT ADDR IMAGE [--wait 0] [--dry-run]
"""
import argparse
import sys
import time

import serial

import TLSR825xComFlasher as f
import dump_flash as d

PAGE = 0x100
SECTOR = 0x1000


def read_exact(port, addr, n, tries=16):
    for i in range(tries):
        got = d.read_chunk(port, addr, n)
        if got is not None and len(got) == n:
            return got
        if i % 4 == 3:
            d.sync(port)
    return None


def write_page(port, addr, data, tries=8):
    for i in range(tries):
        f.FlashWriteEnable(port)
        f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x00]))  # CS low
        f.rd_sws_fifo_wr_usbcom(port, 0x0C, bytearray([0x02, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF]) + bytearray(data))
        f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x01]))  # CS high
        time.sleep(0.01)
        back = read_exact(port, addr, len(data))
        if back == bytes(data):
            return True
        if back is not None and any((b & ~a) & 0xFF for a, b in zip(data, back)):
            # bits that must go 0->1 can't be fixed by rewriting; needs a sector erase
            return False
        if i % 3 == 2:
            d.sync(port)
    return False


def erase_sector(port, addr, tries=6):
    for _ in range(tries):
        f.FlashWriteEnable(port)
        f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x00]))  # CS low
        # One byte per SWire write: a multi-byte write without FIFO mode walks
        # into registers 0x0D-0x0F (chip select) instead of reaching the flash.
        for byte in (0x20, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF):
            f.rd_sws_wr_addr_usbcom(port, 0x0C, bytearray([byte]))
        f.rd_sws_wr_addr_usbcom(port, 0x0D, bytearray([0x01]))  # CS high
        time.sleep(0.4)
        head = read_exact(port, addr, 16)
        tail = read_exact(port, addr + SECTOR - 16, 16)
        if head == b"\xFF" * 16 and tail == b"\xFF" * 16:
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("addr", type=lambda x: int(x, 0))
    ap.add_argument("image")
    ap.add_argument("--wait", type=int, default=0)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--self-test", type=lambda x: int(x, 0),
                    help="erase/write/verify/erase one unused sector at this address, then exit")
    args = ap.parse_args()
    d.NO_SLEEP = True
    d.PARTIAL = False

    img = open(args.image, "rb").read()
    if len(img) % PAGE:
        img += b"\xFF" * (PAGE - len(img) % PAGE)
    end = args.addr + len(img)
    sectors = list(range(args.addr & ~(SECTOR - 1), (end + SECTOR - 1) & ~(SECTOR - 1), SECTOR))
    print(f"{args.image}: {len(img)} bytes -> 0x{args.addr:06x}-0x{end:06x}, {len(sectors)} sectors")

    port = serial.Serial(args.port, 921600)
    port.reset_input_buffer()
    port.timeout = 0.1
    if not d.sync(port):
        if not (args.wait and d.wait_for_chip(port, args.wait) and d.sync(port)):
            print("No SWS response")
            sys.exit(1)
    halted, _ = d.halt_cpu(port)
    print(f"CPU halted: {halted}")
    if not halted:
        sys.exit(1)
    if args.dry_run:
        print("dry run: stopping before any flash modification")
        return

    if not f.FlashUnlock(port):
        print("warning: flash status unlock not confirmed; continuing (verify will catch failures)")

    if args.self_test is not None:
        s = args.self_test & ~(SECTOR - 1)
        pattern = bytes((i * 7 + 0x5A) & 0xFF for i in range(PAGE))
        ok = erase_sector(port, s)
        print(f"self-test erase 0x{s:06x}: {ok}")
        ok = ok and write_page(port, s + PAGE, pattern)
        print(f"self-test write+verify page 0x{s + PAGE:06x}: {ok}")
        ok = ok and erase_sector(port, s)
        print(f"self-test re-erase 0x{s:06x}: {ok}")
        print("SELF-TEST PASSED" if ok else "SELF-TEST FAILED")
        sys.exit(0 if ok else 4)

    t0 = time.time()
    for s in sectors:
        if not erase_sector(port, s):
            print(f"FAILED to erase sector 0x{s:06x}")
            sys.exit(2)
    print(f"erased {len(sectors)} sectors in {time.time() - t0:.0f}s")

    order = list(range(PAGE, len(img), PAGE)) + [0]
    for n, off in enumerate(order):
        page = img[off:off + PAGE]
        if page == b"\xFF" * PAGE:
            continue
        if not write_page(port, args.addr + off, page):
            print(f"\nFAILED to write/verify page 0x{args.addr + off:06x} (boot flag page {'written' if off == 0 else 'NOT written'})")
            sys.exit(3)
        if n % 16 == 0 or off == 0:
            done = n + 1
            eta = (time.time() - t0) / done * (len(order) - done)
            print(f"\rpage {done}/{len(order)}  0x{args.addr + off:06x}  ETA {eta / 60:4.1f} min", end="", flush=True)
    print(f"\nwrote and verified {len(img)} bytes in {(time.time() - t0) / 60:.1f} min")
    hdr = read_exact(port, args.addr, 0x20)
    print("header now:", hdr.hex(" ") if hdr else "READ FAILED")
    print("CPU left halted; power-cycle the module to boot.")


if __name__ == "__main__":
    main()
