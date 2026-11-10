disting mk4 sub voice
=====================

Custom firmware for the Expert Sleepers disting mk4: a gated sub-bass voice
(X gate, Y velocity, Z V/oct; A audio, B envelope), built on Expert Sleepers'
open-source mk4 framework. (The `sub-bass` branch has the stereo sub-bass
enhancer instead.)

| Path | Contents |
|---|---|
| `hello_disting_mk4/` | MPLAB X project: the framework plus the Sub voice algorithm. Its README covers parameters, Z calibration, building and installing. |
| `tools/hex2disting4bin.py` | Converts the built `.hex` to a `disting4.bin` for bootloader b2 (SD card install), or validates an existing one. |
| `tools/gen_subbass_tables.py` | Regenerates the DSP lookup tables (`subBassTables.h`). |
| `tools/subbass_host_test.c` | Runs and checks the DSP on a PC. |

The framework is (c) Expert Sleepers Ltd, released under the MIT License (see `LICENSE`).
