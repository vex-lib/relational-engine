"""Strict migrated native IO/NIO owners, with assertions and ASan/UBSan.

The engine owns every storage/I/O object; only SpinLock and Crypto computation
come from Vexspoke. No Vexspoke io/nio source or header exists in this build.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

SUITE = Path(__file__).resolve().parent
WORKSPACE = SUITE.parents[1]
ENGINE = WORKSPACE / "suite/ecosystem/relational-engine"
CPU = WORKSPACE / "suite/ecosystem/vexspoke"

assert not (CPU / "src/nio/mem.c").exists()
assert not list((CPU / "src/io").glob("*"))
sources = [ENGINE / "src/nio/mem.c", *sorted((ENGINE / "src/io").glob("*.c")),
           ENGINE / "src/io/clipboard_mac.m", CPU / "src/atomic/spin.c",
           CPU / "src/security/crypto.c", CPU / "src/c23/free.c"]
owners = [*sorted((SUITE / "io").glob("*_test.c")),
          SUITE / "nio/mem_test.c", SUITE / "nio/mem_exhaustion_test.c",
          SUITE / "nio/transient_lifetime_test.c"]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--owner", choices=[p.stem for p in owners])
parser.add_argument("--mode", choices=["strict", "release", "sanitized", "thread"])
args = parser.parse_args()
if args.owner:
    owners = [p for p in owners if p.stem == args.owner]
modes = [("strict", []), ("release", []), ("sanitized", ["-fsanitize=address,undefined"])]
if args.mode == "thread":
    modes = [("thread", ["-fsanitize=thread"])]
elif args.mode:
    modes = [mode for mode in modes if mode[0] == args.mode]
with tempfile.TemporaryDirectory(prefix="re-native-", dir=os.environ.get("TMPDIR")) as tmp:
    tmp = Path(tmp)
    for mode, sanitizer in modes:
        flags = ["-std=gnu23", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
                 "-mcpu=apple-m1", "-mmacosx-version-min=14.0",
                 "-I" + str(CPU / "src"), "-I" + str(ENGINE / "src"),
                  "-I" + str(WORKSPACE / "tests"), *sanitizer]
        if mode != "release":
            flags.append("-DDEBUG_BORROW_CHECK")
        objects = []
        for index, source in enumerate(sources):
            obj = tmp / f"{mode}-{index}.o"
            arc = ["-fobjc-arc"] if source.suffix == ".m" else []
            subprocess.run(["clang", *flags, *arc, "-c", str(source), "-o", str(obj)],
                           check=True, timeout=60)
            objects.append(str(obj))
        for owner in owners:
            binary = tmp / f"{mode}-{owner.stem}"
            owner_objects = objects
            owner_flags = []
            if owner.stem == "mem_exhaustion_test":
                # Compile-time allocator substitution only in this test's mem.o:
                # no production test hooks, and other owners retain real libc.
                injected = tmp / f"{mode}-injected-mem.o"
                subprocess.run(["clang", *flags, "-Dmalloc=MemTest_malloc",
                                "-Dcalloc=MemTest_calloc", "-Drealloc=MemTest_realloc",
                                "-c", str(sources[0]), "-o", str(injected)],
                               check=True, timeout=60)
                owner_objects = [str(injected), *objects[1:]]
                owner_flags = ["-DMEM_TEST_FAULTS=1"]
            subprocess.run(["clang", *flags, *owner_flags, str(owner), *owner_objects, "-framework", "AppKit", "-o", str(binary)],
                           check=True, timeout=60)
            result = subprocess.run([str(binary)], timeout=15)
            if result.returncode == 77:
                assert owner.name == "clipboard_test.c", owner
                print(f"SKIP: {mode} {owner.relative_to(SUITE)} (system clipboard)", flush=True)
                continue
            result.check_returncode()
            print(f"PASS: {mode} {owner.relative_to(SUITE)}", flush=True)
print("Completed selected native owners; see individual PASS/SKIP results; macOS only")
