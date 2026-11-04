"""Shared owner check for scaffold metadata, not imported engine behavior."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2] / "host-behavior" / "re"
SUITE = Path(__file__).resolve().parent


def run(*args, **kwargs):
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          capture_output=True, timeout=90, **kwargs)


def main():
    parts = ("nio", "io", "reflection", "relational")
    for part in parts:
        assert (ROOT / "src" / part).is_dir()
        assert not (ROOT / part).exists()
    sources = sorted([*(ROOT / "src").rglob("*.c"), *(ROOT / "src").rglob("*.m")])
    assert sources
    ignored = ["rust/target/generated.o", "build/generated.o", "cmake-build-debug/CMakeCache.txt",
               ".idea/workspace.xml", "generated.dll", "generated.dSYM/Contents/data"]
    tracked = ["src/io/file.c", "rust/src/lib.rs", "rust/Cargo.toml", "rust/Cargo.lock",
               "rust/include/relational_memory.h"]
    result = run("git", "check-ignore", "--no-index", "--stdin",
                 input="\n".join(ignored + tracked) + "\n")
    assert set(result.stdout.splitlines()) == set(ignored)
    assert not (ROOT / "CMakeLists.txt").exists()
    assert "learning backend" in (ROOT / "README.md").read_text()
    assert "not a port" in (ROOT / "rust/README.md").read_text()
    for doc in (ROOT / "README.md", ROOT / "rust/README.md", SUITE / "README.md"):
        assert "re/test/rust/run.py" in doc.read_text()
        assert "rust/test/run.py" not in doc.read_text()
    assert "[[test]]" not in (ROOT / "rust/Cargo.toml").read_text()
    assert 'name = "memory_owner"' in (SUITE / "rust/Cargo.toml").read_text()
    for module, unit in (("nio", "mem"), ("primitives", "string"), ("ffi", "memory")):
        assert (ROOT / "rust/src" / module / "mod.rs").is_file()
        assert (ROOT / "rust/src" / module / f"{unit}.rs").is_file()
        assert (SUITE / "rust" / module / f"{unit}_test.rs").is_file()
        assert f'{module}/{unit}_test.rs' in (SUITE / "rust/Cargo.toml").read_text()
        assert f'src/{module}/{unit}.rs' in (ROOT / "rust/README.md").read_text()
    for old in ("mem.rs", "string.rs", "ffi.rs"):
        assert not (ROOT / "rust/src" / old).exists()
    assert "https://gist.github.com/vex-graph/" in (ROOT / "CONTRIBUTING.md").read_text()
    assert "re/test" in (ROOT / "CONTRIBUTING.md").read_text()
    assert "re-preferences.md" in (ROOT / "CONTRIBUTING.md").read_text()
    assert (ROOT / "src/LICENSE").read_text().startswith("Boost Software License")
    assert (ROOT / "LICENSE").read_text().startswith("MIT License")
    workspace = ROOT.parents[1]
    assert "EXCLUDE_FROM_ALL" in (workspace / "CMakeLists.txt").read_text()
    scratch = os.environ.get("TMPDIR")
    with tempfile.TemporaryDirectory(prefix="relational-scaffold-", dir=scratch) as tmp:
        tmp = Path(tmp)
        run("cmake", "-S", str(workspace), "-B", str(tmp / "ide"), "-DBUILD_TESTING=OFF")
        commands = json.loads((tmp / "ide/compile_commands.json").read_text())
        commands = [entry for entry in commands if Path(entry["file"]).is_relative_to(ROOT)]
        assert {Path(entry["file"]).resolve() for entry in commands} == set(sources)
        for entry in commands:
            command = entry["command"]
            assert all(flag in command for flag in ("-Wall", "-Wextra", "-Werror"))
            if Path(entry["file"]).suffix == ".c":
                assert "-std=gnu" in command
        env = dict(os.environ, CARGO_TARGET_DIR=str(tmp / "cargo"), RUSTFLAGS="-D warnings")
        run("cargo", "check", "--offline", "--locked", "--manifest-path", "rust/Cargo.toml", env=env)
    print(f"PASS: {len(sources)} C code-model entries, mixed ignores, local layout and Rust scaffold check")


if __name__ == "__main__":
    main()
