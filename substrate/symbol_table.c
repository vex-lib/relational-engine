// substrate/symbol_table.c — SymbolTable registry port (Legacy: variable/SymbolTable.java).
//
// Rows reference shared pool slots by index (never inline names); lookup
// goes pool-first, then one sparse hop (slot -> var id). Typed queries
// filter rows by classId — one structure, no segregated lists.

#include "substrate/symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang/annotation.h"
#include "nio/mem.h"
#include "variable/variable_pool.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: SymbolTable
 * ============================================================================
 * The relational symbol registry: dense SymbolRow records (varId == index,
 * append-only) reference shared StringPool slots by index — never inline
 * names — and a sparse bySlot hop maps pool slot back to varId. Names are
 * validated cold (ASCII alnum/underscore/dot, folded lowercase, max
 * STRING_POOL_NAME_MAX) before interning; typed queries filter rows by
 * classId over one structure with no segregated lists. Rows and the bySlot
 * table grow on demand via malloc/realloc; shutdown frees both and marks the
 * registry inactive.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SymbolTable (substrate/symbol_table.c)
 * ============================================================================
 * the relational symbol registry (Legacy: variable/SymbolTable.java).
 *
 * STRUCT FIELDS (Mirroring substrate/symbol_table.h):
 * ----------------------------------------------------------------------------
 *   SymbolTable {
 *     bool active; // runtime-active flag
 *     SymbolRow *rows; // dense rows, varId == index (append-only)
 *     u32 count; // live rows
 *     u32 capacity; // allocated rows
 *     i32 *bySlot; // pool slot -> varId, -1 empty (sparse, grown)
 *     u32 bySlotCap; // bySlot slots allocated
 *   }
 *
 * SLOT RECORD (owned by SymbolTable, behaviorless):
 * ----------------------------------------------------------------------------
 *   SymbolRow slot; // i32 + pool slot index (name lives in pool)
 *   SymbolRow classId; // u32 + value class, pinned at creation
 *   SymbolRow pointer; // uintptr_t + the value
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

// Validate + fold to lowercase for pool space. Dots ride along for dotted
// paths (character.position.x); everything outside ASCII alnum/underscore/
// dot — including multibyte UTF-8/16 garbage — is rejected cold.
static bool clean_name(const char *name, char *lowered) {
    if (!name || name[0] == '\0')
        return false;
    bytes len = strlen(name);
    if (len > STRING_POOL_NAME_MAX)
        return false;
    for (bytes i = 0; i < len; i++) {
        unsigned char c = (unsigned char) name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok)
            return false;
        lowered[i] = (c >= 'A' && c <= 'Z') ? (char) (c + 32) : (char) c;
    }
    lowered[len] = '\0';
    return true;
}

static SymbolRow *row_at(SymbolTable *v, i32 varId) {
    if (!v || !(*v).active || !(*v).rows || varId < 0 || (u32) varId >= (*v).count)
        return nullptr;
    SymbolRow *rows = (*v).rows;
    return &rows[varId];
}

// Pool slot -> var id through the sparse hop. -1 when unmapped (a pool slot
// may exist globally without belonging to this scope).
static i32 slot_var(SymbolTable *v, i32 slot) {
    if (slot < 0 || (u32) slot >= (*v).bySlotCap)
        return -1;
    i32 *bySlot = (*v).bySlot;
    return bySlot[slot];
}

static bool ensure_byslot(SymbolTable *v, u32 need) {
    if (need < (*v).bySlotCap)
        return true;
    u32 cap = (*v).bySlotCap ? (*v).bySlotCap * 2 : 64;
    while (cap <= need) {
        if (cap > UINT32_MAX / 2)
            return false;
        cap *= 2;
    }
    i32 *next = (i32*) malloc((bytes) cap * sizeof(i32));
    if (!next)
        return false;
    for (u32 i = 0; i < cap; i++)
        next[i] = -1;
    if ((*v).bySlot) {
        memcpy(next, (*v).bySlot, (bytes)(*v).bySlotCap * sizeof(i32));
        free((*v).bySlot);
    }
    (*v).bySlot = next;
    (*v).bySlotCap = cap;
    return true;
}

SymbolTable SymbolTable_0(void) {
    SymbolTable v;
    memset(&v, 0, sizeof v);
    MemoryArena *arena = Memory_defaultArena();
    if (!arena)
        return v;
    if (!StringPool_init(arena))
        return v;
    v.rows = (SymbolRow*) malloc(SYMBOL_TABLE_DEFAULT_CAPACITY * sizeof(SymbolRow));
    if (!v.rows)
        return v;
    v.capacity = SYMBOL_TABLE_DEFAULT_CAPACITY;
    v.active = true;
    return v;
}

void SymbolTable_shutdown(SymbolTable *v) {
    if (!v || !(*v).active)
        return;
    free((*v).rows);
    free((*v).bySlot);
    (*v).rows = nullptr;
    (*v).bySlot = nullptr;
    (*v).count = 0;
    (*v).capacity = 0;
    (*v).bySlotCap = 0;
    (*v).active = false;
}

