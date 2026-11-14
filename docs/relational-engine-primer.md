# Relational Engine — the C boundary, plainly

This is a reading document, not a spec. It explains what `re` owns after the Rust
move, how the native allocator and its C ABI fit together, and where the Rust half
now lives. It is written so you can follow the C side without reading every file,
and so you can disagree with the architecture in an informed way.

If something here contradicts the source, the source wins — this document is a
map, not the territory. The Rust half has its own primer:
`../../../rust/docs/rangoon-primer.md`.

## Current State

`re` is the **R0 memory and storage owner**: the C allocator, native IO/NIO, the
stable C ABI, the type algebra, the base spinlock and the thread registry. It is a
partial backend, not a finished engine.

Implemented and proven on macOS arm64: `Memory`/`MemoryArena`/`Transient` with the
32-byte header and 32-byte payload floor; `File`/`Log` and the macOS clipboard
adapter; the 64-bit type algebra and parent-chain resolver; the spinlock; the
thread registry. Windows is unproven. `substrate/` is the older C relational
surface and the imported comparison material; it is not a second production path.
Rust storage, its C ABI, the mappings and the name-search primitive live in
`rangoon` (`../../../rust/`), and `re` keeps no second Rust implementation.

## 1. The one-sentence version

`re` is the ecosystem's **memory and storage floor**: it allocates bytes, stamps
them so a pointer names its own type and length, owns the file/log/clipboard
seams, and publishes the type algebra every other tier shares.

It does not own windows, GPUs, UI, or the Rust storage surface. It owns:
allocation, native files, the stable C ABI, the shared type-id algebra, the base
lock and the process thread table.

## 2. What lives here

| Directory | Owns |
| :--- | :--- |
| `nio/` | `Memory`, `MemoryArena`, `Transient` — the allocator implementation |
| `io/` | `File`, `FileEvent`, `FileWriter`, `HotFile`, `Log`, `LogParser` |
| `clipboard/` | the macOS clipboard adapter and its stub |
| `type/` | the 64-bit type-id algebra and this project's class registry |
| `atomic/` | `SpinLock`, one atomic word, acquire/release |
| `thread/` | the fixed-capacity thread index and opaque tag table |
| `substrate/` | the older C relational surface: `Cell`, `Shelf`, `SymbolTable`, variable maps |
| `test/` | the native owner tests |

`re.h` is the repository door; `nio/nio.h`, `io/io.h`, `substrate/substrate.h`
and the other directory headers follow the Directory Header Law.

## 3. The 32-byte block header

Every `Memory_*` payload sits behind four 64-bit words:

```
[ typeId ][ hash ][ length ][ pointer ]   then the payload bytes
```

- `typeId` is the triple-sugar self-describe word: form, project, modifier,
  wrapper one, wrapper two, the **BE6C** sugar and the class number. A pointer
  therefore names its own project and class without a registry lookup.
- `hash` is an FNV-1a veto over `(typeId, length, pointer)`, with the low bit
  forced so a zeroed header always fails.
- `length` is the payload byte count.
- `pointer` is the redirect the block carries: the object's own address, or the
  address of its value when the id's form says the type is a primitive.

The payload pointer is 32 bytes after the header, so negative pointer arithmetic
recovers the header in one subtraction. `Memory_type` and `Memory_length` are
O(1) reads with no locking. The header is frozen at four words: no growth room
lives in-band.

Two independent checks fail closed: the BE6C sugar inside the id, and the hash
over the stated fields. Provenance beyond that is the arena-range gate, never a
magic cookie. Blocks are **immovable** — `pointer` records an address, so a live
block is never relocated.

Payloads are 32-byte aligned on every supported platform (the Universal Alignment
Law). The engine builds that floor itself by over-allocating one unit and rounding
the base forward; it does not take alignment from `aligned_alloc` or a platform
attribute.

The header's `hash` is a **veto, not an identity**. It is a pure function of
`(typeId, length, pointer)`, so a block reused at the same address with the same
type and length produces the same hash. It catches corruption; it cannot tell an
incarnation from its successor. Reuse detection is the generation, and the
generation lives with the slot that issued the handle — the header is per block,
and the generation is per slot.

## 4. Three arenas, three lifetimes

