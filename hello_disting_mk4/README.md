disting mk4
===========

firmware for the disting mk4 Eurorack module

Requirements
------------

* MPLAB X IDE (current version)
* MPLAB XC32 compiler (current version)
* The legacy peripheral library (`plib`). Current XC32 ships its headers and
  source but not the built library. Build it once per machine:

      python3 tools/build_plib.py

  This compiles the sources in your XC32's `pic32-libs/peripheral` into
  `hello_disting_mk4/plib/libmchp_peripheral_32MX170F256D.a` (not committed),
  which the project links. If the compiler cannot find `plib.h` or
  `GenericTypeDefs.h`, copy them (and the `peripheral` folder) from
  `pic32-libs/include` or `pic32mx/include` into `pic32m/include`.
  (XC32 v1.34 has everything built in and needs none of this.)
* Python 3, to make `disting4.bin` (see below)

The Microchip Libraries for Applications (MLA) are no longer needed: the SD
card code that used them has been removed (the "Test SD" menu item and the two
SD playback settings are gone). Installing firmware from the SD card is done
by the bootloader, not this code, so it is unaffected.

When you open the project, MPLAB X may say the XC32 version it was saved with
(1.34) is missing: choose your installed XC32 under Project Properties >
Compiler Toolchain. Warnings are no longer treated as errors, since newer
compilers warn about more of this older code.

Sub voice (algorithm A1)
------------------------

This build has a single algorithm: a gated sub-bass voice.

| Socket | Use |
|---|---|
| X | V/oct pitch: 0 V = C4 before Octave (uses the module's factory input calibration, no setup needed) |
| Y | gate: high above 1 V, low below 0.5 V |
| Z | velocity: the Z knob plus any CV on the Z jack. Knob fully counter-clockwise = silent, fully clockwise = full. Applies continuously, like a VCA |
| A | sub audio; Level 99 = 5 V peak at full velocity |
| B | envelope CV, 0..5 V (not scaled by velocity or Level) |

While the gate is high the envelope rises (Attack) to full and stays there;
when the gate goes low it falls (Release). A new gate during the release
attacks again from the current level, so there is no click.

Each note starts the waveform at phase 0 (a zero crossing for sine,
triangle and saw; square starts at full and is smoothed by Attack), so every
note begins identically. This only happens when the previous note has
released below -60 dB: if it is still sounding, the waveform keeps running,
because jumping it back to zero would click.

Velocity follows the Z knob (and Z CV) continuously, smoothed over a couple of
milliseconds so turning it doesn't zipper. 0 V on X is C4 before the Octave
parameter, so the default (Octave -2) puts 0 V at C2.

| # | Name | Range | Default | Meaning |
|---|---|---|---|---|
| 0 | Shape | 0..99 | 0 | 0 sine, 33 triangle, 66 saw, 99 square (morphs between) |
| 1 | Drive | 0..99 | 0 | saturation on the sub |
| 2 | Level | 0..99 | 50 | sub level; 99 = 5 V peak at full envelope and velocity |
| 3 | Octave | -4..0 | -2 | octaves relative to Z |
| 4 | Fine | -50..50 | 0 | cents |
| 5 | Quant | 0..1 | 1 | snap pitch to semitones (hides Z reading noise) |
| 6 | Attack | 0..99 | 10 | 0.5 ms .. 200 ms (time constant; full level arrives after about 1.6x) |
| 7 | Release | 0..99 | 30 | 10 ms .. 2 s (time constant) |

Controls are the framework's: turn S to edit, hold S and turn to pick a
parameter (its number shows, then its name scrolls), press S for the menu.

### Testing the DSP without hardware

`tools/subbass_host_test.c` runs the DSP core (`subBassDSP.c`) on a PC and
checks pitch, gate thresholds, envelope, velocity and levels. Build commands
are at the top of the file. `tools/gen_subbass_tables.py` regenerates
`subBassTables.h`.

### Checking CPU load on the hardware

The framework drives RC9 low while waiting for the next sample (`IDLE()`).
On a scope, the low time is the spare CPU per sample at 75 kHz (13.3 us total).
If it reaches zero, raise `CONTROL_INTERVAL` in `subBass.c` first.

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
