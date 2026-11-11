"""Registered Rust owner suite and actual C static-library ABI client."""
from pathlib import Path
import os
import subprocess
import tempfile

SUITE = Path(__file__).resolve().parent
WORKSPACE = SUITE.parents[2]
ROOT = WORKSPACE / "re"
LIB = WORKSPACE.parent / "lib"

def run(*args, env=None):
    subprocess.run(args, cwd=ROOT, env=env, check=True, timeout=90)

with tempfile.TemporaryDirectory(prefix="relational-owner-", dir=os.environ.get("TMPDIR")) as tmp:
    target = Path(tmp)
    env = dict(os.environ, CARGO_TARGET_DIR=tmp, RUSTFLAGS="-D warnings")
    run("cargo", "test", "--offline", "--locked", "--manifest-path", str(SUITE / "Cargo.toml"), env=env)
    run("cargo", "test", "--release", "--offline", "--locked", "--manifest-path", str(SUITE / "Cargo.toml"), env=env)
    run("cargo", "test", "--doc", "--offline", "--locked", "--manifest-path", "rust/Cargo.toml", env=env)
    run("cargo", "build", "--offline", "--locked", "--manifest-path", "rust/Cargo.toml", env=env)
    binary = str(target / "ffi-test")
    c_includes = ("-Irust/include", "-I" + str(ROOT), "-I" + str(WORKSPACE / "vexspoke"),
                  "-I" + str(LIB), "-I" + str(LIB / "lang"))
    run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", *c_includes,
        str(SUITE / "ffi/memory_test.c"), str(target / "debug/librelational_engine_scratchpad.a"),
        "-o", binary)
    run(binary)
    result = subprocess.run([
        "rustc", "--edition=2024", str(SUITE / "nio/borrow_rejection.rs"),
        "--extern", f"relational_engine_scratchpad={target}/debug/librelational_engine_scratchpad.rlib",
        "-L", f"dependency={target}/debug/deps", "-o", str(target / "borrow-rejection")],
        cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert result.returncode != 0 and "E0502" in result.stderr, result.stderr
    run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
        *c_includes, str(SUITE / "ffi/memory_test.c"),
        str(target / "debug/librelational_engine_scratchpad.a"), "-o", binary)
    run(binary)
    print("PASS: debug/release owners, executable docs, C ABI, C ASan/UBSan, E0502 borrow rejection")
    for sanitizers in ([], ["-fsanitize=address,undefined"]):
        run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", *sanitizers,
            "-Irust/include", "-I" + str(ROOT),
            "-I" + str(WORKSPACE / "vexspoke"), "-I" + str(LIB), "-I" + str(LIB / "lang"),
            str(WORKSPACE / "re/test/nio/relational_memory_test.c"),
            str(target / "debug/librelational_engine_scratchpad.a"), "-o", binary)
        run(binary)
    for sanitizers in ([], ["-fsanitize=address,undefined"]):
        run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", *sanitizers,
            "-Irust/include", "-I" + str(ROOT),
            "-I" + str(WORKSPACE / "vexspoke"), "-I" + str(LIB), "-I" + str(LIB / "lang"),
            str(WORKSPACE / "re/test/nio/fixed_mapped_file_abi_test.c"),
            str(target / "debug/librelational_engine_scratchpad.a"), "-o", binary)
        run(binary)
    print("PASS: C client of the fixed-extent mapping ABI (strict + ASan/UBSan)")
    for sanitizers in ([], ["-fsanitize=address,undefined"]):
        run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", *sanitizers,
            "-Irust/include", "-I" + str(ROOT),
            "-I" + str(WORKSPACE / "vexspoke"), "-I" + str(LIB), "-I" + str(LIB / "lang"),
            str(WORKSPACE / "re/test/nio/growable_mapped_file_abi_test.c"),
            str(target / "debug/librelational_engine_scratchpad.a"), "-o", binary)
        run(binary)
    print("PASS: C client of the growable mapping ABI (strict + ASan/UBSan)")

    print("PASS: engine-owned separate atomic byte/string extern handshake")
    for sanitizers in ([], ["-fsanitize=address,undefined"]):
        run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", "-O2", *sanitizers,
            "-I" + str(ROOT), "-I" + str(LIB), str(WORKSPACE / "re/test/search/name_search_test.c"),
            "search/primitives/name_search.c", "-o", binary)
        result = subprocess.run([binary], cwd=ROOT, capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, result.stderr
        lines = result.stderr.splitlines()
        assert len(lines) == 10 and all(line.startswith("[vex] ") and
            line.endswith("name search rejected invalid borrowed span") for line in lines), result.stderr
    print("PASS: native C name-search boundary and exact cold rejection diagnostics")
    for sanitizers in ([], ["-fsanitize=address,undefined"]):
        run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", "-O2", *sanitizers,
            *c_includes, str(SUITE / "ffi/variable_registry_test.c"),
            str(target / "debug/librelational_engine_scratchpad.a"), "-o", binary)
        result = subprocess.run([binary], cwd=ROOT, capture_output=True, text=True, timeout=90)
        assert result.returncode == 0 and result.stderr == "", result.stderr
    print("PASS: actual C variable registry layout, stable pointers, rebinding and teardown")
    for profile in ("debug", "release"):
        run("cargo", "build", *( ["--release"] if profile == "release" else [] ),
            "--offline", "--locked", "--manifest-path", "rust/Cargo.toml", env=env)
        for sanitizers in ([], ["-fsanitize=address,undefined"]):
            run("clang", "-std=c23", "-Wall", "-Wextra", "-Werror", "-O2", *sanitizers,
                *c_includes, str(SUITE / "ffi/row_pool_test.c"),
                str(target / profile / "librelational_engine_scratchpad.a"), "-o", binary)
            result = subprocess.run([binary], cwd=ROOT, capture_output=True, text=True, timeout=90)
            assert result.returncode == 0 and result.stderr == "", result.stderr
    print("PASS: actual C aligned row-pool ABI debug/release + C ASan/UBSan, named-binding borrow/teardown")
    for form, diagnostic in [("chunk_arity", "no rules expected"), ("list_arity", "no rules expected"),
        ("slot_arity", "no rules expected"), ("registry_arity", "no rules expected"),
        ("wrong_capacity", "E0308"), ("chunk_borrow", "E0502"), ("registry_borrow", "E0502"),
        ("typed_chunk_arity", "no rules expected"), ("typed_pool_arity", "no rules expected"),
        ("typed_wrong_capacity", "E0308"), ("typed_chunk_borrow", "E0502"),
        ("typed_pool_borrow", "E0502"), ("typed_pool_release_borrow", "E0502"),
        ("row_pool_arity", "no rules expected"), ("row_chunk_arity", "unexpected end"),
        ("row_handle_arity", "unexpected end"), ("row_wrong_capacity", "E0308"),
        ("row_pool_borrow", "E0502"), ("row_pool_release_borrow", "E0502")]:
        result = subprocess.run(["rustc", "--edition=2024", "--cfg", form,
            str(SUITE / "storage_rejection.rs"), "--extern",
            f"relational_engine_scratchpad={target}/debug/librelational_engine_scratchpad.rlib",
            "-L", f"dependency={target}/debug/deps", "-o", str(target / "storage-negative")],
            cwd=ROOT, capture_output=True, text=True, timeout=90)
        assert result.returncode != 0 and diagnostic in result.stderr, result.stderr
    print("PASS: storage arity/type and exclusive-borrow compile-negative contracts")
    for form, diagnostic in [("unsafe_admission", "E0133"), ("unsafe_explicit", "E0133"),
        ("arity", "no rules expected"), ("wrong_mode", "E0308"), ("wrong_path", "E0277"),
        ("close_borrow", "E0502"), ("write_borrow", "E0502"),
        ("drop_borrow", "E0505"), ("f64_mut", "E0499"),
        ("preallocation_arity", "unexpected end"), ("preallocation_length", "E0308"),
        ("preallocation_extra_arity", "no rules expected"),
        ("preallocation_move", "E0382"), ("preallocation_borrow", "E0502"),
        ("preallocation_unsafe", "E0133")]:
        result = subprocess.run(["rustc", "--edition=2024", "--cfg", form,
            str(SUITE / "nio/fixed_mapped_file_rejection.rs"), "--extern",
            f"relational_engine_scratchpad={target}/debug/librelational_engine_scratchpad.rlib",
            "-L", f"dependency={target}/debug/deps", "-o", str(target / "mapping-negative")],
            cwd=ROOT, capture_output=True, text=True, timeout=90)
        assert result.returncode != 0 and diagnostic in result.stderr, result.stderr
    print("PASS: mapping/preallocation unsafe admission, arity/types, move and close/write/drop borrow rejection")