- **Default (master) arena** — process-lifetime objects. `Memory_init` carves it
  once; `Memory_alloc` / `Memory_free` are the hot path; `Memory_freeAll` resets
  the whole arena last.
- **Secondary arenas** — isolated ownership domains. `MemoryArena_create` and
  `MemoryArena_destroy` bracket them; a block is freed back to its own arena.
- **Transient arena** — frame and scratch memory. `Transient_alloc` is the bump
  path; `Transient_reset` resets at the frame boundary; `Transient_contains`
  answers which lifetime a pointer belongs to.

Slab blocks recycle individually in O(1). Bump blocks invalidate on free but
reclaim capacity only on `freeAll`. A refused allocation returns `nullptr` and
reports once per epoch; the count is queryable through
`Memory_exhaustionCount`, `MemoryArena_exhaustionCount` and
`Transient_exhaustionCount`. There is no untracked heap fallback.

## 5. The allocator is the only libc seam

`nio/mem.c` is the single place libc allocation appears. `nio/arena.c` and
`nio/transient.c` own the arena and scratch mechanics and call the private
`Memory_systemAlloc` / `Memory_systemCalloc` / `Memory_systemRealloc` /
`Memory_systemFree` wrappers declared in `nio/mem.h`.

Every other repository allocates through `Memory_alloc`, `MemoryArena_alloc` or
`Transient_alloc`. `malloc`, `calloc`, `realloc`, `free`, `strdup` and friends
outside the owner are defects, and `tools/python/alloc_gate.py` is the executable
audit.

## 6. Native IO

- `io/file/` — `File`, its event surface, `FileWriter`, and `HotFile`. HotFile is
  a draft no-op: its lifecycle calls prove the shape, not live reload.
- `io/log/` — `Log` and `LogParser`.
- `clipboard/` — a macOS adapter plus a stub for targets without one.

These are the production native C file seams. Buffered readers/writers and
FFF-style gathering/indexing are planned work in `io/`, not implemented durable
snapshots. Manifest-backed persistence remains a discussion.

## 7. The type algebra

A 64-bit id encodes *whose* class it is (project byte), its *shape* (form), its
*modifier* and wrappers, and its *class number* (per project). The layout is:

```
0xF PRPR M W1 W2 BE6C CCCCCC
```

`Type_make`, `Type_class`, `Type_arch` and the masks are project-agnostic. The
parent-chain resolver (`Type_registerParents`, `Type_getParentClass`,
`Type_isA`) is registered once per project byte and walks a bounded chain, so a
cyclic table can never be registered. Vexspoke's legacy bare ids go through
`Type_registerBareParents`.

Each repository keeps its own class registry. `type/re_type.h` carries the shared
algebra plus `re`'s own registry; it must never include another project's registry.

## 8. The base lock and the thread table

`atomic/spin_lock.c` is one atomic word: `0` is free, otherwise the owner-encoded
`(threadId << 1) | 1`. The successful CAS is acquire; unlock is release. It can be
embedded inside other structs with no separate allocation.

`thread/thread_registry.c` gives every thread a dense index in
`[0, THREAD_REGISTRY_DEFAULT_SIZE)` and one opaque `u32` tag. The table is
fixed-capacity by design, never allocates, and slot indices never invalidate.
What a tag *means* is policy and belongs to the behavior owner (Vexspoke's
`ThreadRole`), not to `re`.

## 9. The older C relational surface

`substrate/` carries `Cell`, `Shelf`, `SymbolTable` and the variable maps. It is
the C predecessor of the name-to-value storage that now lives in `rangoon`'s
`VariableRegistry`. The README calls the imported comparison material
non-production; either way, new name-to-value storage should reach for the Rust
owner, not add a second production path here.

## 10. How C talks to the Rust half

The Rust half ships its own C ABI under `rangoon/include/relational_engine/`.
`re` does not include or link it today; a consumer that wants Rust storage
includes that door and links `librangoon.a`.

The boundary rules are simple:

1. `extern "C"` plus `#[unsafe(no_mangle)]` gives a fixed C name and calling
   convention.
2. `#[repr(C)]` fixes a shared struct's layout. Scalars and opaque pointers
   cross; Rust `String`, `Vec` and data-carrying enums do not.
