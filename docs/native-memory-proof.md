# Native memory: exhaustion and adversarial proof

This assessment covers `nio/mem.{h,c}`, `nio/pool.c`, `nio/arena.c`, and
`nio/transient.c`, not Rust pools, mappings, or disk preallocation. The
implementation is a 3-tier allocator: a growable OS page pool (`MemoryPool` in
`pool.c`) feeding lightweight logical arenas (`MemoryArena` in `arena.c`) that
hand out 32-byte-aligned `any` payloads (`mem.c`). It is not a generational
handle allocator or a complete production-readiness certificate.

## Contract and repaired defects

- **Tier 1 (`MemoryPool`, `nio/pool.c`)**: Maps 64 MiB anonymous OS
  super-chunks on demand (`mmap` on POSIX, `VirtualAlloc` on Windows) and grows
  until OS OOM with no hardcoded limit. Hands out architecture-native pages
  (`16 KiB` on Apple Silicon, `4 KiB` elsewhere) and contiguous multi-page
  spans. When freed pages return, the pool clears page metadata, coalesces
  adjacent free runs in the chunk bitmap, and advises the OS (`madvise`
  `MADV_FREE` / `MADV_DONTNEED`, or `MEM_RESET`) to reclaim physical RAM. A
  sorted chunk directory supports O(log chunks) address-to-page lookup even
  with hundreds of thousands of live arenas.
- **Tier 2 (`MemoryArena`, `nio/arena.c`)**: Starts at zero pages and pulls
  pages from `MemoryPool` on demand (`maxPages == 0` grows until OS OOM;
  `maxPages > 0` enforces a hard per-arena page budget). Exhausting a budgeted
  arena returns `nullptr` without heap allocation or mutating existing blocks.
  Physical slab slots (`64 B..4096 B`) recycle individually in O(1); when a
  slab page empties, it returns immediately to `MemoryPool`. Large allocations
  (`> 4096 B`) claim contiguous multi-page spans and return them directly to
  `MemoryPool` on free.
- **Tier 3 (`any` VarHandles, `nio/mem.c`)**: Every payload carries a frozen
  32-byte `MemoryHeader` (`[typeId][hash][length][pointer]`) and 32-byte
  payload alignment. Enumeration (`MemoryArena_findAll`) includes both live
  slab and span blocks, returns the total count, and writes only up to
  `maxCount`.
- **Reset (`MemoryArena_freeAll`)**: Unlinks all slab and span pages and
  returns every page to `MemoryPool` (clearing page metadata and advising OS
  reclamation). Reset work is proportional to owned pages, not constant time.
- **Realloc**: Invalid-source realloc rejects before constructing a
  replacement. Successful grow/shrink copies the shorter length; failed
  replacement preserves the source intact.

Allocation/free of disjoint blocks may contend after single-threaded admission.
Init, reset, destroy, enumeration, and shared-block mutation require caller
exclusion. Destroy returns all owned pages to the pool and frees the arena
descriptor; it does not preserve live borrows. Freed pointers have no
generations: address reuse can alias a new block. Header hash checks detect
ordinary corruption, not hostile deliberate checksum forgery.

## Executable scenarios

`test/re_test.c` runs `suite_mem` (public-seam regression),
`suite_transient_lifetime` (scratch lifetime and borrow-check death test), and
`suite_mem_exhaustion`:

| Factor | Implemented test |
| :--- | :--- |
| Construction and scaling | Fault-inject `MemoryArena_pages` allocation failure; verify 1-page arenas, unbounded arenas (`MemoryArena()` / `maxPages == 0`), and 256 concurrent arenas sharing pool chunks with full page return on destroy |
| Capacity | Exhaust a 1-page arena budget with zero heap calls; verify repeated rejection, exhaustion counter, single-slot free/reuse, and full page return on `MemoryArena_freeAll` |
| Span coalescing | Allocate three multi-page spans in an 8-page arena, free two, verify adjacent pool page coalescing into a larger span with zero heap calls, then free all to 0 used pages |
| Boundaries and ownership | Exact 1-page and 2-page budget boundaries, failed realloc preserving source bytes, true A-to-B realloc transfer, and wrong-owner free preservation |
| Slab classes | Exhaust every one of the 7 slab classes (`64 B..4096 B`) independently in a 1-page arena, verify rejection, single-slot reuse, and full page return |
| Hostile admission | `SIZE_MAX` / `UINT32_MAX` limits, misaligned interior pointers, and separate hash/type/length header tampering |
| Size histories | 10,000 seeded operations, 64 live-model slots, and 27 payload sizes across every slab class transition, zero, and multi-page spans |
| Legal contention | Four synchronized workers, 4,000 disjoint allocation/free operations each, post-join counts, zero active bytes, and zero used pages |

The native runner substitutes libc calls only when compiling `re_test`'s
`mem.o`; production source has no test hooks. All registered native
subprocesses run with external watchdogs (`timeout=15`).

```sh
python3 -B test/native_run.py
python3 -B test/native_run.py --owner re_test --suite mem_exhaustion --mode thread
```

The main runner covers optimized strict, release-like (no `DEBUG_BORROW_CHECK`),
and ASan/UBSan builds with assertions enabled. Exact commands/results/hashes
live in `../../../metadata/gists/test-checklist.md`; a runner's presence is not
execution evidence.

## Not yet proved

The Deliberate Exhaustion and Backend Trust Law requires thousands of
simultaneously live allocations across workers where legal. The current
contention case performs 16,000 operations but retains at most four
worker-owned blocks at once; its exhaustion cases are single-threaded. Neither
proves the combined concurrent-occupancy requirement. Multiple seeded
schedules, explicit live-address non-overlap under concurrent occupancy, and
overlapping pressure through exhaustion/recovery remain missing. Existing
scoped passes are not retroactively promoted by this law.

Windows/macOS 14 runtime, all legal schedules, long-running downstream loads,
host teardown exclusion, first-use concurrency, adversarial checksum forgery,
every metadata-corruption traversal, OS allocation pressure, generation-safe
raw pointer reuse, and full performance/latency distributions remain unproved.
macOS ASan is not leak detection; construction-failure accounting is not an OS
leak certificate. No timing threshold or benchmark is used as a correctness
oracle. This is scoped passing evidence, not blanket battle-tested readiness.
