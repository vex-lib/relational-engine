# Relational Engine owner tests

Tests for `vex/re`, maintained beside production source. Run from the engine
root:

```sh
python3 -B test/native_run.py
python3 -B test/native_memory_doc_test.py
```

`native_run.py` compiles the native C allocator (`nio/mem.c`, `nio/pool.c`,
`nio/arena.c`, `nio/transient.c`), spinlock, IO, and clipboard sources with
strict optimized C23 (`-std=gnu23 -Wall -Wextra -Werror`), release, and
ASan/UBSan builds, assertions active, and bounded watchdogs (`timeout=15`).
The clipboard mutation owner (`re_manual_test.c`) is built but skipped unless
`RE_TEST_SYSTEM_CLIPBOARD=1` is set.

`native_memory_doc_test.py` checks `docs/native-memory-proof.md` and `README.md`
against `nio/pool.c`, `nio/arena.c`, `test/re_test.c`, and `test/native_run.py`.

Needs the sibling workspace `vex/re` checkout and clang. Current evidence is
macOS arm64 only. No interactive visual tests.
