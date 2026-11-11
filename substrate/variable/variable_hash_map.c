// substrate/variable/variable_hash_map.c — the relational name => pointer hash map.
//
// Two-level index (bucket = first folded char, slot = Murmur3(FNV1a(tail)) &
// 1023); each occupied slot is a lazily-minted ChunkedList of VariableSlot rows.
// Bands are minted lazily too. Cold path only.

#include "substrate/variable/variable_hash_map.h"

#include <stdio.h>
#include <string.h>

#include "lang/annotation.h"
#include "nio/mem.h"
#include "type/type.h"
#include "substrate/variable/variable_slot.h"
#include "vexspoke/util/hash.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VariableHashMap
 * ============================================================================
 * Hash system B of the Relational Engine: a name => pointer map whose key is a
 * variable name. The name is folded to lowercase over the 39-char charset
 * [a-z0-9_$-] (the atom's policy, VariableSlot_foldName), the first character
 * selects one of 39 buckets, and Murmur3Mix64(FNV1a(tail)) & 1023 selects one
 * of 1024 slots inside that bucket. Each (bucket, slot) is a lazily-minted
 * ChunkedList of VariableSlot rows, so a slot grows without a ceiling and rows
 * never move; collisions resolve by a full 24-byte name compare.
 *
 * memory: bands (1024-slot arrays) are minted on first use of a first-char, so
 * an empty map costs 39 pointers. A slot list starts tiny (root of one slot,
 * one leaf of VARIABLE_HASH_SLOT_LEAF rows) and grows by adding leaves. All
 * backing is arena memory.
 *
 * Path temperature: cold. The map is the relational rendezvous — search, debug,
 * script, save/load, hot-swap — never a per-frame walk (the Cold-Only
 * Reflection Law). No removal verb: like the Variable registry, a symbol is
 * rebound, not unregistered (a chunked list keeps rows never-moved and
 * hole-free by design).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VariableHashMap (substrate/variable/variable_hash_map.c)
 * ============================================================================
 * the relational name => pointer hash map (hash system B).
 *
 * STRUCT FIELDS (Mirroring substrate/variable/variable_hash_map.h):
 * ----------------------------------------------------------------------------
 *   VariableHashMap {
 *     bool active;                                // runtime-active flag
 *     u32 count;                             // live entries
 *     ChunkedList **bands[39];                    // first-char -> 1024 lazy slot lists
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own):
 * ----------------------------------------------------------------------------
 *   (none — the fold/index helpers are static functions below, not records)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VariableHashMap_0()                       : arena-allocated empty map
 *   - VariableHashMap_init(map)                 : embedded init (zero the bands)
 *   - VariableHashMap_free(map)                 : shutdown + release an arena map
 *   - VariableHashMap_shutdown(map)             : release bands + slot lists
 *
 * Private Core Functions: (.c static)
 *   - bucketOf(c)                               : first-char -> 0..38
 *   - locate(name, outBucket, outSlot, outFolded): fold + two-level index
 *   - ensureBand(map, bucket)                   : mint a 1024-slot band lazily
 *   - ensureSlot(band, slot)                    : mint a slot list lazily
 *   - findRow(list, name)                       : name compare inside a slot
 *
 * Public Core Functions: (.h)
 *   - VariableHashMap_add(map, name, pointer)   : insert or update
 *   - VariableHashMap_get(map, name): resolve to a returned pointer value
 *   - VariableHashMap_contains(map, name)       : probe (silent miss)
 *   - VariableHashMap_forEach(map, fn, userdata): cold iteration
 *
 * Public Getters: (.h)
 *   - VariableHashMap_count(map)
 *   - VariableHashMap_isEmpty(map)
 *
 * Public String Projections: (.h)
 *   - VariableHashMap_toString(map, dest, cap, outTruncated)
 *   - VariableHashMap_toStringStruct(map, dest, cap, outTruncated)
 * ============================================================================
 */

// Fold a name and compute its two-level index. outFolded must hold
// VARIABLE_SLOT_NAME_BYTES. False on an invalid name.
static bool locate(const char *name, u32 *outBucket, u32 *outSlot, char *outFolded) {
    if (!VariableSlot_foldName(name, outFolded))
        return false;
    int bucket = VariableSlot_bucketOf(outFolded[0]);
    if (bucket < 0)
        return false;
    const char *tail = outFolded + 1;
    u64 seed = Hash_fnv1a64((const u8*) tail, strlen(tail));
    *outBucket = (u32) bucket;
    *outSlot = (u32)(Hash_murmur3Mix64(seed) & (u64) VARIABLE_HASH_SLOTS_MASK);
    return true;
}

// Mint a band (the 1024 slot pointers for one first-char) on first use.
static ChunkedList **ensureBand(VariableHashMap *map, u32 bucket) {
    ChunkedList **band = (*map).bands[bucket];
    if (band)
        return band;
    bytes Bytes = (bytes) VARIABLE_HASH_SLOTS * sizeof(ChunkedList*);
    band = (ChunkedList**) Memory_alloc(TYPE_VARIABLE_HASH_MAP, Bytes);
    if (!band)
        return nullptr;
    memset(band, 0, Bytes);
    (*map).bands[bucket] = band;
    return band;
}

// Mint a slot's list on first use (tiny root, small leaf; grows on collision).
static ChunkedList *ensureSlot(ChunkedList **band, u32 slot) {
    ChunkedList *list = band[slot];
    if (list)
        return list;
    list = ChunkedList(ID_VARIABLE_SLOT, VARIABLE_SLOT_SIZE, VARIABLE_HASH_SLOT_ROOT, VARIABLE_HASH_SLOT_LEAF);
    if (!list)
        return nullptr;
    band[slot] = list;
    return list;
}

// Find a row by folded name inside one slot's list. Null on a hole or a miss.
static VariableSlot *findRow(ChunkedList *list, const char *name) {
    u32 rows = ChunkedList_size(list);
    for (u32 i = 0; i < rows; i++) {
        VariableSlot *row = (VariableSlot*) ChunkedList_slot(list, i);
        if (row && VariableSlot_nameEquals(row, name))
            return row;
    }
    return nullptr;
}

// CONSTRUCTORS

bool VariableHashMap_init(VariableHashMap *map) {
    if (!map)
        return false;
    memset(map, 0, sizeof(*map));
    (*map).active = true;
    return true;
}

void VariableHashMap_shutdown(VariableHashMap *map) {
    if (!map)
        return;
    for (u32 b = 0; b < VARIABLE_HASH_BUCKETS; b++) {
        ChunkedList **band = (*map).bands[b];
        if (!band)
            continue;
        for (u32 s = 0; s < VARIABLE_HASH_SLOTS; s++) {
            ChunkedList *list = band[s];
            if (list)
                ChunkedList_free(list);
        }
        Memory_free(band);
        (*map).bands[b] = nullptr;
    }
    (*map).count = 0u;
    (*map).active = false;
}

VariableHashMap *VariableHashMap_0(void) {
    VariableHashMap *map = (VariableHashMap*) Memory_alloc(TYPE_VARIABLE_HASH_MAP, sizeof(VariableHashMap));
    if (!map)
        return nullptr;
    if (!VariableHashMap_init(map)) {
        Memory_free(map);
        return nullptr;
    }
    return map;
}

void VariableHashMap_free(VariableHashMap *map) {
    if (!map)
        return;
    VariableHashMap_shutdown(map);
    Memory_free(map);
}

// CORE FUNCTIONS

bool VariableHashMap_add(VariableHashMap *map, const char *name, uintptr_t pointer) {
    if (!map || !(*map).active)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    u32 bucket = 0u;
    u32 slot = 0u;
    if (!locate(name, &bucket, &slot, folded))
        return false;
    ChunkedList **band = ensureBand(map, bucket);
    if (!band)
        return false;
    ChunkedList *list = ensureSlot(band, slot);
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
    (*map).count++;
    return true;
}

uintptr_t VariableHashMap_get(const VariableHashMap *map, const char *name) {
    if (!map || !(*map).active)
        return 0u;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    u32 bucket = 0u;
    u32 slot = 0u;
    if (!locate(name, &bucket, &slot, folded))
        return 0u;
    ChunkedList **band = (*map).bands[bucket];
    if (!band)
        return 0u;
    ChunkedList *list = band[slot];
    if (!list)
        return 0u;
    VariableSlot *row = findRow(list, name);
    if (!row)
        return 0u;
    return VariableSlot_getPointer(row);
}

bool VariableHashMap_contains(const VariableHashMap *map, const char *name) {
    if (!map || !(*map).active)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    u32 bucket = 0u;
    u32 slot = 0u;
    if (!locate(name, &bucket, &slot, folded))
        return false;
    ChunkedList **band = (*map).bands[bucket];
    if (!band || !band[slot])
        return false;
    return findRow(band[slot], name) != nullptr;
}

void VariableHashMap_forEach(VariableHashMap *map, VariableHashMapVisitFn fn, void *userdata) {
    if (!map || !(*map).active || !fn)
        return;
    for (u32 b = 0u; b < VARIABLE_HASH_BUCKETS; b++) {
        ChunkedList **band = (*map).bands[b];
        if (!band)
            continue;
        for (u32 s = 0u; s < VARIABLE_HASH_SLOTS; s++) {
            ChunkedList *list = band[s];
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
}

// GETTERS

u32 VariableHashMap_count(const VariableHashMap *map) {
    return map ? (*map).count : 0u;
}

bool VariableHashMap_isEmpty(const VariableHashMap *map) {
    if (!map)
        return true;
    return (*map).count == 0u;
}

// STRING PROJECTIONS (the toString Law)

void VariableHashMap_toString(const VariableHashMap *map, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!map) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "VariableHashMap(count=%u)", (*map).count);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void VariableHashMap_toStringStruct(const VariableHashMap *map, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!map) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "VariableHashMap { active=%s, count=%u, bands=<array[%u]> }",
                           (*map).active ? "true" : "false", (*map).count, VARIABLE_HASH_BUCKETS);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
