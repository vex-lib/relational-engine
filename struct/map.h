#ifndef STRUCT_MAP_H
#define STRUCT_MAP_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"
#include "struct/list.h"

/**
 * struct/map.h, the polymorphic Map interface.
 *
 * Concrete map backings (HashMap, LinkedHashMap) embed Map at offset zero and
 * register a MapOps vtable. Construct with any C key and value types directly:
 *
 * Map *m = HashMap(string, Class);
 * Map *o = LinkedHashMap(u32, Class);
 * Map_put(m, "hero", myClass);
 * Class *c = Map_get(m, "hero");
 * u32 v = Map_getAs(counts, "hero", u32);
 * Map_free(m);
 */

/**
 * Concrete Map backing discriminator stored in MapOps.
 *
 * MAP_KIND_UNKNOWN     - uninitialized or invalid backing
 * MAP_KIND_HASH        - flat open-addressing array table (HashMap)
 * MAP_KIND_LINKED_HASH - insertion-ordered doubly-linked hash map (LinkedHashMap)
 */
;;CLASSNAME("MapKind")
typedef enum MapKind {
    MAP_KIND_UNKNOWN = 0,
    MAP_KIND_HASH = 1,
    MAP_KIND_LINKED_HASH = 2,
} MapKind;

typedef struct Map Map;

typedef bool (*MapPutBytesFn)(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes);
typedef any (*MapEmplaceBytesFn)(Map *self, const_any key, bytes keyBytes, bool *outInserted);
typedef any (*MapGetBytesFn)(const Map *self, const_any key, bytes keyBytes);
typedef bool (*MapContainsBytesFn)(const Map *self, const_any key, bytes keyBytes);
typedef bool (*MapRemoveBytesFn)(Map *self, const_any key, bytes keyBytes);
typedef fn (*MapClearFn)(Map *self);
typedef bool (*MapReserveFn)(Map *self, bytes minCapacity);
typedef List *(*MapKeysFn)(const Map *self);
typedef List *(*MapValuesFn)(const Map *self);
typedef fn (*MapVisitFn)(const_any key, any value, bytes index, any userdata);
typedef fn (*MapForEachFn)(Map *self, MapVisitFn visit, any userdata);
typedef bytes (*MapCountFn)(const Map *self);
typedef bytes (*MapCapacityFn)(const Map *self);
typedef fn (*MapDestroyFn)(Map *self);

/**
 * Polymorphic dispatch table implemented by every concrete Map backing.
 *
 * MapKind kind                      - concrete backing discriminator
 * string kindName                   - human-readable backing name
 * MapPutBytesFn putBytes            - insert or update key/value pair
 * MapEmplaceBytesFn emplaceBytes    - insert zeroed value if absent, return value slot
 * MapGetBytesFn getBytes            - lookup pointer to value slot or nullptr
 * MapContainsBytesFn containsBytes  - key presence predicate
 * MapRemoveBytesFn removeBytes      - remove entry by key
 * MapClearFn clear                  - remove all entries
 * MapReserveFn reserve              - ensure table capacity for minCapacity entries
 * MapKeysFn keys                    - export all keys as an ArrayList
 * MapValuesFn values                - export all values as an ArrayList
 * MapForEachFn forEach              - visit every entry in backing order
 * MapCountFn count                  - active entry count
 * MapCapacityFn capacity            - slot table capacity
 * MapDestroyFn destroy              - release table and handle
 */
;;CLASSNAME("MapOps")
typedef struct MapOps {
    MapKind kind;
    string kindName;
    MapPutBytesFn putBytes;
    MapEmplaceBytesFn emplaceBytes;
    MapGetBytesFn getBytes;
    MapContainsBytesFn containsBytes;
    MapRemoveBytesFn removeBytes;
    MapClearFn clear;
    MapReserveFn reserve;
    MapKeysFn keys;
    MapValuesFn values;
    MapForEachFn forEach;
    MapCountFn count;
    MapCapacityFn capacity;
    MapDestroyFn destroy;
} MapOps;

/**
 * Polymorphic Map interface header embedded at offset zero of every Map backing.
 *
 * Collection collection   - embedded Collection header (offset 0)
 * const MapOps *ops       - borrowed vtable describing the concrete backing
 * MemoryArena *arena      - borrowed MemoryArena owning allocations
 * ElementLayout keyLayout - key type layout (including string content-hash mode)
 * ElementLayout valLayout - value type layout
 */
;;CLASSNAME("Map")
typedef struct Map {
    Collection collection;
    const MapOps *ops;
    MemoryArena *arena;
    ElementLayout keyLayout;
    ElementLayout valLayout;
} Map;

bool Map_initBase(Map *self, u64 typeId, const MapOps *ops,
                  ElementLayout keyLayout, ElementLayout valLayout, MemoryArena *arena);

