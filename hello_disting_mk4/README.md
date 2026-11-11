disting mk4
===========

firmware for the disting mk4 Eurorack module

Requirements:

Microchip Libraries for Applications, legacy v2013-06-15 version
https://www.microchip.com/mplab/microchip-libraries-for-applications
installed or symlinked to 'microchip_solutions_v2013-06-15' alongside the hello_disting_mk4 folder

Microchip XC32 compiler toolchain v1.34

Sub voice (algorithm A1)
------------------------

This build has a single algorithm: a gated sub-bass voice.

| Socket | Use |
|---|---|
| X | gate: high above 1 V, low below 0.5 V |
| Y | velocity: 0..5 V, sampled when the gate opens (0 V or less = silent, above 5 V = full) |
| Z | V/oct pitch (the Z knob is added to the jack; keep it fully counter-clockwise) |
| A | sub audio; Level 99 = 5 V peak at full velocity |
| B | envelope CV, 0..5 V (not scaled by velocity or Level) |

While the gate is high the envelope rises (Attack) to full and stays there;
when the gate goes low it falls (Release). A new gate during the release
attacks again from the current level, so there is no click.

Each note starts the waveform at phase 0 (a zero crossing for sine,
triangle and saw; square starts at full and is smoothed by Attack), so every
note begins identically. This only happens when the previous note has
released below -60 dB: if it is still sounding, the waveform keeps running,
because jumping it back to zero would click. Velocity is
read once per note; changing Y during a note has no effect until the next
gate. 0 V on Z is C4 before the Octave parameter, so the default
(Octave -2) puts 0 V at C2.

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

### Calibrating Z (do this once)

The Z knob and jack are summed into one 10-bit reading that the factory
calibration does not cover. Until you calibrate, the display says
"Z not calibrated" and pitch uses a rough nominal scale.

1. Turn the Z knob fully counter-clockwise.
2. Press S, turn to "Cal Z", press S.
3. Patch exactly 1 V into Z (e.g. a quantiser or a known-good CV source) and press S.
4. Patch exactly 3 V and press S. The display shows "Z calibrated".

Press Z at either prompt to cancel. "Cal failed" means a reading was at the
ADC limit or the two readings were too close. The calibration is stored in the
settings flash page (offset 0x100), which the bootloader never writes, so it
survives reinstalling this firmware. Going back to the stock firmware may
overwrite it; recalibrate if so.

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
