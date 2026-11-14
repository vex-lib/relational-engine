# Relational engine owner tests

Tests for `vex/re`, maintained beside production source. Run from the engine
root:

```sh
python3 -B test/native_run.py
```

The Rust owner suite, the C ABI clients and the mapping/preallocation proof
moved to `rangoon` (`rust/`). Run `python3 -B rust/test/run.py` from the
workspace root, or `python3 -B test/run.py` from the rangoon crate root. See
`../../../rust/README.md` and `../../../rust/rangoon-preferences.md`.

`native_run.py` compiles the migrated native C implementation, never Vexspoke
IO/NIO copies, with CPU-only spin, crypto and destructor dispatch from Vexspoke.
Eleven native owners execute with strict optimized C23 and ASan/UBSan,
assertions active and bounded watchdogs. The clipboard mutation owner is built
but SKIP without explicit permission; HotFileSys lifecycle calls prove only its
existing draft no-op. New process/WS owners cover real child exhaustion/reap/
reuse/cancel and fixed frame boundaries.

`native_memory_doc_test.py` checks the native memory proof document against the
runner. `scaffold_test.py`, `preferences_test.py` and `primer_test.py` are
pre-existing stale debt: they resolve `parents[2]` to the wrong root and read the
retired `rust/src` layout, so they do not run green and are not part of the
native baseline. They reference Rust files that now live in `rangoon`.

Needs the sibling workspace `vex/re` checkout, clang and CMake. Current evidence
is macOS only. No interactive visual tests.