bool Map_putBytes(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes);
any Map_emplaceBytes(Map *self, const_any key, bytes keyBytes, bool *outInserted);
any Map_getBytes(const Map *self, const_any key, bytes keyBytes);
bool Map_copyOut(const Map *self, const_any key, bytes keyBytes,
                 any dest, bytes valBytes, string valTypeName);
bool Map_containsKeyBytes(const Map *self, const_any key, bytes keyBytes);
bool Map_removeBytes(Map *self, const_any key, bytes keyBytes);
fn Map_clear(Map *self);
bool Map_reserve(Map *self, bytes minCapacity);
List *Map_keys(const Map *self);
List *Map_values(const Map *self);
fn Map_forEach(Map *self, MapVisitFn visit, any userdata);
fn Map_free(Map *self);

bytes Map_count(const Map *self);
bytes Map_size(const Map *self);
bytes Map_capacity(const Map *self);
bool Map_isEmpty(const Map *self);
bytes Map_keyStride(const Map *self);
bytes Map_valStride(const Map *self);
string Map_keyTypeName(const Map *self);
string Map_valTypeName(const Map *self);
MapKind Map_kind(const Map *self);
string Map_kindName(const Map *self);
MemoryArena *Map_arena(const Map *self);
const MapOps *Map_ops(const Map *self);
ElementLayout Map_keyLayout(const Map *self);
ElementLayout Map_valLayout(const Map *self);

;;TEST
u64 Map_exhaustionCount(const Map *self);

fn Map_toString(const Map *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Map_toStringStruct(const Map *self, utf8 *dest, bytes cap, bool *outTruncated);

fn MapOps_setKind(MapOps *ops, MapKind kind);
fn MapOps_setKindName(MapOps *ops, string kindName);
fn MapOps_setPutBytes(MapOps *ops, MapPutBytesFn fnPtr);
fn MapOps_setEmplaceBytes(MapOps *ops, MapEmplaceBytesFn fnPtr);
fn MapOps_setGetBytes(MapOps *ops, MapGetBytesFn fnPtr);
fn MapOps_setContainsBytes(MapOps *ops, MapContainsBytesFn fnPtr);
fn MapOps_setRemoveBytes(MapOps *ops, MapRemoveBytesFn fnPtr);
fn MapOps_setClear(MapOps *ops, MapClearFn fnPtr);
fn MapOps_setReserve(MapOps *ops, MapReserveFn fnPtr);
fn MapOps_setKeys(MapOps *ops, MapKeysFn fnPtr);
fn MapOps_setValues(MapOps *ops, MapValuesFn fnPtr);
fn MapOps_setForEach(MapOps *ops, MapForEachFn fnPtr);
fn MapOps_setCount(MapOps *ops, MapCountFn fnPtr);
fn MapOps_setCapacity(MapOps *ops, MapCapacityFn fnPtr);
fn MapOps_setDestroy(MapOps *ops, MapDestroyFn fnPtr);

#define Map_put(map, key, val)                                               \
    ({                                                                       \
        auto _vex_map_put_k = (key);                                         \
        auto _vex_map_put_v = (val);                                         \
        Map_putBytes((map), &_vex_map_put_k, sizeof(_vex_map_put_k),         \
                     &_vex_map_put_v, sizeof(_vex_map_put_v));               \
    })

#define Map_emplace(map, key)                                                \
    ({                                                                       \
        auto _vex_map_emp_k = (key);                                         \
        Map_emplaceBytes((map), &_vex_map_emp_k, sizeof(_vex_map_emp_k),     \
                         nullptr);                                           \
    })

#define Map_get(map, key)                                                    \
    ({                                                                       \
        auto _vex_map_get_k = (key);                                         \
        Map_getBytes((map), &_vex_map_get_k, sizeof(_vex_map_get_k));        \
    })

#define Map_getAs(map, key, V)                                               \
    ({                                                                       \
        auto _vex_map_ga_k = (key);                                          \
        typeof_unqual(V) _vex_map_ga_out;                                    \
        memset(&_vex_map_ga_out, 0, sizeof(_vex_map_ga_out));                \
        (discard) Map_copyOut((map), &_vex_map_ga_k, sizeof(_vex_map_ga_k),  \
                              &_vex_map_ga_out, sizeof(_vex_map_ga_out),     \
                              #V);                                           \
        _vex_map_ga_out;                                                     \
    })

#define Map_containsKey(map, key)                                            \
    ({                                                                       \
        auto _vex_map_has_k = (key);                                         \
        Map_containsKeyBytes((map), &_vex_map_has_k,                         \
                             sizeof(_vex_map_has_k));                        \
    })

#define Map_remove(map, key)                                                 \
    ({                                                                       \
        auto _vex_map_rem_k = (key);                                         \
        Map_removeBytes((map), &_vex_map_rem_k, sizeof(_vex_map_rem_k));     \
    })

#endif
