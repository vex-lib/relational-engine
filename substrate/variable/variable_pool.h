#ifndef RELATIONAL_VARIABLE_POOL_H
#define RELATIONAL_VARIABLE_POOL_H

#include "vex.h"
#include "nio/mem.h"

/**
 * substrate/variable/variable_pool.h, process-wide interned string pool.
 *
 * Names are stated once, shared by pointer, never copied: each distinct name
 * lives in exactly one 32-byte slot, and variable rows (or any consumer)
 * reference the slot instead of duplicating Bytes. One table per process,
 * name identity is inherently global (two arenas interning "label" must
 * share, not duplicate). Backing comes from the init-time arena; pool
 * strings are immutable and freed wholesale at arena teardown, never per
 * string. All entries serialize on an internal spinlock.
 *
 * SLOT RECORD (owned by the pool service, behaviorless):
 * [ptr][str1][str2][str3], 4x uint64, 32 Bytes total:
 * self  : intrusive validity, must equal the slot's own address
 * name  : NUL-terminated Bytes, zero-padded (exact Bytes, no case fold)
 * Lookup goes through a sorted u32 index over the slots (binary search with
 * full 24-byte compares). Slot indices stay valid forever; raw slot pointers
 * hold until the next grow, re-resolve via find/slot, or gate use on isSlot.
 */

#define STRING_POOL_NAME_MAX 23u
#define STRING_POOL_SLOT_SIZE 32u
#define STRING_POOL_DEFAULT_CAPACITY 64u

/**
 * Process-wide interned string slot (32 bytes, behaviorless).
 *
 * u64 self      - intrusive validity; must equal the slot's own address
 * utf8 name[24] - NUL-terminated, zero-padded, exact bytes
 */
typedef struct StringSlot {
    u64 self;
    utf8 name[24];
} StringSlot;

/**
 * Internal state table for the process-wide StringPoolService.
 *
 * u32 magic          - STRING_POOL_MAGIC when live (fail-closed gate)
 * MemoryArena *arena - backing arena (borrowed; teardown frees all)
 * StringSlot *slots  - slot array (append-only; may move on grow)
 * u32 *order         - sorted slot indices
 * u32 count          - live slots
 * u32 capacity       - allocated slots
 */
typedef struct StringPoolState {
    u32 magic;
    MemoryArena *arena;
    StringSlot *slots;
    u32 *order;
    u32 count;
    u32 capacity;
} StringPoolState;

bool StringPool_init(MemoryArena *arena);

fn StringPool_shutdown();

i32 StringPool_intern(string str);

i32 StringPool_find(string str);

const StringSlot *StringPool_slot(u32 index);
string StringPool_name(u32 index);

u32 StringPool_count();

bool StringPool_isSlot(const_any ptr);

#endif
