#ifndef STRUCT_MAP_H
#define STRUCT_MAP_H

#include "vex.h"
#include "struct/array.h"
#include "struct/collection.h"

// struct/map.h — the Map class, ported from struct/Map.java.
//
// Open-addressing hash map. Each slot is 32 Bytes: key(8) + val(8) + hash(8) +
// state(8); state is 0=empty, 1=occupied, 2=deleted. The embedded Collection
// stores key_class in element_class and val_class in stride. Reference keys
// (strings, collections, structs) are hashed/compared via their block headers.

typedef struct Map {
    Collection collection;
} Map;

// Empty map for key_class/val_class with initial slot capacity (min 4, power of two).
Map *Map_3(u32 key_class, u32 val_class, bytes capacity);

fn Map_free(Map *map);

// Insert or update key => value. Grows (rehashes) past the 0.75 load factor.
fn Map_put(Map *map, u64 key, u64 value);

// Value for key, or 0 if absent.
u64 Map_get(Map *map, u64 key);

bool Map_containsKey(Map *map, u64 key);

// Remove key; returns the old value, or 0 if absent.
u64 Map_remove(Map *map, u64 key);

// Array of all active keys (class key_class).
Array *Map_keys(Map *map);

bool Map_isEmpty(Map *map);
bytes Map_size(Map *map);
// Refused-grow count since construction (the Exhaustion Loudness Law).
;;TEST
u64 Map_exhaustionCount(const Map *map);
bytes Map_capacity(Map *map);
u32 Map_keyClassId(Map *map);
u32 Map_valClassId(Map *map);
u8 *Map_dataBuffer(Map *map);


Map *Map_2(u32 key_class, u32 val_class);

#define Map(...) CONSTRUCTOR_DISPATCH(Map, __VA_ARGS__)

#endif