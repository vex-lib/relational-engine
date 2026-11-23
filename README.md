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

- **3-tier relational memory (`re/nio/mem.h`)**:
  - **Tier 1, `MemoryPool` (`nio/pool.c`)**: Growable OS virtual-memory super-chunks (`64 MiB` default, growing until OS OOM) handing out architecture-native pages (`16 KiB` on Apple Silicon, `4 KiB` elsewhere) and reclaiming freed physical pages via `madvise` / `MEM_RESET`.
  - **Tier 2, `MemoryArena` (`nio/arena.c`)**: Lightweight per-library logical owners drawn from `MemoryPool` (`maxPages == 0` for unbounded growth, or a hard page budget), with 7 slab classes (`64 B..4096 B`) and contiguous multi-page spans (`> 4 KiB`) that return empty pages immediately to the pool.
  - **Tier 3, `any` VarHandles (`nio/mem.c`)**: Explicit non-null `Memory_alloc(type, arena, bytes)` with a frozen 32-byte allocation header and 32-byte payload floor (rejects `nullptr`; never falls back to a global arena).
  - `Transient` frame scratch allocator (`nio/transient.c`) for short-lived per-frame or per-pass work.
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

- [`docs/relational-engine-primer.md`](docs/relational-engine-primer.md), Architecture overview and usage guide.
- [`docs/native-memory-proof.md`](docs/native-memory-proof.md), Allocator invariants, slab layout, and proofs.

## Future

- Complete the library-wide migration to explicit non-null `Memory_alloc(type, arena, bytes)` across all downstream callers.
- Expand Linux and Windows native IO parity (`FileEventWatcher` and clipboard backends).
- Add automated stress/fuzz coverage for generational handle wraparound and coalescing edge cases.

## Scope and Limitations

- **macOS arm64 primary**: Sanitizers, `FileEventWatcher`, and clipboard paths are verified on Apple Silicon macOS; non-macOS backends are partial.
- **Explicit arenas required**: Passing a null arena is rejected by design; callers must own and pass a valid `MemoryArena`.
- **Single-process runtime**: Cross-process shared memory and IPC belong to `vexspoke`, not `re`.
