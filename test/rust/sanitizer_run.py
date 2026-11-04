"""Local Rust ASan owner runner (experimental compiler instrumentation).

Uses RUSTC_BOOTSTRAP=1 only in child commands, not production builds or global
configuration. Rust standard library/native name-search objects are not rebuilt
with instrumentation. Leak detection is unavailable on this macOS run.
Run: python3 relational-engine/test/rust/sanitizer_run.py.
"""
from pathlib import Path
import os
import subprocess
import tempfile

SUITE = Path(__file__).resolve().parent
ROOT = SUITE.parents[2]
OWNERS = ("typed_chunk_owner", "typed_pool_owner", "handle_owner",
          "row_chunk_owner", "row_pool_owner", "row_handle_owner", "ffi_row_pool_owner",
          "mapped_file_owner", "mapping_error_owner", "preallocated_file_owner",
          "preallocation_owner", "preallocation_error_owner", "preallocated_failure_owner")


def main():
    version = subprocess.run(["rustc", "-vV"], capture_output=True, text=True, check=True, timeout=30)
    host = next(line.split(": ", 1)[1] for line in version.stdout.splitlines() if line.startswith("host: "))
    if host != "aarch64-apple-darwin":
        print("SKIP: Rust ASan owner runner currently proven only on Apple Silicon macOS")
        return 77
    runtime = subprocess.run(["clang", "-print-file-name=libclang_rt.asan_osx_dynamic.dylib"],
                             capture_output=True, text=True, check=True, timeout=30).stdout.strip()
    if not Path(runtime).is_file():
        print("SKIP: installed Clang ASan runtime unavailable")
        return 77
    symbols = subprocess.run(["nm", "-g", runtime], capture_output=True,
                             text=True, check=True, timeout=30).stdout
    if "___asan_version_mismatch_check_v8" not in symbols:
        print("SKIP: Rust instrumentation requires ASan v8; installed Apple runtime lacks its version symbol")
        return 77
    with tempfile.TemporaryDirectory(prefix="re-rust-asan-", dir=os.environ.get("TMPDIR")) as tmp:
        env = dict(os.environ, CARGO_TARGET_DIR=tmp, RUSTC_BOOTSTRAP="1",
                   RUSTFLAGS=f"-D warnings -Zsanitizer=address -Zexternal-clangrt -Clinker=clang -Clink-arg={runtime} -Clink-arg=-Wl,-rpath,{Path(runtime).parent} -Cforce-frame-pointers=yes",
                   MACOSX_DEPLOYMENT_TARGET="14.0",
                   ASAN_OPTIONS="detect_leaks=0")
        for release in (False, True):
            args = ["cargo", "test", "--offline", "--locked", "--target", host,
                    "--manifest-path", str(SUITE / "Cargo.toml")]
            if release:
                args.append("--release")
            for owner in OWNERS:
                args.extend(["--test", owner])
            subprocess.run(args, cwd=ROOT, env=env, check=True, timeout=180)
    print("PASS: Rust typed/row storage ASan debug/release; experimental local flags, std/native C/leak gaps stated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
