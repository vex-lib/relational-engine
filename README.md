# relational-engine

R0 memory/storage owner: the C allocator, native IO/NIO, and the stable C ABI.
Rust storage lives in `rangoon` (`rust/`).

**Ecosystem repository:** https://github.com/vex-lib/relational-engine
The local checkout is `vex/re`; its physical location does not change its role.
This is a partial backend, not a finished engine.

**Library name — `re`.** Consumers import this repository by the short name it
is known as, not by its remote name:

```c
#include "re/nio/mem.h"
#include "re/type/type.h"
```

Build manifests name it the same way — `"dependencies": ["re", ...]`,
`"includes": ["re"]`. `re` is this repository; it is not a wrapper, a shim or a
second engine. The remote keeps the long name because that is where it is
published; everything that resolves a path uses `re`.

## Current State

**Role:** R0 memory/storage owner. A partial backend, not a finished engine.

**Implemented and proven (macOS arm64):**
- Production native C IO/NIO: `Memory`/`MemoryArena`/`Transient` (32-byte
  header arena, 32-byte payload floor) and `File`/`Log` plus the macOS clipboard
  adapter.
- The shared **type algebra** (`type/type.{h,c}`): the 64-bit id encoding,
  `PROJ_*`/`ARCH_*`, `Type_make`/`Type_arch`, and the parent-chain resolver
  (`Type_registerParents`, `Type_registerBareParents`, `Type_getParentClass`,
  bounded `Type_isA`).
- The base spinlock (`atomic/`) and the opaque thread-index/tag registry
  (`thread/`).

**Moved to rangoon (`rust/`):** the Rust vocabulary projection, byte/string
storage, stable `Chunk`/`ChunkedList`, `TypedChunk`/`TypedPool`, the 32-byte
`VariableSlot`/`VariableRegistry`, the aligned byte-row `RowPool`, the Rust C ABI
headers under `rust/include/`, the fixed/growable file mappings, physical
preallocation, and the native C `name_search` primitive that scans Rust-owned
rows. rangoon owns their proof; `re` keeps no second Rust implementation.

**Stubbed, draft, or planned:** buffered readers/writers and FFF-style
gathering/indexing in `io/`; manifest-backed persistence (discussion, not
implemented durable snapshots or concurrent writes); live Hotcwap reload
integration.

**Platforms proven:** macOS arm64 only; Windows is unproven. `substrate/`
remains imported C comparison material, not production.

**Evidence:** `test/native_run.py`, `../../rust/test/run.py`, and
`../../../metadata/gists/test-checklist.md`.

## R0 responsibility split

R0 has two cooperating repositories. `re` owns the C allocator, native IO/NIO,
the stable C ABI, the type algebra, the base spinlock and the thread registry.
`rangoon` (`rust/`) owns the Rust vocabulary and the Rust storage surface behind
its own C ABI. Production engine code includes no consumer/host headers;
imported C comparison files do not prove standalone runtime dependency closure.

Production IO/NIO ownership has migrated: the default allocator and file/cache/
log/transport implementations now live in this engine's `nio/` and `io/`.
Vexspoke contains no copies. Canonical includes and the default workspace build
resolve this engine-owned C ABI. Native allocation semantics and 32-byte headers
are preserved, not rewritten into Rust. Payloads are 32-byte aligned on every
supported platform (the Universal Alignment Law). R3 may borrow either R0 public
contract. R1 owns lifetimes/residency and excludes active users before
destruction. GPU shaders, dispatch, capabilities and synchronization remain
Graphvex. No C/Rust atomic-layout compatibility or automatic record-schema
migration is assumed.

## Why C and Rust coexist

This is a boundary decision, not a taste one — and it is the question every reader
asks, so it is answered here once.

**The ABI is already C, and it is wide.** `Memory_*`, `File`, the 32-byte
block header, the project-scoped type algebra and the legacy per-project type ids are
consumed by *every* tier: R1 `hotcwap`, the R3 drivers, R4 interfaces and R5 apps all
link the C ABI. The constitution is explicit that native allocation semantics and the
existing type ids are **preserved, not rewritten**, and that this migration is *not* a
Rust allocator rewrite. Rewriting the ABI in one step would invalidate the whole
ecosystem at once, in the wrong dependency order.

**Rust is used where the boundary is thinnest and C's cost is highest:**
typed storage (aligned chunks, typed pools, generation-tagged handles), byte/string
values with atomic publication, and file-backed mappings. These are the places with
pointer arithmetic, lifetime reasoning and *unsafe external admission* — a mutable
file another process may truncate. Rust's ownership system turns
use-after-free, aliasing and "remap while borrowed" from a runtime hope into a
compile error, and the owner suite proves that with compile-negative cases, not
prose: `E0502` borrow rejection, storage arity/type rejection, and
mapping move/close/write/drop rejection.

**Why not all Rust?** Four technical reasons, in order of weight — and one of intent:

1. **The ABI break.** Every C consumer above links the current ABI; a Rust rewrite
   of it lands as one simultaneous, unprovable change across the ecosystem.
2. **No runtime forced on the floor.** R1 and the R3 drivers stay lean and
   predictable; a general-purpose allocator, panic machinery or a runtime is not
   something a host or a driver should inherit.
3. **The hot path wants an arena.** The laws require zero steady-state allocation
   and bounded waits. An arena with explicit lifetimes serves that better than a
   general allocator — and the arena implementation stays C.
