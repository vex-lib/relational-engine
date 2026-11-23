/**
 * substrate/relational/symbol_table.c, SymbolTable registry port (Legacy: substrate/variable/SymbolTable.java).
 *
 * Rows reference shared pool slots by index (never inline names); lookup
 * goes pool-first, then one sparse hop (slot  to  var id). Typed queries
 * filter rows by classId, one structure, no segregated lists.
 */

#include "vex.h"
#include "substrate/relational/symbol_table.h"

#include <stdio.h>
#include <string.h>

#include "lang/annotation.h"
#include "nio/mem.h"
#include "type/re_type.h"
#include "substrate/variable/variable_pool.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: SymbolTable
 * ============================================================================
 * The relational symbol registry: dense SymbolRow records (varId == index,
 * append-only) reference shared StringPool slots by index, never inline
 * names, and a sparse bySlot hop maps pool slot back to varId. Names are
 * validated cold (ASCII alnum/underscore/dot, folded lowercase, max
 * STRING_POOL_NAME_MAX) before interning; typed queries filter rows by
 * classId over one structure with no segregated lists. Rows and the bySlot
 * table grow on demand via Memory_alloc/Memory_realloc; shutdown frees both and marks the
 * registry inactive.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SymbolTable (substrate/relational/symbol_table.c)
 * ============================================================================
 * the relational symbol registry (Legacy: substrate/variable/SymbolTable.java).
 *
 * STRUCT FIELDS (Mirroring substrate/relational/symbol_table.h):
 * ----------------------------------------------------------------------------
 *   SymbolTable {
 *     bool active; // runtime-active flag
 *     SymbolRow *rows; // dense rows, varId == index (append-only)
 *     u32 count; // live rows
 *     u32 capacity; // allocated rows
 *     i32 *bySlot; // pool slot  to  varId, -1 empty (sparse, grown)
 *     u32 bySlotCap; // bySlot slots allocated
 *   }
 *
 * SLOT RECORD (owned by SymbolTable, behaviorless):
 * ----------------------------------------------------------------------------
 *   SymbolRow slot; // i32 + pool slot index (name lives in pool)
 *   SymbolRow classId; // u32 + value class, pinned at creation
 *   SymbolRow pointer; // pointer + the value
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - SymbolTable_0()
 *   - SymbolTable_shutdown(v)
 *
 * Core Functions:
 *   - SymbolTable_instant(v, name, classId, targetPointer)
 *   - SymbolTable_rename(v, oldName, newName)
 *   - SymbolTable_findByClass(v, classId, outIds, cap)
 *   - SymbolTable_compareAndSetPointer(v, varId, expected, newPointer)
 *
 * Setters:
 *   - SymbolTable_setPointer(v, varId, targetPointer)
 *
 * Getters:
 *   - SymbolTable_getId(v, name)
 *   - SymbolTable_getPointer(v, varId)
 *   - SymbolTable_getClassId(v, varId)
 *   - SymbolTable_getName(v, varId)
 *   - SymbolTable_getActiveCount(v)
 * ============================================================================
 */

/**
 * Validate + fold to lowercase for pool space. Dots ride along for dotted
 * paths (character.position.x); everything outside ASCII alnum/underscore/
 * dot, including multibyte UTF-8/16 garbage, is rejected cold.
 */
static bool clean_name(string name, utf8 *lowered) {
    if (!name || name[0] == '\0') 
        return false;

    bytes len = strlen(name);

    if (len > STRING_POOL_NAME_MAX) 
        return false;

    for (bytes i = 0; i < len; i++) {
        u8 c = (u8) name[i];

        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok) 
            return false;

        lowered[i] = (c >= 'A' && c <= 'Z') ? (utf8) (c + 32) : (utf8) c;
    }

    lowered[len] = '\0';

    return true;
}

;;GETTER
static inline bool SymbolTable_active(const SymbolTable *v)
{
    return (*v).active;
}
;;SETTER
static inline fn SymbolTable_setActive(SymbolTable *v, bool active)
{
    (*v).active = active;
}
;;GETTER
static inline SymbolRow *SymbolTable_rows(const SymbolTable *v)
{
    return (*v).rows;
}
;;SETTER
static inline fn SymbolTable_setRows(SymbolTable *v, SymbolRow *rows)
{
    (*v).rows = rows;
}
;;GETTER
static inline u32 SymbolTable_count(const SymbolTable *v)
{
    return (*v).count;
}
;;SETTER
static inline fn SymbolTable_setCount(SymbolTable *v, u32 count)
{
    (*v).count = count;
}
;;GETTER
static inline u32 SymbolTable_capacity(const SymbolTable *v)
{
    return (*v).capacity;
}
;;SETTER
static inline fn SymbolTable_setCapacity(SymbolTable *v, u32 cap)
{
    (*v).capacity = cap;
}
;;GETTER
static inline i32 *SymbolTable_bySlot(const SymbolTable *v)
{
    return (*v).bySlot;
}
;;SETTER
static inline fn SymbolTable_setBySlot(SymbolTable *v, i32 *bySlot)
{
    (*v).bySlot = bySlot;
}
;;GETTER
static inline u32 SymbolTable_bySlotCap(const SymbolTable *v)
{
    return (*v).bySlotCap;
}
;;SETTER
static inline fn SymbolTable_setBySlotCap(SymbolTable *v, u32 cap)
{
    (*v).bySlotCap = cap;
}
;;GETTER
static inline pointer SymbolRow_pointer(const SymbolRow *row)
{
    return (*row).pointer;
}
;;SETTER
static inline fn SymbolRow_setPointer(SymbolRow *row, pointer ptr)
{
    (*row).pointer = ptr;
}
;;GETTER
static inline u32 SymbolRow_classId(const SymbolRow *row)
{
    return (*row).classId;
}
;;GETTER
static inline i32 SymbolRow_slot(const SymbolRow *row)
{
    return (*row).slot;
}

