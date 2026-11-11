# relational-engine — Repo-Local Living Preferences

## Constitution Link

gist.md (`../../metadata/gists/preferences/gist.md`) is the compressed law list; all universal laws bind this repository. Read one on demand: `./tools/prefs law "<title>"`. Universal laws are inherited, never restated.

Tier: lifetime (R0 storage substrate). Owns: `nio/mem.c`, `nio/arena.c`, and `nio/transient.c` (allocator implementation), filesystem IO/NIO, memory/storage, stable row chunks, variable bindings, native C search, relational symbol table, the base spinlock (`atomic/`), and the opaque thread-index/tag registry (`thread/`). Borrows: Vexspoke CPU-only contracts (higher synchronization, crypto, constructor/annotation seams) — never duplicate storage. Thread role policy is Vexspoke's (`thread/`), never re's. VexHome layout policy is Vexspoke's (`io/`), not re's.

## Exclusive Laws

| Law Title | Scope | Enforcement |
| **Resident Storage Boundary Law** | C/Rust ownership; consumer reload | Mandatory for `re` |
| **Cross-Language Atomic Access Law** | Shared primitives and strings | Mandatory for `re` |
| **One Rust Class Per File Law** | Rust declarations | Mandatory for `re` |
| **CamelCase Rust Constructor Macro Law** | Construction spelling | Mandatory for `re` |
| **R1 Storage & Memory Substrate Law** | Rust storage; native memory; C search | Mandatory for `re` |
| **Stable Row and Variable Binding Law** | Chunks, bindings, 32-byte ABI | Mandatory for `re` |
| **Fixed File Mapping Law** | Fixed-extent file-backed byte views | Mandatory for `re` |
| **Growable File Mapping Law** | Reservation-backed growing byte views | Mandatory for `re` |
| **Physical File Reservation Law** | Create-new byte files | Mandatory for `re` |

### Resident Storage Boundary Law

* R1 residency of engine code/storage: code/storage stays resident across consumer reloads; active readers/writers excluded before destruction.
* Whole-value replacement is not automatic record-schema migration; migration requires staged validation, rollback and owner proof.

### Cross-Language Atomic Access Law

* Rust owns atomic storage; C calls the canonical engine C ABI and never casts Rust atomics to C `_Atomic` representations.
* Registration/release/destruction require exclusive access; existing atomic values permit concurrent atomic get/set while the registry stays frozen.
* Non-atomic getters cannot read atomic cells; wrong storage kinds reject with a defined error.
* Atomic Bytes use release stores/acquire loads; atomic strings publish immutable byte snapshots with release/acquire; readers never lock; writers try-lock and return busy.

### One Rust Class Per File Law

* One file per class: a class is its Rust struct plus its owning `impl` methods; the lowercase snake-case file name matches the class.
* Each implementation file defines at most one named struct or enum; public methods `pub fn`.
* Each enum has its own file; private helper records too.
* `mod.rs`/`lib.rs` declare/re-export modules and macros, not mixed behavioral types.

### CamelCase Rust Constructor Macro Law

* Constructor macros use CamelCase matching the object concept: `Memory!()` and `Bytes!()`.
* Ordinary Rust methods and C-facing primitive operations use snake_case (e.g. `get_atomic_byte`).
* Compatibility lowercase macros may remain for existing clients but never replace the canonical spelling in new examples.

### R1 Storage & Memory Substrate Law

* `nio/mem.c` is the only libc backing seam; `nio/arena.c` and `nio/transient.c` own the arena and scratch mechanics. Every other repository allocates through `Memory_alloc`/`MemoryArena_alloc`/`Transient_alloc`.
* libc `malloc`/`free`/`calloc`/`realloc`/`strdup` outside `re/nio/mem.c` is a defect.
* Engine `io`/`nio` own the production native C Memory/MemoryArena/Transient and file/log/transport ABI (32-byte headers, legacy project type IDs, C allocation semantics preserved).
* Default builds consume the engine; Rust typed pools stay a distinct API, not a silent allocator substitution.

### Stable Row and Variable Binding Law

* `nio/chunk.rs` owns a fixed-capacity typed row allocation; `struct/chunked_list.rs` grows its directory without moving initialized rows.
* Geometry is chosen at construction (named default, caller override), validated for size/alignment, immutable thereafter.
* Rows are append-only, never deleted/reused in this version.
* `variable/variable_slot.rs` is a C-layout 32-byte binding: `[name: u8[24]][value pointer: 8 bytes]`, offsets 0/24 checked.
* Names are 1..23 ASCII bytes plus NUL and zero padding, folded lowercase; invalid input rejects without silent truncation or mutating prior state.
* Zero slots are unnamed/unbound; registry admission requires a valid name.
* `VariableRegistry` owns stable slot rows and rejects folded duplicate names.
* Engine C `search/primitives/name_search` scans fixed 24-byte keys in borrowed leaves: no Vexspoke/host headers, no allocation; invalid spans admitted through engine `exception/throw.h`.

### Fixed File Mapping Law

* `FixedMappedFile` owns a fixed-extent existing regular-file mapping through memmap2; the owner retains the file until close/drop and unmaps before release.
* Read-only and shared read/write modes are explicit; empty files are valid open owners without an OS mapping.
* No implicit create/truncate/resize, C mapping ABI, internal dirty tracker, database format or typed reinterpretation.
* Opening is unsafe admission: the caller excludes external mutation/truncation and alias violations of writable exclusivity for the full mapping lifetime.
* Reads/writes validate bounds and arithmetic before access; rejection preserves bytes.
* Flush requests mapped-page writeback; sync requests file synchronization; both expose OS errors; neither claims atomic publication or power-loss transactions.

### Growable File Mapping Law

* `GrowableMappedFile` owns a fixed virtual RESERVATION over a file it creates or reopens, plus a committed length growing inside that reservation.
* Reservation is fixed at create, caller-chosen; mapping made once; growing the reservation is not offered.
* Committed length grows in caller-sized steps; only the step's blocks are allocated, never the whole reservation up front.
* Reserve step blocks, then raise the committed length; a failed growth leaves length and every byte unchanged.
* Byte access is bounded by committed length, never the reservation.
* Released ranges are reused via a free list of emptied page ranges.
* Always read/write; no read-only form: growth requires write access.

### Physical File Reservation Law

* `PreallocatedFile`: caller-sized nonzero, create-new regular byte storage.
* macOS F_PREALLOCATE/F_ALLOCATEALL and Linux fallocate reserve disk before success; EOF is never reservation and allocated OS blocks must cover the extent.
* Unsupported hosts/filesystems and allocation failures reject without sparse fallback.
* Unix mode starts at 0600; existing destinations/final symlinks reject unchanged.
* Creation-stage failure closes/removes the artifact; cleanup failure reports both errors; drop/close retains successful files.

## Repo-Local Extensions

* None — no managed exceptions to universal laws.

## Readiness Cross-Reference

* Feature readiness: relational-engine readiness gist.
* Open blockers: ecosystem blockers gist.
* Proof lawbook: `tests/test-preferences.md` — `./tools/prefs test "<title>"`.
* Evidence ledger: `metadata/gists/test-checklist.md` — `python3 tools/test_checklist.py check`.
