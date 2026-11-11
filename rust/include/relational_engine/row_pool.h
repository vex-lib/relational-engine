#ifndef RE_ROW_POOL_H
#define RE_ROW_POOL_H
#include "vex.h"
#include "relational_engine/row_handle.h"

/* Runtime-selected, aligned, flat byte rows owned by Rust, not Memory_* ABI.
 * Status: 0 OK, 1 invalid geometry/span/null argument, 2 allocation/exhaustion,
 * 3 stale/wrong-owner/missing handle, 4 insufficient read/projection capacity.
 * Racing new calls may return 2 (single-attempt owner ID issue); retry externally.
 * Failure preserves outputs/live rows, except truncation flag set on status 4.
 * Geometry: nonzero size/count, power-of-two alignment, checked stride/total size.
 * One pool holds one size/alignment; rows are opaque initialized bytes. Input is
 * copied, never retained. Embedded pointers/destructors stay caller-owned.
 * All non-null owners/outputs/spans must be live, aligned/sized and disjoint.
 * Source must not alias pool row storage. No arbitrary/stale owner validation.
 * All calls externally serialized; exclude raw row reads before any mutation/drop.
 * Borrow is READ ONLY and survives growth/write to other rows; expires on removal
 * or owner drop. release_empty retains generation history, never releases live rows.
 * Handles never wrap; exhausted slots retire. Wrong-owner handles reject, but this
 * is not security against forged identities. R1 owns pool lifetime/residency.
 * Cold new/add/release/projections may allocate; no per-frame latency promise.
 * Normal pool allocations are fallible; formatting follows Rust abort-on-OOM policy.
 * No panic recovery, internal threads/locks, persistence, type registry, schema
 * migration or live reload integration is provided by these operations. */
typedef struct ReRowPool ReRowPool;

u32 re_rows_new(bytes row_size, bytes alignment, bytes rows_per_chunk, ReRowPool **out_owner);
void re_rows_drop(ReRowPool *owner); /* null is a quiet no-op; live owner drops once */
u32 re_rows_add(ReRowPool *owner, const u8 *source, bytes length, ReRowHandle *out_handle);
u32 re_rows_read(const ReRowPool *owner, ReRowHandle handle, u8 *dest, bytes capacity, bool *out_truncated);
u32 re_rows_write(ReRowPool *owner, ReRowHandle handle, const u8 *source, bytes length);
u32 re_rows_borrow(const ReRowPool *owner, ReRowHandle handle, const u8 **out_bytes);
u32 re_rows_remove(ReRowPool *owner, ReRowHandle handle);
u32 re_rows_release_empty(ReRowPool *owner, bytes *out_released);
u32 re_rows_len(const ReRowPool *owner, bytes *out_len);
u32 re_rows_geometry(const ReRowPool *owner, bytes *out_size, bytes *out_alignment, bytes *out_rows_per_chunk);
u32 re_rows_to_string(const ReRowPool *owner, char *dest, bytes capacity, bool *out_truncated);
u32 re_rows_to_string_struct(const ReRowPool *owner, char *dest, bytes capacity, bool *out_truncated);

/* Default geometry is a starting point, never a total row ceiling. */
#define RE_ROWS_PER_CHUNK_DEFAULT ((bytes) 1024)
static inline u32 ReRowPool_3(bytes size, bytes alignment, ReRowPool **out_owner) {
    return re_rows_new(size, alignment, RE_ROWS_PER_CHUNK_DEFAULT, out_owner);
}
static inline u32 ReRowPool_4(bytes size, bytes alignment, bytes capacity, ReRowPool **out_owner) {
    return re_rows_new(size, alignment, capacity, out_owner);
}
#define RE_ROW_POOL_SELECT(_1, _2, _3, _4, NAME, ...) NAME
#define ReRowPool(...) RE_ROW_POOL_SELECT(__VA_ARGS__, ReRowPool_4, ReRowPool_3)(__VA_ARGS__)
#endif
