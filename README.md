disting mk4 sub bass
====================

Custom firmware for the Expert Sleepers disting mk4: a sub-bass generator
for a stereo signal (X/Y in, V/oct on Z, A/B out), built on Expert Sleepers'
open-source mk4 framework.

| Path | Contents |
|---|---|
| `hello_disting_mk4/` | MPLAB X project: the framework plus the Sub bass algorithm. Its README covers parameters, Z calibration, building and installing. |
| `tools/hex2disting4bin.py` | Converts the built `.hex` to a `disting4.bin` for bootloader b2 (SD card install), or validates an existing one. |
| `tools/gen_subbass_tables.py` | Regenerates the DSP lookup tables (`subBassTables.h`). |
| `tools/subbass_host_test.c` | Runs and checks the DSP on a PC. |

The framework is (c) Expert Sleepers Ltd, released under the MIT License (see `LICENSE`).
