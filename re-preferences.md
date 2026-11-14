# relational-engine — Repo-Local Living Preferences

## Constitution Link

gist.md (`../../metadata/gists/preferences/gist.md`) is the compressed law list; all universal laws bind this repository. Read one on demand: `./tools/prefs law "<title>"`. Universal laws are inherited, never restated.

Tier: lifetime (R0 storage substrate). Owns: `nio/mem.c`, `nio/arena.c`, and `nio/transient.c` (the allocator implementation), filesystem IO/NIO, native C memory/storage, the base spinlock (`atomic/`), and the opaque thread-index/tag registry (`thread/`). Borrows: Vexspoke CPU-only contracts (higher synchronization, crypto, constructor/annotation seams) — never duplicate storage. Thread role policy is Vexspoke's (`thread/`), never re's. VexHome layout policy is Vexspoke's (`io/`), not re's. Rust storage, its C ABI and the storage/mapping/reservation laws are rangoon's (`rust/`), not re's.

## Exclusive Laws

| Law Title | Scope | Enforcement |
| **Resident Storage Boundary Law** | C residency; consumer reload | Mandatory for `re` |
| **R1 Storage & Memory Substrate Law** | Native memory; allocator seam | Mandatory for `re` |
| **Rust Boundary Law** | Rust ownership seam | Mandatory for `re` |

### Resident Storage Boundary Law

* R1 residency of engine code/storage: code/storage stays resident across consumer reloads; active readers/writers excluded before destruction.
* Whole-value replacement is not automatic record-schema migration; migration requires staged validation, rollback and owner proof.

### R1 Storage & Memory Substrate Law

* `nio/mem.c` is the only libc backing seam; `nio/arena.c` and `nio/transient.c` own the arena and scratch mechanics. Every other repository allocates through `Memory_alloc`/`MemoryArena_alloc`/`Transient_alloc`.
* libc `malloc`/`free`/`calloc`/`realloc`/`strdup` outside `re/nio/mem.c` is a defect.
* Engine `io`/`nio` own the production native C Memory/MemoryArena/Transient and file/log/transport ABI (32-byte headers, legacy project type IDs, C allocation semantics preserved).
* Default builds consume the engine; Rust typed pools are rangoon's distinct API, not a silent allocator substitution.

### Rust Boundary Law

* Rust storage, the Rust C ABI and the storage/mapping/reservation laws live in `rangoon` (`rust/`); `re` keeps no second Rust implementation.
* The C name-search primitive moved with the Rust rows into `rangoon`; `re` does not own or compile it.
* `re` may consume rangoon through its canonical C ABI; it never includes rangoon's Rust internals or re-implements its storage.

## Repo-Local Extensions

* None — no managed exceptions to universal laws.

## Readiness Cross-Reference

* Feature readiness: relational-engine readiness gist.
* Open blockers: ecosystem blockers gist.
* Proof lawbook: `tests/test-preferences.md` — `./tools/prefs test "<title>"`.
* Evidence ledger: `metadata/gists/test-checklist.md` — `python3 tools/test_checklist.py check`.
