#ifndef RELATIONAL_VARIABLE_POOL_H
#define RELATIONAL_VARIABLE_POOL_H

#include "vex.h"

#include "nio/mem.h"

// substrate/variable/variable_pool.h — process-wide interned string pool.
//
// Names are stated once, shared by pointer, never copied: each distinct name
// lives in exactly one 32-byte slot, and variable rows (or any consumer)
// reference the slot instead of duplicating Bytes. One table per process —
// name identity is inherently global (two arenas interning "label" must
// share, not duplicate). Backing comes from the init-time arena; pool
// strings are immutable and freed wholesale at arena teardown, never per
// string. All entries serialize on an internal spinlock.
//
// SLOT RECORD (owned by the pool service, behaviorless):
//   [ptr][str1][str2][str3] — 4x uint64, 32 Bytes total:
//     self  : intrusive validity, must equal the slot's own address
//     name  : NUL-terminated Bytes, zero-padded (exact Bytes, no case fold)
// Lookup goes through a sorted u32 index over the slots (binary search with
// full 24-byte compares). Slot indices stay valid forever; raw slot pointers
// hold until the next grow — re-resolve via find/slot, or gate use on isSlot.

#define STRING_POOL_NAME_MAX 23u
#define STRING_POOL_SLOT_SIZE 32u
#define STRING_POOL_DEFAULT_CAPACITY 64u

typedef struct StringSlot {
    u64 self;      // intrusive validity: must equal own address
    char name[24];      // NUL-terminated, zero-padded, exact Bytes
} StringSlot;

bool StringPool_init(MemoryArena *arena);

void StringPool_shutdown(void);

i32 StringPool_intern(const char *str);

i32 StringPool_find(const char *str);

const StringSlot *StringPool_slot(u32 index);
const char *StringPool_name(u32 index);

u32 StringPool_count(void);

bool StringPool_isSlot(const void *ptr);

#endif
