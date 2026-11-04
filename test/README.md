# Relational engine owner tests

Tests for `../../vex/re`, maintained in the independent tests
repository rather than production source. Run from the workspace root:

```sh
python3 relational-engine/test/rust/run.py
python3 relational-engine/test/scaffold_test.py
python3 relational-engine/test/native_run.py
```

`rust/Cargo.toml` registers twenty-two independent owner targets:
- `memory_owner`: `rust/nio/mem_test.rs` (ownership, growth, stale IDs, macros).
- `string_owner`: `rust/primitives/string_test.rs` (byte and UTF-8 projections).
- `atomic_string_owner`: `rust/primitives/atomic_string_test.rs` (snapshot lifetime,
  caller budget rejection, and barrier-driven reader/writer publication).
- `ffi_memory_owner`: `rust/ffi/memory_test.rs` (FFI rejection/recovery).
- `chunk_owner`: fixed typed rows, alignment, OOM/retry, drops and projections.
- `chunked_list_owner`: stable addresses across growth, OOM/retry and lifetime.
- `variable_slot_owner`: 32-byte layout, all byte/name boundaries and arities.
- `variable_registry_owner`: actual C search, stable bindings and borrowed values.
- `ffi_variable_registry_owner`: every registry extern and output preservation.
- `typed_chunk_owner`: bitmap edges, over-alignment, ownership, hole reuse and OOM.
- `typed_pool_owner`: lazy growth, stable survivors, reclamation, failure/retry and
   20,000 seeded operations against an independent owned-value model.
- `handle_owner`, `row_handle_owner`: owner-local/owner-tagged generation identity.
- `row_chunk_owner`, `row_pool_owner`, `ffi_row_pool_owner`: actual aligned byte
  rows, failure preservation, reclamation history and C borrowing.
- `mapped_file_owner`, `mapping_error_owner`: fixed existing-file byte views,
  offset/count boundaries, writeback/sync round trips, close/drop, shared readers,
   observable errors and intended unsafe/arity/type/borrow compiler rejections.
- `preallocated_file_owner`: real decimal 1 GB physical reservation, persistent
  bit changes, existing-file preservation, permissions and descriptor transfer.
- `preallocation_owner`: actual syscall/allocated-block seam and readonly failure.
- `preallocation_error_owner`: all error projections and original OS causes.
- `preallocated_failure_owner`: exact owner compiled with test-only backend stage
  failures; deterministic reservation/EOF/query rejection, cleanup and recovery.

These directories mirror `rust/src/{nio,primitives,ffi}` in the engine. The companion
C client is `rust/ffi/memory_test.c`; the compile-negative borrow fixture is
`rust/nio/borrow_rejection.rs`. The runner executes
debug/release tests, the engine doctest, an actual strict C23 static-library
client, C-client ASan/UBSan and expected E0502 borrow rejection. Subprocesses
have 90-second watchdogs; generated output stays in temporary directories.
The runner also builds and runs `relational-engine/test/nio/relational_memory_test.c`
against the real engine, with and without C-client ASan/UBSan. This proves the
Rust extern handshake, not a rewrite of the native allocator or Hotcwap reload integration.

Native owners now mirror engine `src/io` and `src/nio`. `native_run.py` compiles
the migrated implementation, never Vexspoke IO/NIO copies, with CPU-only spin,
crypto and destructor dispatch from Vexspoke. Eleven native owners execute with
strict optimized C23 and ASan/UBSan, assertions active and bounded watchdogs.
The clipboard mutation owner is built but SKIP without explicit permission;
HotFileSys lifecycle calls prove only its existing draft no-op. New process/WS
owners cover real child exhaustion/reap/reuse/cancel and fixed frame boundaries.

`scaffold_test.py` checks C IDE metadata and ignores, documentation markers,
and warnings-denied Cargo discovery. It does not exercise imported C behavior.
`preferences_test.py` checks the R2 responsibility layout and explicit planned
scope. io implements create-new physical reservation; compress/virtual have no runtime behavior to prove. File
indexing, codecs and GPU transfer remain unimplemented. Fixed-extent Rust mmap
does not provide resize, C mapping ABI, hostile truncation protection or transactions.
OS mapping/flush/sync failure injection and huge-file admission remain unproved. Primitive owners also
execute legacy `text` aliases. C `search/primitives/name_search_test.c` executes
span boundaries and exact diagnostic counts under ASan/UBSan. A real C registry
client asserts ABI layout, growth, pointer rebinding and borrowed-value teardown.
`storage_rejection.rs` proves unsupported macro arities/types and exclusive Rust
borrows fail for the intended diagnostic. New fault injection is thread-local,
deterministic, and scoped to isolated owner executables; no timing sleeps.

Needs the sibling workspace `../../vex/re` checkout, Cargo,
rustc, clang and CMake. Current evidence is macOS only. Rust internals are not
sanitizer-instrumented here; OOM injection, concurrency, Vexspoke allocator
equivalence, deterministic writer-busy injection, automatic record schema
migration and Windows remain gaps. No interactive visual tests.