4. **Progressive proof.** Each step of the migration is proved in *both* languages:
   a real C client compiles against the engine-owned header and runs the
   Rust-owned behavior. A big-bang rewrite has no such intermediate state.
5. Intentional: I dont believe in **RIIR** sorry not sorry!!!!

**Why not all C?** Because the mapping and typed-storage layer is exactly where
`unsafe` lives. C can express the same rules, but only by convention and review;
Rust enforces the view-versus-close/write/drop ordering at compile time and makes the
owner's `Drop` order explicit (`views before file`). That enforcement is the reason
those files are Rust.

**What it costs, honestly.** Two toolchains (`Cargo` and `b`), a hand-written extern
boundary, and two rules that must never be relaxed: C calls the canonical engine C ABI
and **never** casts Rust atomics to C `_Atomic`, and no C/Rust atomic-layout
compatibility is assumed. Both are stated in the lawbook and both are covered by the
C-ABI and extern-handshake rows in rangoon's owner suite.

**The rule that keeps it honest: C owns the stable ABI; Rust owns new typed storage
behind it.** Migration is progressive and each step is proved on both sides. The
Rust side is `rangoon`; `re` is the C side.

## Starting point: two interpretations

`re` is the C storage backend alongside Vexspoke, not a host or graphics driver.
Native C modules separate `nio`, `io`, `atomic`, `thread`, `type` and the
imported comparison material. Memory/string/FFI, stable chunks, append-only
collections, variable slots/registry and the native C name search moved to
`rangoon` with the Rust rows. Buffered readers/writers and FFF-style
gathering/indexing belong in `io` as planned work. Manifest-backed persistent
objects remain discussion, not implemented durable snapshots or concurrent file
writes. Graphics compute (GPU shaders/dispatch) remains Graphvex-owned.

- `nio/`, `io/`: migrated production native C allocation, scratch, filesystem,
  cache, logging and bounded transport/job APIs. The macOS clipboard adapter is
  engine-owned too. Vexspoke supplies CPU-only spin/crypto/annotation/type contracts.
- `substrate/`: preserved C comparison material, never part of the
  production native target or a source of shadow consumer headers.
- `rangoon` (`rust/`): the Rust vocabulary and storage surface, its C ABI, the
  storage/mapping/reservation laws and the owner suite. See `../../rust/README.md`.
- `docs/relational-engine-primer.md`: the C-boundary primer — the allocator, the
  native IO seams, the type algebra and the C/Rust ABI. The Rust half has its own
  primer in `rangoon`: `../../rust/docs/rangoon-primer.md`.
- Owner and tooling tests live in `test/`.

The imported C reference retains Vexspoke's Boost Software License; the
repository's original MIT license remains at the root.

## Build

The build entry is [b](https://github.com/vex-graph/b); with a
standalone b installation:

```sh
b.sh build relational_engine
```

The integrated production native entry is `./tools/b build relational_engine`
from the workspace root, with the same engine dependency supplied to ordinary
Vexspoke/Graphvex/Darling builds. The Rust surface builds in `rangoon` with
`cargo`; see `../../rust/README.md`.

## Verification and known gaps

Run `python3 -B test/native_run.py` from the engine root for strict optimized native
owners, release-like builds without scratch poisoning, and ASan/UBSan with
assertions active. Twelve owners execute per configuration; the clipboard
mutation owner explicitly skips without permission. The native allocator now
rejects physical backing exhaustion instead of returning untracked heap blocks.
Its exhaustion supplement covers all seven slab classes, bump spill, copied-byte
preservation, enumeration/reset, construction/registry allocation failures and
seeded mixed-size histories. Focused TSan uses `--owner mem_exhaustion_test
--mode thread`; its scope is initialized disjoint alloc/free, not concurrent
registry mutation or reset. See [native memory proof](docs/native-memory-proof.md)
and the checklist for actual execution and remaining gaps. HotFileSys remains a
draft no-op. No Windows, performance or live reload claim is implied.

The Rust owner suite, C ABI clients, mapping/preallocation owners, native
name-search proof and compile-negative contracts live in `rangoon`
(`python3 -B rust/test/run.py`). See `../../rust/README.md` and
`../../rust/rangoon-preferences.md`.

Universal architecture and proof follow the linked constitution in
`CONTRIBUTING.md` and `re-preferences.md`. Engine code/storage remain resident
across consumer reloads; value replacement is not automatic record schema
migration.

## Scope and Limitations

**Scope:** R0 memory and storage — allocation, file/cache/log/transport IO, the
stable C ABI, and the shared type-id algebra. It is a backend, not a host or a
driver.

**Deliberately not covered:**
- No GPU work: shaders, dispatch, capabilities and synchronization are Graphvex.
- No host/supervisor role: R1 `hotcwap` owns lifetimes/residency and excludes
  active users before destruction.
- No Rust storage: that surface, its C ABI and its mapping/preallocation proof
  live in `rangoon`.
- Imported `substrate/` is comparison material, never part of
  the production native target.

**Known limits and gaps:**
- No automatic record-schema migration; whole-value replacement is not schema
  migration, and no C/Rust atomic-layout compatibility is assumed.
- Rust typed pools are rangoon's separate API; the native C ABI semantics are
  preserved, not rewritten in Rust.
- No Windows execution, general zero-copy storage, complete concurrency/fault
  coverage, or performance claim.