i32 SymbolTable_instant(SymbolTable *v, const char *name, u32 classId, uintptr_t targetPointer) {
    if (!v || !(*v).active) {
        fprintf(stderr, "[variable] instant: inactive registry\n");
        return -1;
    }
    if (!name || name[0] == '\0') {
        fprintf(stderr, "[variable] instant: empty name rejected\n");
        return -1;
    }
    char lowered[24];
    bytes rawLen = strlen(name);
    if (rawLen > STRING_POOL_NAME_MAX) {
        fprintf(stderr, "[variable] instant: name too long ('%.32s', max 23)\n", name);
        return -1;
    }
    if (!clean_name(name, lowered)) {
        fprintf(stderr, "[variable] instant: illegal name ('%.32s'): ascii alnum, underscore, dot only\n", name);
        return -1;
    }
    i32 slot = StringPool_find(lowered);
    if (slot >= 0 && slot_var(v, slot) >= 0) {
        fprintf(stderr, "[variable] instant: name already registered ('%s')\n", lowered);
        return -1;
    }
    if (!ensure_byslot(v, StringPool_count())) {
        fprintf(stderr, "[variable] instant: out of memory ('%s')\n", lowered);
        return -1;
    }
    slot = StringPool_intern(lowered);
    if (slot < 0) {
        fprintf(stderr, "[variable] instant: intern failed ('%s')\n", lowered);
        return -1;
    }
    if ((*v).count >= (*v).capacity) {
        bytes newCap = (bytes)(*v).capacity + SYMBOL_TABLE_DEFAULT_CAPACITY;
        SymbolRow *next = (SymbolRow*) realloc((*v).rows, newCap * sizeof(SymbolRow));
        if (!next) {
            fprintf(stderr, "[variable] instant: out of memory ('%s')\n", lowered);
            return -1;
        }
        (*v).rows = next;
        (*v).capacity = (u32) newCap;
    }
    u32 id = (*v).count;
    SymbolRow *rows = (*v).rows;
    rows[id].slot = slot;
    rows[id].classId = classId;
    rows[id].pointer = targetPointer;
    i32 *bySlot = (*v).bySlot;
    bySlot[slot] = (i32) id;
    (*v).count++;
    return (i32) id;
}

i32 SymbolTable_getId(SymbolTable *v, const char *name) {
    if (!v || !(*v).active || !name)
        return -1;
    char lowered[24];
    if (!clean_name(name, lowered))
        return -1;
    i32 slot = StringPool_find(lowered);
    if (slot < 0)
        return -1;
    return slot_var(v, slot);
}

bool SymbolTable_rename(SymbolTable *v, const char *oldName, const char *newName) {
    if (!v || !(*v).active) {
        fprintf(stderr, "[variable] rename: inactive registry\n");
        return false;
    }
    char oldLower[24];
    char newLower[24];
    if (!oldName || !clean_name(oldName, oldLower)) {
        fprintf(stderr, "[variable] rename: bad old name\n");
        return false;
    }
    if (!newName || !clean_name(newName, newLower)) {
        fprintf(stderr, "[variable] rename: bad new name\n");
        return false;
    }
    i32 oldSlot = StringPool_find(oldLower);
    i32 id = oldSlot >= 0 ? slot_var(v, oldSlot) : -1;
    if (id < 0) {
        fprintf(stderr, "[variable] rename: unknown name ('%s')\n", oldLower);
        return false;
    }
    i32 newSlot = StringPool_find(newLower);
    if (newSlot >= 0 && slot_var(v, newSlot) >= 0) {
        fprintf(stderr, "[variable] rename: name already registered ('%s')\n", newLower);
        return false;
    }
    if (!ensure_byslot(v, StringPool_count())) {
        fprintf(stderr, "[variable] rename: out of memory ('%s')\n", newLower);
        return false;
    }
    newSlot = StringPool_intern(newLower);
    if (newSlot < 0) {
        fprintf(stderr, "[variable] rename: intern failed ('%s')\n", newLower);
        return false;
    }
    SymbolRow *rows = (*v).rows;
    i32 *bySlot = (*v).bySlot;
    i32 oldSlotIdx = rows[id].slot;
    rows[id].slot = newSlot;
    if (oldSlotIdx >= 0 && (u32) oldSlotIdx < (*v).bySlotCap && bySlot[oldSlotIdx] == id)
        bySlot[oldSlotIdx] = -1;
    bySlot[newSlot] = id;
    return true;
}

bytes SymbolTable_findByClass(SymbolTable *v, u32 classId, i32 *outIds, bytes cap) {
    if (!v || !(*v).active)
        return 0;
    bytes total = 0;
    SymbolRow *rows = (*v).rows;
    for (u32 i = 0; i < (*v).count; i++) {
        if (rows[i].classId == classId) {
            if (outIds && total < cap)
                outIds[total] = (i32) i;
            total++;
        }
    }
    return total;
}

uintptr_t SymbolTable_getPointer(SymbolTable *v, i32 varId) {
    SymbolRow *row = row_at(v, varId);
    return row ? (*row).pointer : 0;
}

void SymbolTable_setPointer(SymbolTable *v, i32 varId, uintptr_t targetPointer) {
    SymbolRow *row = row_at(v, varId);
    if (!row)
        return;
    (*row).pointer = targetPointer;
}

bool SymbolTable_compareAndSetPointer(SymbolTable *v, i32 varId, uintptr_t expected, uintptr_t newPointer) {
    SymbolRow *row = row_at(v, varId);
    if (!row)
        return false;
    if ((*row).pointer != expected)
        return false;
    (*row).pointer = newPointer;
    return true;
}

u32 SymbolTable_getClassId(SymbolTable *v, i32 varId) {
    SymbolRow *row = row_at(v, varId);
    return row ? (*row).classId : 0;
}

const char *SymbolTable_getName(const SymbolTable *v, i32 varId) {
    const SymbolRow *row = row_at((SymbolTable*) v, varId);
    if (!row)
        return nullptr;
    return StringPool_name((u32) (*row).slot);
}

bytes SymbolTable_getActiveCount(SymbolTable *v) {
    if (!v || !(*v).active)
        return 0;
    return (*v).count;
}
