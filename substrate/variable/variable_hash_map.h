#ifndef RELATIONAL_VARIABLE_HASH_MAP_H
#define RELATIONAL_VARIABLE_HASH_MAP_H

#include "vex.h"
#include "lang/constructor.h"
#include "substrate/variable/variable_slot.h"
#include "struct/chunked_list.h"

// substrate/variable/variable_hash_map.h — the relational name => pointer hash map.
//
// Hash system B of the Relational Engine (the manifesto's two-hash design; the
// other is Hash_pointer over a whole pointer). Keys are variable names, values
// are uintptr_ts. A name is folded to lowercase over the 39-character charset
// [a-z0-9_$-] before anything else (capitals are allowed and fold away; the dot
// is the path splitter and is never a name character — the atom's policy,
// shared through VariableSlot_foldName).
//
// Two-level index, straight from the manifesto:
//
//     bucket = first folded char        -> one of 39 selections (a-z 0-9 _ $ -)
//     slot   = Murmur3Mix64(FNV1a(tail)) & 1023  -> 1024 slots per selection
//
// The tail is the folded name minus its first character ("helloWorld" ->
// "helloworld" -> bucket [h], dashcode over "elloworld"). Each (bucket, slot)
// is a LAZILY-MINTED ChunkedList of VariableSlot rows, so a slot grows without a
// ceiling and a row's address never moves; a collision inside a slot resolves by
// a full 24-byte name compare. Bands (the 1024-slot arrays) are minted lazily
// too, so an empty map costs 39 pointers.
//
// COLD PATH ONLY: this is the relational rendezvous — search, debug, script,
// save/load, hot-swap (the Cold-Only Reflection Law). Never a per-frame walk.

#define VARIABLE_HASH_BUCKETS 39u
#define VARIABLE_HASH_SLOTS 1024u
#define VARIABLE_HASH_SLOTS_MASK (VARIABLE_HASH_SLOTS - 1u)

// Per-slot leaf geometry: a slot almost always holds one entry, so the minted
// list starts tiny (one leaf of VARIABLE_HASH_SLOT_LEAF rows, root of one slot)
// and grows by adding leaves on collision.
#define VARIABLE_HASH_SLOT_ROOT 1u
#define VARIABLE_HASH_SLOT_LEAF 4u

typedef struct VariableHashMap {
    bool active;                                // runtime-active flag
    u32 count;                             // live entries
    ChunkedList **bands[VARIABLE_HASH_BUCKETS]; // first-char -> 1024 lazy slot lists (null until used)
} VariableHashMap;

bool VariableHashMap_init(VariableHashMap *map);

fn VariableHashMap_shutdown(VariableHashMap *map);

VariableHashMap *VariableHashMap_0();

#define VariableHashMap(...) CONSTRUCTOR_DISPATCH(VariableHashMap, __VA_ARGS__)

fn VariableHashMap_free(VariableHashMap *map);

bool VariableHashMap_add(VariableHashMap *map, const char *name, pointer pointer);

pointer VariableHashMap_get(const VariableHashMap *map, const char *name);

bool VariableHashMap_contains(const VariableHashMap *map, const char *name);

// Visit every live entry (cold iteration; bands and slots spelled in order).
// The visitor receives the row (name + pointer); it must not mutate the map. A
// null map/fn is a no-op.
typedef fn (*VariableHashMapVisitFn)(VariableSlot *slot, any userdata);
fn VariableHashMap_forEach(VariableHashMap *map, VariableHashMapVisitFn fn, any userdata);

u32 VariableHashMap_count(const VariableHashMap *map);
bool VariableHashMap_isEmpty(const VariableHashMap *map);

fn VariableHashMap_toString(const VariableHashMap *map, char *dest, bytes cap, bool *outTruncated);
fn VariableHashMap_toStringStruct(const VariableHashMap *map, char *dest, bytes cap, bool *outTruncated);

#endif
