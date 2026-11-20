# Relational Engine (`re`)

The core C23 runtime for Vexgraph: per-library `MemoryArena` allocation, generational handles, native IO, type algebra, atomics, and threading.

Remote: [vex-lib/relational-engine](https://github.com/vex-lib/relational-engine). Headers are imported under the short prefix `re/`:

```c
#include "re/nio/mem.h"
#include "re/type/type.h"
```

## About

`re` is the foundation beneath every Vexgraph library. Instead of scattered `malloc`/`free` calls or a hidden global allocator, each library establishes its own `MemoryArena` so its constructors allocate into a dedicated, bounded region. Callers pass that arena explicitly to `Memory_alloc(type, arena, bytes)` and receive a raw `any` pointer; when stable indirection or use-after-free protection is needed, `re` provides generational handles (index + generation) resolving into arena-backed objects.

## Features

- **Per-library arenas (`re/nio/mem.h`)**:
  - Explicit non-null `Memory_alloc(type, arena, bytes)` returning a raw `any` pointer (rejects `nullptr`; never falls back to a global arena).
  - 32-byte allocation header and 32-byte payload floor.
  - 7 fixed slab classes for allocations `<= 4 KiB`; coalescing free-list with tail retraction for allocations `> 4 KiB`.
  - `Transient` frame scratch allocator for short-lived per-frame or per-pass work.
  - Dedicated generational handle / slot-table API (index + generation resolving to arena-backed storage).
- **Native IO (`re/nio/`)**: `File`, `Log`, `FileEventWatcher`, and macOS clipboard access.
- **Type algebra (`re/type/`)**: Runtime type descriptors, sizing, and structural metadata.
- **Concurrency (`re/atomic/`, `re/thread/`)**: Spinlocks, atomic primitives, and a thread registry.

## Build and test

From the `worktree/` root:

```bash
./tools/b build relational_engine
python3 -B test/native_run.py
```

Run the memory exhaustion suite under ThreadSanitizer:

```bash
python3 -B test/native_run.py --owner re_test --suite mem_exhaustion --mode thread
```

## Documentation

- [`docs/relational-engine-primer.md`](docs/relational-engine-primer.md) — Architecture overview and usage guide.
- [`docs/native-memory-proof.md`](docs/native-memory-proof.md) — Allocator invariants, slab layout, and proofs.

## Future

- Complete the library-wide migration to explicit non-null `Memory_alloc(type, arena, bytes)` across all downstream callers.
- Expand Linux and Windows native IO parity (`FileEventWatcher` and clipboard backends).
- Add automated stress/fuzz coverage for generational handle wraparound and coalescing edge cases.

## Scope and Limitations

- **macOS arm64 primary**: Sanitizers, `FileEventWatcher`, and clipboard paths are verified on Apple Silicon macOS; non-macOS backends are partial.
- **Explicit arenas required**: Passing a null arena is rejected by design; callers must own and pass a valid `MemoryArena`.
- **Single-process runtime**: Cross-process shared memory and IPC belong to `vexspoke`, not `re`.