static SymbolRow *row_at(SymbolTable *v, i32 varId) {
    if (!v || !SymbolTable_active(v) || !SymbolTable_rows(v) || varId < 0 || (u32) varId >= SymbolTable_count(v)) 
        return nullptr;

    SymbolRow *rows = SymbolTable_rows(v);

    return &rows[varId];
}

/**
 * Pool slot  to  var id through the sparse hop. -1 when unmapped (a pool slot
 * may exist globally without belonging to this scope).
 */
static i32 slot_var(SymbolTable *v, i32 slot) {
    if (slot < 0 || (u32) slot >= SymbolTable_bySlotCap(v)) 
        return -1;

    i32 *bySlot = SymbolTable_bySlot(v);

    return bySlot[slot];
}

static bool ensure_byslot(SymbolTable *v, u32 need) {
    u32 curCap = SymbolTable_bySlotCap(v);
    if (need < curCap) 
        return true;

    u32 cap = curCap ? curCap * 2 : 64;

    while (cap <= need) {
        if (cap > UINT32_MAX / 2) 
            return false;

        cap *= 2;
    }

    i32 *next = (i32*) Memory_alloc(TYPE_SYMBOL_TABLE, Re_arena(), (bytes) cap * sizeof(i32));

    if (!next) 
        return false;

    for (u32 i = 0; i < cap; i++) 
        next[i] = -1;

    i32 *oldBySlot = SymbolTable_bySlot(v);
    if (oldBySlot) {
        memcpy(next, oldBySlot, (bytes) curCap * sizeof(i32));

        Memory_free(oldBySlot);
    }

    SymbolTable_setBySlot(v, next);

    SymbolTable_setBySlotCap(v, cap);

    return true;
}

SymbolTable SymbolTable_0() {
    SymbolTable v;

    memset(&v, 0, sizeof v);

    MemoryArena *arena = Memory_defaultArena();

    if (!arena) 
        return v;

    if (!StringPool_init(arena)) 
        return v;

    v.rows = (SymbolRow*) Memory_alloc(TYPE_SYMBOL_TABLE, Re_arena(), SYMBOL_TABLE_DEFAULT_CAPACITY * sizeof(SymbolRow));

    if (!v.rows) 
        return v;

    v.capacity = SYMBOL_TABLE_DEFAULT_CAPACITY;

    v.active = true;

    return v;
}

fn SymbolTable_shutdown(SymbolTable *v) {
    if (!v || !SymbolTable_active(v)) 
        return;

    Memory_free(SymbolTable_rows(v));

    Memory_free(SymbolTable_bySlot(v));

    SymbolTable_setRows(v, nullptr);

    SymbolTable_setBySlot(v, nullptr);

    SymbolTable_setCount(v, 0);

    SymbolTable_setCapacity(v, 0);

    SymbolTable_setBySlotCap(v, 0);

    SymbolTable_setActive(v, false);
}

i32 SymbolTable_instant(SymbolTable *v, string name, u32 classId, pointer targetPointer) {
    if (!v || !SymbolTable_active(v)) {
        fprint(stderr, "[variable] instant: inactive registry");

        return -1;
    }

    if (!name || name[0] == '\0') {
        fprint(stderr, "[variable] instant: empty name rejected");

        return -1;
    }

    utf8 lowered[24];

    bytes rawLen = strlen(name);

    if (rawLen > STRING_POOL_NAME_MAX) {
        fprint(stderr, "[variable] instant: name too long ('", name, "', max 23)");

        return -1;
    }

    if (!clean_name(name, lowered)) {
        fprint(stderr, "[variable] instant: illegal name ('", name, "'): ascii alnum, underscore, dot only");

        return -1;
    }

    i32 slot = StringPool_find(lowered);

    if (slot >= 0 && slot_var(v, slot) >= 0) {
        fprint(stderr, "[variable] instant: name already registered ('", lowered, "')");

        return -1;
    }

    if (!ensure_byslot(v, StringPool_count())) {
        fprint(stderr, "[variable] instant: out of memory ('", lowered, "')");

        return -1;
    }

    slot = StringPool_intern(lowered);

    if (slot < 0) {
        fprint(stderr, "[variable] instant: intern failed ('", lowered, "')");

        return -1;
    }

    u32 count = SymbolTable_count(v);
    u32 cap = SymbolTable_capacity(v);
    if (count >= cap) {
        bytes newCap = (bytes)cap + SYMBOL_TABLE_DEFAULT_CAPACITY;

        SymbolRow *next = (SymbolRow*) Memory_realloc(SymbolTable_rows(v), newCap * sizeof(SymbolRow));

        if (!next) {
            fprint(stderr, "[variable] instant: out of memory ('", lowered, "')");

            return -1;
        }

        SymbolTable_setRows(v, next);

        SymbolTable_setCapacity(v, (u32) newCap);
    }

    u32 id = count;

    SymbolRow *rows = SymbolTable_rows(v);

    rows[id].slot = slot;

    rows[id].classId = classId;

    rows[id].pointer = targetPointer;

    i32 *bySlot = SymbolTable_bySlot(v);

    bySlot[slot] = (i32) id;

    SymbolTable_setCount(v, count + 1u);

    return (i32) id;
}

