#!/usr/bin/env python3
"""
Build the legacy PIC32 peripheral library (plib) for the disting mk4's
PIC32MX170F256D from the sources that ship with XC32, and put it where the
MPLAB X project links it from:

    hello_disting_mk4/plib/libmchp_peripheral_32MX170F256D.a

Why: XC32 1.x shipped this library prebuilt. Newer XC32 releases still ship
the plib headers and the library *source* (pic32-libs/peripheral), but not
the built library, so the link fails with "undefined reference to
UARTConfigure" and similar. Run this once per machine (and again after
changing XC32 version):

    python3 tools/build_plib.py

It finds the newest XC32 under /Applications/microchip/xc32 (macOS),
/opt/microchip/xc32 (Linux) or C:\\Program Files\\Microchip\\xc32 (Windows),
and the newest PIC32MX device pack under MPLAB X's packs folder. Override
with --xc32 and --dfp if needed.

Every peripheral source file is compiled; files that do not apply to this
chip and fail to compile are skipped (that is expected). At the end it checks
that every plib function the firmware uses is in the library.
"""
import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys

DEVICE = "32MX170F256D"
LIBNAME = "libmchp_peripheral_%s.a" % DEVICE
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT_DIR = os.path.join(REPO, "hello_disting_mk4", "plib")

# plib functions the firmware calls (from the link errors with XC32 v6)
REQUIRED = """
PORTSetPinsDigitalIn PORTSetPinsDigitalOut
UARTEnable UARTConfigure UARTSetDataRate
INTDisableInterrupts INTRestoreInterrupts INTEnableSystemMultiVectoredInt
INTSetVectorPriority INTSetVectorSubPriority INTClearFlag INTEnable
OSCREFConfig SpiChnOpenEx SpiChnPutC
I2CRepeatStart I2CGetStatus I2CBusIsIdle I2CStart I2CTransmitterIsReady I2CSendByte
I2CTransmissionHasCompleted I2CStop I2CByteWasAcknowledged I2CConfigure
I2CSetFrequency I2CEnable
NVMErasePage NVMWriteWord
""".split()

XC32_ROOTS = ["/Applications/microchip/xc32", "/opt/microchip/xc32",
              r"C:\Program Files\Microchip\xc32", r"C:\Program Files (x86)\Microchip\xc32"]
MPLABX_ROOTS = ["/Applications/microchip/mplabx", "/opt/microchip/mplabx",
                r"C:\Program Files\Microchip\MPLABX", r"C:\Program Files (x86)\Microchip\MPLABX"]


def version_key(path):
    name = os.path.basename(path.rstrip("/\\")).lstrip("v")
    return [int(p) if p.isdigit() else 0 for p in name.replace("-", ".").split(".")]


def newest(paths):
    paths = [p for p in paths if os.path.isdir(p)]
    return max(paths, key=version_key) if paths else None


def find_xc32():
    cands = []
    for root in XC32_ROOTS:
        cands += glob.glob(os.path.join(root, "v*"))
    return newest(cands)


def find_dfp():
    cands = []
    for root in MPLABX_ROOTS:
        cands += glob.glob(os.path.join(root, "v*", "packs", "Microchip", "PIC32MX_DFP", "*"))
    home = os.path.expanduser("~")
    cands += glob.glob(os.path.join(home, ".mchp_packs", "Microchip", "PIC32MX_DFP", "*"))
    return newest(cands)


