disting mk4
===========

firmware for the disting mk4 Eurorack module

Requirements:

Microchip Libraries for Applications, legacy v2013-06-15 version
https://www.microchip.com/mplab/microchip-libraries-for-applications
installed or symlinked to 'microchip_solutions_v2013-06-15' alongside the hello_disting_mk4 folder

Microchip XC32 compiler toolchain v1.34

Installing via the SD card (bootloader b2, firmware 4.15+)
----------------------------------------------------------

Bootloader b2 does not accept `image.hex`; it installs a file named `disting4.bin`.
To build one without a PIC programmer:

1. In MPLAB X, build the `MX170F256D_for_bootloader` configuration
   (linker script `pic32mx170f256b_1-31_for_bootloader.ld`).
2. Convert the resulting hex:

       python3 ../tools/hex2disting4bin.py convert \
           dist/MX170F256D_for_bootloader/production/hello_disting_mk4.production.hex \
           -o disting4.bin

3. Copy `disting4.bin` to the root of a FAT32 MicroSD card, power on holding S,
   confirm, and wait for "Success".

To restore the stock firmware, install the official `disting4.bin` the same way.
`python3 ../tools/hex2disting4bin.py check disting4.bin` validates any .bin
against the bootloader's rules before you flash it.

### disting4.bin format (bootloader b2)

Determined by disassembling the bootloader embedded in Expert Sleepers'
`dm4_bootloader_upgrader_b2` and comparing with the stock 4.28 `disting4.bin`.

| What | Detail |
|---|---|
| Layout | Flat image of program flash; file byte N goes to flash address START + N |
| Word 0 | `0xBADABEEF` (else "Corrupt file") |
| Word 1 | START: 1 KB aligned, `0x9D000000` <= START < `0x9D03F400` (else "Invalid start") |
| Integrity | No checksum; each 512-byte sector is verified by read-back after programming |
| Skipped | `0x9D004000`-`0x9D006BFF` (the bootloader itself) |
| Never written | `0x9D03F400`-`0x9D03FFFF` (settings `0xBD03F400`, presets `0xBD03F800`, calibration `0xBD03FE00`) |
| Boot | Jumps to `0xBD008000` only if `0xBD008370` holds `0xBADABEEF` |

Stock firmware uses START = `0x9D000000`; the b2 upgrader and this project use
START = `0x9D007000` (see `b2Header` in `main.c`), so the existing
`_for_bootloader` linker script works unchanged. The first 16 KB of flash
(`0x9D000000`-`0x9D003FFF`) is not used by this build.

Because the bootloader lives outside the area an image can write, a broken
application can always be replaced by powering on with S held, unless the
application itself erases the bootloader or the calibration page.
