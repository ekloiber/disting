#!/usr/bin/env python3
"""
Convert an MPLAB X / XC32 Intel HEX file into a 'disting4.bin' that the
disting mk4 bootloader b2 (firmware 4.15+) will install from the MicroSD card,
or validate an existing disting4.bin.

Format accepted by bootloader b2 (reverse-engineered from the b2 upgrader):

  * The file is a flat image of program flash, beginning at START.
  * Word 0 (little-endian) must be 0xBADABEEF        -> else "Corrupt file"
  * Word 1 is START: 1 KB aligned, 0x9D000000 <= START < 0x9D03F400
                                                     -> else "Invalid start"
  * File byte N is programmed at flash address START + N, in 1 KB pages,
    up to the end of the file or 0x9D03F400, whichever comes first.
  * Pages in 0x9D004000-0x9D006BFF (the bootloader itself) are skipped.
  * 0x9D03F400-0x9D03FFFF (settings, presets, calibration) is never written.
  * There is no checksum; each 512-byte sector is read back and verified.
  * At boot, the bootloader jumps to 0xBD008000 only if the word at
    0xBD008370 is 0xBADABEEF ("No valid firmware installed" otherwise).

Usage:
  hex2disting4bin.py convert INPUT.hex [-o disting4.bin]
  hex2disting4bin.py check   disting4.bin
"""
import argparse
import struct
import sys

MAGIC = 0xBADABEEF
FLASH_LO = 0x1D000000           # physical addresses
FLASH_HI = 0x1D03F400           # bootloader never writes at or above this
BOOTLOADER_LO = 0x1D004000
BOOTLOADER_HI = 0x1D006C00
APP_MAGIC_ADDR = 0x1D008370
APP_RESET_ADDR = 0x1D008000
PAGE = 0x400


def phys(addr):
    """KSEG0/KSEG1 virtual -> physical."""
    return addr & 0x1FFFFFFF


def read_hex(path):
    mem = {}
    base = 0
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(':'):
                sys.exit(f"{path}:{lineno}: not an Intel HEX record")
            rec = bytes.fromhex(line[1:])
            if sum(rec) & 0xFF:
                sys.exit(f"{path}:{lineno}: bad record checksum")
            n, addr, rtype = rec[0], (rec[1] << 8) | rec[2], rec[3]
            data = rec[4:4 + n]
            if rtype == 0x00:
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif rtype == 0x01:
                break
            elif rtype == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif rtype == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            # 0x03 / 0x05 (start address records) are irrelevant here
    return {phys(a): b for a, b in mem.items()}


def check(image, verbose=True):
    """Apply the bootloader b2's rules to an image. Returns list of errors."""
    errors, warnings = [], []
    if len(image) < 8:
        return ["file too short"]
    magic, start = struct.unpack_from('<II', image, 0)
    if magic != MAGIC:
        errors.append(f"word 0 is 0x{magic:08X}, expected 0xBADABEEF (bootloader: 'Corrupt file')")
    if start & 0x3FF or not (0x9D000000 <= start < 0x9D03F400):
        errors.append(f"start address 0x{start:08X} invalid (bootloader: 'Invalid start')")
    p_start = phys(start)
    p_end = p_start + len(image)

    def at(paddr, n=4):
        off = paddr - p_start
        if 0 <= off and off + n <= len(image):
            return image[off:off + n]
        return None

    m = at(APP_MAGIC_ADDR)
    if m is None or struct.unpack('<I', m)[0] != MAGIC:
        errors.append("no 0xBADABEEF at 0xBD008370 (bootloader: 'No valid firmware installed')")
    r = at(APP_RESET_ADDR)
    if r is None or r == b'\xff\xff\xff\xff':
        errors.append("nothing at the application reset address 0xBD008000")
    if p_end > FLASH_HI:
        warnings.append(f"image runs to 0x{p_end | 0x80000000:08X}; bytes from 0x9D03F400 are ignored")
    lo, hi = max(p_start, BOOTLOADER_LO), min(p_end, BOOTLOADER_HI)
    if lo < hi and any(b != 0xFF for b in image[lo - p_start:hi - p_start]):
        warnings.append("image has data in 0x9D004000-0x9D006BFF, which the bootloader skips")
    if len(image) % PAGE:
        warnings.append("length is not a multiple of 1 KB (trailing sector bytes would be programmed)")
    if verbose:
        print(f"start 0x{start:08X}, length {len(image)} (0x{len(image):X}), "
              f"ends 0x{(p_end | 0x80000000):08X}")
        for w in warnings:
            print("warning:", w)
        for e in errors:
            print("ERROR:", e)
        print("OK" if not errors else "FAILED")
    return errors


def convert(hex_path, out_path):
    mem = read_hex(hex_path)
    dropped = {a for a in mem if not (FLASH_LO <= a < FLASH_HI)}
    if dropped:
        lo, hi = min(dropped), max(dropped)
        print(f"note: dropping {len(dropped)} bytes outside program flash "
              f"(0x{lo:08X}-0x{hi:08X}, e.g. configuration bits)")
    flash = {a: b for a, b in mem.items() if a not in dropped}
    if not flash:
        sys.exit("no program-flash data in hex file")

    hdr = bytes(flash.get(a, 0xFF) for a in range(min(flash), min(flash) + 8))
    magic, start = struct.unpack('<II', hdr)
    if magic != MAGIC:
        sys.exit(f"lowest program-flash address 0x{min(flash) | 0x80000000:08X} does not hold "
                 "the { 0xBADABEEF, start } header; see b2Header in main.c")
    p_start = phys(start)
    if p_start != min(flash):
        sys.exit(f"header start address 0x{start:08X} does not match where it is located "
                 f"(0x{min(flash) | 0x80000000:08X})")
    in_bl = [a for a in flash if BOOTLOADER_LO <= a < BOOTLOADER_HI and flash[a] != 0xFF]
    if in_bl:
        sys.exit(f"hex has data at 0x{min(in_bl) | 0x80000000:08X}, inside the bootloader's region "
                 "0x9D004000-0x9D006BFF; fix the linker script")

    p_end = max(flash) + 1
    p_end = (p_end + PAGE - 1) & ~(PAGE - 1)
    image = bytes(flash.get(a, 0xFF) for a in range(p_start, p_end))
    with open(out_path, 'wb') as f:
        f.write(image)
    print(f"wrote {out_path}")
    return check(image)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('convert', help='convert .hex to disting4.bin')
    c.add_argument('hex')
    c.add_argument('-o', '--output', default='disting4.bin')
    k = sub.add_parser('check', help='validate an existing disting4.bin')
    k.add_argument('bin')
    a = ap.parse_args()
    if a.cmd == 'convert':
        errs = convert(a.hex, a.output)
    else:
        with open(a.bin, 'rb') as f:
            errs = check(f.read())
    sys.exit(1 if errs else 0)


if __name__ == '__main__':
    main()