def tool(xc32, name):
    for cand in (os.path.join(xc32, "bin", name), os.path.join(xc32, "bin", name + ".exe")):
        if os.path.isfile(cand):
            return cand
    sys.exit("can't find %s in %s/bin" % (name, xc32))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--xc32", help="XC32 install folder, e.g. /Applications/microchip/xc32/v6.00")
    ap.add_argument("--dfp", help="PIC32MX_DFP folder, e.g. .../packs/Microchip/PIC32MX_DFP/1.7.380")
    ap.add_argument("--keep", action="store_true", help="keep object files and the compile log")
    ap.add_argument("-v", "--verbose", action="store_true", help="show why each skipped file failed")
    a = ap.parse_args()

    xc32 = a.xc32 or find_xc32()
    if not xc32:
        sys.exit("XC32 not found; pass --xc32 <install folder>")
    dfp = a.dfp or find_dfp()
    if not dfp:
        sys.exit("PIC32MX device pack not found; pass --dfp <PIC32MX_DFP/version folder>")
    src_root = os.path.join(xc32, "pic32-libs", "peripheral")
    if not os.path.isdir(src_root):
        sys.exit("no peripheral library source at %s" % src_root)
    gcc, ar, nm = tool(xc32, "xc32-gcc"), tool(xc32, "xc32-ar"), tool(xc32, "xc32-nm")
    libs_include = os.path.join(xc32, "pic32-libs", "include")

    print("XC32:        %s" % xc32)
    print("device pack: %s" % dfp)

    sources = sorted(glob.glob(os.path.join(src_root, "**", "*.c"), recursive=True) +
                     glob.glob(os.path.join(src_root, "**", "*.S"), recursive=True))
    print("sources:     %d files under %s" % (len(sources), src_root))

    build = os.path.join(OUT_DIR, "obj")
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(build)

    def compile_one(i_src):
        i, src = i_src
        obj = os.path.join(build, "%04d_%s.o" % (i, os.path.splitext(os.path.basename(src))[0]))
        cmd = [gcc, "-mprocessor=" + DEVICE, "-mdfp=" + dfp, "-c", "-O1",
               "-fgnu89-inline", "-fno-common", "-w",
               "-D_SUPPRESS_PLIB_WARNING", "-D_DISABLE_OPENADC10_CONFIGPORT_WARNING",
               "-I", os.path.dirname(src), "-idirafter", libs_include,
               src, "-o", obj]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        return src, obj if r.returncode == 0 else None, r.stdout

    ok, failed, log = [], [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for src, obj, out in ex.map(compile_one, enumerate(sources)):
            rel = os.path.relpath(src, src_root)
            if obj:
                ok.append(obj)
            else:
                failed.append(rel)
                log.append("==== %s\n%s" % (rel, out))
    print("compiled:    %d ok, %d skipped (not for this chip, or needs other headers)" % (len(ok), len(failed)))
    if a.verbose:
        print("\n".join(log))
    with open(os.path.join(OUT_DIR, "build_plib.log"), "w") as f:
        f.write("\n".join(log))
    if not ok:
        sys.exit("nothing compiled; see %s" % os.path.join(OUT_DIR, "build_plib.log"))

    lib = os.path.join(OUT_DIR, LIBNAME)
    if os.path.exists(lib):
        os.remove(lib)
    # add in batches to stay under command-line length limits
    for i in range(0, len(ok), 200):
        subprocess.run([ar, "rcs", lib] + ok[i:i + 200], check=True)

    r = subprocess.run([nm, "--defined-only", lib], stdout=subprocess.PIPE, universal_newlines=True, check=True)
    defined = {}
    member = None
    for line in r.stdout.splitlines():
        if line.endswith(":") and line.strip():
            member = line[:-1]
            continue
        parts = line.split()
        if len(parts) == 3 and parts[1] in "TtDdBbRrSsGg":
            defined.setdefault(parts[2], set()).add(member)
    missing = [s for s in REQUIRED if s not in defined]
    dupes = [s for s in REQUIRED if s in defined and len(defined[s]) > 1]

    if not a.keep:
        shutil.rmtree(build, ignore_errors=True)
    print("library:     %s" % lib)
    if dupes:
        print("note: defined in more than one object (the linker uses the first): %s" % ", ".join(dupes))
    if missing:
        print("MISSING from the library: %s" % ", ".join(missing))
        print("Send this output and %s to get these added." % os.path.join(OUT_DIR, "build_plib.log"))
        sys.exit(1)
    print("OK: all %d plib functions the firmware uses are in the library." % len(REQUIRED))


if __name__ == "__main__":
    main()
