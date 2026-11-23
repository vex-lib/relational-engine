/**
 * substrate/variable/variable_mini_map.c, the scoped name => pointer mini map.
 *
 * One bucket level: first folded utf8  to  a lazily-minted ChunkedList of
 * VariableSlot rows. Cold path only.
 */

#include "vex.h"
#include "substrate/variable/variable_mini_map.h"

#include <stdio.h>
#include <string.h>

#include "lang/annotation.h"
#include "nio/mem.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VariableMiniMap
 * ============================================================================
 * The scoped sibling of VariableHashMap: the same first-character bucketing with
 * a single level, for names that belong to one owner (a class's fields or
 * methods). 39 buckets, each a lazily-minted ChunkedList of VariableSlot rows
 * with 16 rows per leaf, so an empty mini map is 39 pointers and a used one
 * costs one small leaf per active first character. Names fold and validate
 * through the atom, dotted field paths included.
 *
 * Lifetime: an arena object (or embedded); shutdown frees every bucket list. No
 * removal verb (a symbol is rebound, like the big map).
 *
 * Path temperature: cold. Read by the reflective scoped search, never on a frame
 * (the Cold-Only Reflection Law). Getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VariableMiniMap (substrate/variable/variable_mini_map.c)
 * ============================================================================
 * the scoped name => pointer mini map (39 single-level buckets).
 *
 * STRUCT FIELDS (Mirroring substrate/variable/variable_mini_map.h):
 * ----------------------------------------------------------------------------
 *   VariableMiniMap {
 *     bool active;      // runtime-active flag
 *     u32 count;   // live entries
 *     ChunkedList *buckets[39]; // first-utf8  to  lazily minted slot list
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VariableMiniMap_0()                    : arena-allocated empty map
 *   - VariableMiniMap_init(map)              : embedded init
 *   - VariableMiniMap_free(map)              : shutdown + release an arena map
 *   - VariableMiniMap_shutdown(map)          : free every bucket list
 *
 * Private Core Functions: (.c static)
 *   - ensureBucket(map, bucket)              : mint a bucket list lazily
 *   - findRow(list, name)                    : name compare inside a bucket
 *
 * Public Core Functions: (.h)
 *   - VariableMiniMap_add(map, name, pointer)
 *   - VariableMiniMap_get(map, name): resolve to a returned pointer value
 *   - VariableMiniMap_contains(map, name)
 *   - VariableMiniMap_forEach(map, fn, userdata)
 *
 * Public Getters: (.h)
 *   - VariableMiniMap_count(map)
 *   - VariableMiniMap_isEmpty(map)
 *
 * Public String Projections: (.h)
 *   - VariableMiniMap_toString(map, dest, cap, outTruncated)
 *   - VariableMiniMap_toStringStruct(map, dest, cap, outTruncated)
 * ============================================================================
 */

/** Find a row by folded name inside one bucket list. Null on a hole or a miss. */
static VariableSlot *findRow(ChunkedList *list, string name) {
    u32 rows = ChunkedList_size(list);

    for (u32 i = 0; i < rows; i++) {
        VariableSlot *row = (VariableSlot*) ChunkedList_slot(list, i);

        if (row && VariableSlot_nameEquals(row, name)) 
            return row;
    }

    return nullptr;
}

;;GETTER
static inline bool VariableMiniMap_active(const VariableMiniMap *map)
{
    return (*map).active;
}
;;SETTER
static inline fn VariableMiniMap_setActive(VariableMiniMap *map, bool active)
{
    (*map).active = active;
}
;;SETTER
static inline fn VariableMiniMap_setCount(VariableMiniMap *map, u32 count)
{
    (*map).count = count;
}
;;GETTER
static inline ChunkedList *VariableMiniMap_bucketAt(const VariableMiniMap *map, u32 b)
{
    return (*map).buckets[b];
}
;;SETTER
static inline fn VariableMiniMap_setBucketAt(VariableMiniMap *map, u32 b, ChunkedList *l)
{
    (*map).buckets[b] = l;
}

/** Mint a bucket's list on first use: one root slot, 16-row leaves. */
static ChunkedList *ensureBucket(VariableMiniMap *map, u32 bucket) {
    ChunkedList *list = VariableMiniMap_bucketAt(map, bucket);

    if (list) 
        return list;

    list = ChunkedList(ID_VARIABLE_SLOT, VARIABLE_SLOT_SIZE, VARIABLE_MINI_ROOT_INIT, VARIABLE_MINI_LEAF_ROWS);

    if (!list) 
        return nullptr;

    VariableMiniMap_setBucketAt(map, bucket, list);

    return list;
}

/** CONSTRUCTORS */

bool VariableMiniMap_init(VariableMiniMap *map) {
    if (!map) 
        return false;

    memset(map, 0, sizeof(*map));

    VariableMiniMap_setActive(map, true);

    return true;
}

fn VariableMiniMap_shutdown(VariableMiniMap *map) {
    if (!map) 
        return;

    for (u32 b = 0u; b < VARIABLE_MINI_BUCKETS; b++) {
        ChunkedList *list = VariableMiniMap_bucketAt(map, b);

        if (list) 
            ChunkedList_free(list);

        VariableMiniMap_setBucketAt(map, b, nullptr);
    }

    VariableMiniMap_setCount(map, 0u);

    VariableMiniMap_setActive(map, false);
}

VariableMiniMap *VariableMiniMap_0() {
    VariableMiniMap *map = (VariableMiniMap*) Memory_alloc(TYPE_VARIABLE_MINI_MAP, Re_arena(), sizeof(VariableMiniMap));

    if (!map) 
        return nullptr;

    if (!VariableMiniMap_init(map)) {
        Memory_free(map);

        return nullptr;
    }

    return map;
}

fn VariableMiniMap_free(VariableMiniMap *map) {
    if (!map) 
        return;

    VariableMiniMap_shutdown(map);

    Memory_free(map);
}

/** CORE FUNCTIONS */

bool VariableMiniMap_add(VariableMiniMap *map, string name, pointer pointer) {
    if (!map || !VariableMiniMap_active(map)) 
        return false;

    utf8 folded[VARIABLE_SLOT_NAME_BYTES];

    if (!VariableSlot_foldName(name, folded)) 
        return false;

    i32 bucket = VariableSlot_bucketOf(folded[0]);

    if (bucket < 0) 
        return false;

    ChunkedList *list = ensureBucket(map, (u32) bucket);

    if (!list) 
        return false;

    VariableSlot *row = findRow(list, name);

    if (row) {
        VariableSlot_setPointer(row, pointer);

        return true;
    }

    VariableSlot *fresh = (VariableSlot*) ChunkedList_addSlot(list);

    if (!fresh) 
        return false;

    if (!VariableSlot_init(fresh, name, pointer)) 
        return false;

    VariableMiniMap_setCount(map, VariableMiniMap_count(map) + 1u);

    return true;
}

pointer VariableMiniMap_get(const VariableMiniMap *map, string name) {
    if (!map || !VariableMiniMap_active(map)) 
        return 0u;

    utf8 folded[VARIABLE_SLOT_NAME_BYTES];

    if (!VariableSlot_foldName(name, folded)) 
        return 0u;

    i32 bucket = VariableSlot_bucketOf(folded[0]);

    if (bucket < 0) 
        return 0u;

    ChunkedList *list = VariableMiniMap_bucketAt(map, (u32) bucket);

    if (!list) 
        return 0u;

    VariableSlot *row = findRow(list, name);

    if (!row) 
        return 0u;

    return VariableSlot_getPointer(row);
}

bool VariableMiniMap_contains(const VariableMiniMap *map, string name) {
    if (!map || !VariableMiniMap_active(map)) 
        return false;

    utf8 folded[VARIABLE_SLOT_NAME_BYTES];

    if (!VariableSlot_foldName(name, folded)) 
        return false;

    i32 bucket = VariableSlot_bucketOf(folded[0]);

    if (bucket < 0) 
        return false;

    ChunkedList *list = VariableMiniMap_bucketAt(map, (u32) bucket);

    return list && findRow(list, name) != nullptr;
}

fn VariableMiniMap_forEach(VariableMiniMap *map, VariableMiniMapVisitFn fn, any userdata) {
    if (!map || !VariableMiniMap_active(map) || !fn) 
        return;

    for (u32 b = 0u; b < VARIABLE_MINI_BUCKETS; b++) {
        ChunkedList *list = VariableMiniMap_bucketAt(map, b);

        if (!list) 
            continue;

        u32 rows = ChunkedList_size(list);

        for (u32 i = 0u; i < rows; i++) {
            VariableSlot *row = (VariableSlot*) ChunkedList_slot(list, i);

            if (row) 
                fn(row, userdata);
        }
    }
}

/** GETTERS */

u32 VariableMiniMap_count(const VariableMiniMap *map) {
    return map ? (*map).count : 0u;
}

bool VariableMiniMap_isEmpty(const VariableMiniMap *map) {
    if (!map) 
        return true;

    return VariableMiniMap_count(map) == 0u;
}

/** STRING PROJECTIONS (the toString Law) */

fn VariableMiniMap_toString(const VariableMiniMap *map, utf8 *dest, bytes cap, bool *outTruncated) {
    if (outTruncated) 
        *outTruncated = false;

    if (!dest || cap == 0u) 
        return;

    if (!map) {
        snprintf(dest, cap, "nullptr");

        return;
    }

    i32 written = snprintf(dest, cap, "VariableMiniMap(count=%u)", VariableMiniMap_count(map));

    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated) 
            *outTruncated = true;
    }
}

fn VariableMiniMap_toStringStruct(const VariableMiniMap *map, utf8 *dest, bytes cap, bool *outTruncated) {
    if (outTruncated) 
        *outTruncated = false;

    if (!dest || cap == 0u) 
        return;

    if (!map) {
        snprintf(dest, cap, "nullptr");

        return;
    }

    i32 written = snprintf(dest, cap, "VariableMiniMap { active=%s, count=%u, buckets=<array[%u]> }",
                           VariableMiniMap_active(map) ? "true" : "false", VariableMiniMap_count(map), VARIABLE_MINI_BUCKETS);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated) 
            *outTruncated = true;
    }
}