i32 SymbolTable_getId(SymbolTable *v, string name) {
    if (!v || !SymbolTable_active(v) || !name) 
        return -1;

    utf8 lowered[24];

    if (!clean_name(name, lowered)) 
        return -1;

    i32 slot = StringPool_find(lowered);

    if (slot < 0) 
        return -1;

    return slot_var(v, slot);
}

bool SymbolTable_rename(SymbolTable *v, string oldName, string newName) {
    if (!v || !SymbolTable_active(v)) {
        fprint(stderr, "[variable] rename: inactive registry");

        return false;
    }

    utf8 oldLower[24];

    utf8 newLower[24];

    if (!oldName || !clean_name(oldName, oldLower)) {
        fprint(stderr, "[variable] rename: bad old name");

        return false;
    }

    if (!newName || !clean_name(newName, newLower)) {
        fprint(stderr, "[variable] rename: bad new name");

        return false;
    }

    i32 oldSlot = StringPool_find(oldLower);

    i32 id = oldSlot >= 0 ? slot_var(v, oldSlot) : -1;

    if (id < 0) {
        fprint(stderr, "[variable] rename: unknown name ('", oldLower, "')");

        return false;
    }

    i32 newSlot = StringPool_find(newLower);

    if (newSlot >= 0 && slot_var(v, newSlot) >= 0) {
        fprint(stderr, "[variable] rename: name already registered ('", newLower, "')");

        return false;
    }

    if (!ensure_byslot(v, StringPool_count())) {
        fprint(stderr, "[variable] rename: out of memory ('", newLower, "')");

        return false;
    }

    newSlot = StringPool_intern(newLower);

    if (newSlot < 0) {
        fprint(stderr, "[variable] rename: intern failed ('", newLower, "')");

        return false;
    }

    SymbolRow *rows = SymbolTable_rows(v);

    i32 *bySlot = SymbolTable_bySlot(v);

    i32 oldSlotIdx = rows[id].slot;

    rows[id].slot = newSlot;

    if (oldSlotIdx >= 0 && (u32) oldSlotIdx < SymbolTable_bySlotCap(v) && bySlot[oldSlotIdx] == id) 
        bySlot[oldSlotIdx] = -1;

    bySlot[newSlot] = id;

    return true;
}

bytes SymbolTable_findByClass(SymbolTable *v, u32 classId, i32 *outIds, bytes cap) {
    if (!v || !SymbolTable_active(v)) 
        return 0;

    bytes total = 0;

    SymbolRow *rows = SymbolTable_rows(v);
    u32 count = SymbolTable_count(v);

    for (u32 i = 0; i < count; i++) {
        if (rows[i].classId == classId) {
            if (outIds && total < cap) 
                outIds[total] = (i32) i;

            total++;
        }
    }

    return total;
}

pointer SymbolTable_getPointer(SymbolTable *v, i32 varId) {
    SymbolRow *row = row_at(v, varId);

    return row ? SymbolRow_pointer(row) : 0;
}

fn SymbolTable_setPointer(SymbolTable *v, i32 varId, pointer targetPointer) {
    SymbolRow *row = row_at(v, varId);

    if (!row) 
        return;

    SymbolRow_setPointer(row, targetPointer);
}

bool SymbolTable_compareAndSetPointer(SymbolTable *v, i32 varId, pointer expected, pointer newPointer) {
    SymbolRow *row = row_at(v, varId);

    if (!row) 
        return false;

    if (SymbolRow_pointer(row) != expected) 
        return false;

    SymbolRow_setPointer(row, newPointer);

    return true;
}

u32 SymbolTable_getClassId(SymbolTable *v, i32 varId) {
    SymbolRow *row = row_at(v, varId);

    return row ? SymbolRow_classId(row) : 0;
}

string SymbolTable_getName(const SymbolTable *v, i32 varId) {
    const SymbolRow *row = row_at((SymbolTable*) v, varId);

    if (!row) 
        return nullptr;

    return StringPool_name((u32) SymbolRow_slot(row));
}

bytes SymbolTable_getActiveCount(SymbolTable *v) {
    if (!v || !SymbolTable_active(v)) 
        return 0;

    return SymbolTable_count(v);
}