3. The Rust side owns its data. C holds an opaque `*mut Owner` it never looks
   inside.
4. Fallible operations return a small integer status and write results through
   caller-supplied outputs. Rust panics must never unwind into C.
5. Arbitrary pointers are not validated. A non-null pointer must be live and
   aligned; the engine trusts the pointer exactly as C does.

`re` remains the C owner of the allocator and the native seams. `rangoon` is the
Rust owner of typed storage and mappings. Neither re-implements the other.

## 11. Why the C half stays C

1. **The ABI is already C and it is wide.** Every tier links `Memory_*`, `File`,
   the header and the type ids. A Rust rewrite would invalidate the whole
   ecosystem at once, in the wrong dependency order.
2. **No runtime on the floor.** R1 and the R3 drivers stay lean; a general-purpose
   allocator, panic machinery or runtime is not something a host or driver should
   inherit.
3. **The hot path wants an arena.** Zero steady-state allocation and bounded
   waits are easier to prove with explicit arena lifetimes than with a general
   allocator.
4. **Progressive proof.** Each migration step is proved in both languages: a real
   C client compiles against the engine-owned header and runs the Rust-owned
   behavior.

## 12. What is real vs planned

**Real:** the native allocator and its exhaustion behavior, `File`/`Log`, the
clipboard adapter, the type algebra, the spinlock and the thread registry, all on
macOS arm64 with strict C23 and ASan/UBSan owners.

**Draft:** `HotFile` is a no-op lifecycle; buffered readers/writers and
FFF-style indexing are planned; manifest-backed persistence is discussion.

**Not proved:** Windows execution, performance, complete fault/concurrency
coverage, live reload integration, or automatic record-schema migration. Whole-
value replacement is not schema migration.

## 13. Pressure points

Honest places to push, so the architecture can be argued with:

1. **The ABI is manual.** Every crossing function restates its contract in a
   comment. A generated header could reduce declaration drift; ownership and
   lifetime contracts would still need review and C-client tests.
2. **One header, one truth.** The 32-byte block header lives with the allocator;
   the type algebra lives in `type/`. They are related but separate — worth
   deciding whether the header should carry more self-description.
3. **The older substrate.** `substrate/` and rangoon's `VariableRegistry` both
   describe name-to-value storage. Decide whether the C predecessor is retired or
   kept as a comparison surface; two production paths would be the drift the
   Compose Down Law forbids.
4. **Windows is unproven.** The floor is stated for six targets; only macOS arm64
   has execution evidence today.

## 14. Scope and Limitations

**Scope:** R0 memory and storage — allocation, native file/cache/log IO, the
stable C ABI, the shared type-id algebra, the base lock and the thread table. It
is a backend, not a host or a driver.

**Deliberately not covered:**

- No GPU work: shaders, dispatch, capabilities and synchronization are Graphvex.
- No host/supervisor role: R1 `hotcwap` owns lifetimes and residency, and excludes
  active users before destruction.
- No Rust storage: that surface, its C ABI, its mappings and its proof live in
  `rangoon`.
- No database policy: transactions, recovery and durability belong to Darkbase.

**Known limits and gaps:**

- No automatic record-schema migration, and no C/Rust atomic-layout compatibility.
- `substrate/` is the older C relational surface, not a second production path.
- No Windows execution, general zero-copy storage, complete concurrency/fault
  coverage, or performance claim.

## 15. Glossary

- **R0** — the host/lifetime tier: `lifetime` plus this storage substrate.
- **block header** — the 32 bytes before every payload.
- **arena** — a region with one lifetime and one reset point.
- **transient** — the per-frame scratch arena.
- **type algebra** — the shared 64-bit id encoding and its readers.
- **BE6C** — the author's stylized Cyrillic "векс" signature inside every id.
- **seam** — the C/Rust boundary and its rules.

---

*Review boundary: this document was rewritten after the Rust move. It was checked
against `nio/mem.h`, `type/re_type.h`, `thread/thread.h`, `atomic/atomic.h`,
`io/io.h`, `re.h` and the repository README. The Rust side was checked against
`rangoon/include/relational_engine/` and `rangoon/lib.rs`. This is not a complete
native IO, Darkbase, R3–R5 application, cross-platform or oldest-supported-macOS
audit.*
