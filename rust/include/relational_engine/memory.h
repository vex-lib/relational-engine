#ifndef RELATIONAL_MEMORY_H
#define RELATIONAL_MEMORY_H
#include "vex.h"
/* Standalone learning ABI, NOT Vexspoke Memory_* compatibility.
 * Owner is opaque and exclusively managed; drop once, never concurrently.
 * IDs are owner-local. Pointer arguments must be live, aligned and sized;
 * null source is valid only for zero length, null destination for empty data.
 * Output parameters must not alias inputs/owner. Failures preserve outputs.
 * Status: 0 OK, 1 invalid, 2 allocation/exhaustion, 3 unknown ID, 4 capacity.
 * Atomic status extensions: 5 wrong kind, 6 byte index bounds, 7 writer busy.
 * Atomic get/set calls may overlap on an existing owner while registration,
 * release and destruction are excluded. Only atomic operations access atomics.
 * Strings publish whole immutable byte snapshots (not necessarily UTF-8).
 * Old snapshots stay until owner destruction; caller chooses retention budget.
 * String set may allocate, returns busy instead of waiting. Buffer-too-small
 * sets out_truncated=true without changing destination/output_length.
 * Engine code/storage must remain resident across consumer hot reloads.
 * new may abort on allocator OOM. No per-frame/real-time guarantees. */
typedef struct ReMemory ReMemory;
ReMemory *re_memory_new(void);
void re_memory_drop(ReMemory *owner);
u32 re_memory_copy(ReMemory *owner, const u8 *source, bytes length, u64 *output);
u32 re_memory_read(const ReMemory *owner, u64 id, u8 *destination, bytes capacity, bytes *output_length);
u32 re_memory_get_byte(const ReMemory *owner, u64 id, bytes index, u8 *output);
u32 re_memory_new_atomic_byte(ReMemory *owner, u8 value, u64 *output);
u32 re_memory_get_atomic_byte(const ReMemory *owner, u64 id, u8 *output);
u32 re_memory_set_atomic_byte(const ReMemory *owner, u64 id, u8 value);
u32 re_memory_new_atomic_string(ReMemory *owner, const u8 *source, bytes length, bytes retention_limit, u64 *output);
u32 re_memory_get_atomic_string(const ReMemory *owner, u64 id, u8 *destination, bytes capacity, bytes *output_length, bool *out_truncated);
u32 re_memory_set_atomic_string(const ReMemory *owner, u64 id, const u8 *source, bytes length);
#endif
