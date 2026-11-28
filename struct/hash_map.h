#ifndef STRUCT_HASH_MAP_H
#define STRUCT_HASH_MAP_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/map.h"

/**
 * struct/hash_map.h, flat open-addressing array-backed HashMap (default Map).
 *
 * Stores hash, state, key, and value inline inside one contiguous 32-byte-aligned
 * arena slot array. Construct with any C key and value types directly:
 *
 * Map *m = HashMap(string, Class);
 * Map *n = HashMap(u32, f32, 64);
 * Map *a = HashMap(u64, Entity, 128, myArena);
 */

#define HASH_MAP_DEFAULT_CAPACITY 16u

/**
 * Flat array-backed open-addressing HashMap embedding Map at offset zero.
 *
 * Map base         - polymorphic Map header and Collection table buffer
 * bytes keyOffset  - aligned byte offset of inline key inside one slot
 * bytes valOffset  - aligned byte offset of inline value inside one slot
 * bytes slotStride - aligned total byte width of one slot
 * u32 tombstones   - number of DELETED slots currently occupying probe chains
 */
;;CLASSNAME("HashMap")
typedef struct HashMap {
    Map base;
    bytes keyOffset;
    bytes valOffset;
    bytes slotStride;
    u32 tombstones;
} HashMap;

Map *HashMap_newLayout(ElementLayout keyLayout, ElementLayout valLayout,
                       bytes initialCapacity, MemoryArena *arena);

#define HashMap_ctor_2(K, V)             HashMap_newLayout(ElementLayout_of(K), ElementLayout_of(V), HASH_MAP_DEFAULT_CAPACITY, Re_arena())
#define HashMap_ctor_3(K, V, cap)        HashMap_newLayout(ElementLayout_of(K), ElementLayout_of(V), (cap), Re_arena())
#define HashMap_ctor_4(K, V, cap, arena) HashMap_newLayout(ElementLayout_of(K), ElementLayout_of(V), (cap), (arena))
#define HashMap(...)                     CONSTRUCTOR_DISPATCH(HashMap_ctor, __VA_ARGS__)

#define Map(...)                         HashMap(__VA_ARGS__)

bool HashMap_isHashMap(const Map *self);
bool HashMap_putBytes(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes);
any HashMap_emplaceBytes(Map *self, const_any key, bytes keyBytes, bool *outInserted);
any HashMap_getBytes(const Map *self, const_any key, bytes keyBytes);
bool HashMap_containsKeyBytes(const Map *self, const_any key, bytes keyBytes);
bool HashMap_removeBytes(Map *self, const_any key, bytes keyBytes);
fn HashMap_clear(Map *self);
bool HashMap_reserve(Map *self, bytes minCapacity);
List *HashMap_keys(const Map *self);
List *HashMap_values(const Map *self);
fn HashMap_forEach(Map *self, MapVisitFn visit, any userdata);
fn HashMap_free(Map *self);

#define HashMap_put(map, key, val)                                               \
    ({                                                                           \
        auto _vex_hm_put_k = (key);                                              \
        auto _vex_hm_put_v = (val);                                              \
        HashMap_putBytes((map), &_vex_hm_put_k, sizeof(_vex_hm_put_k),           \
                         &_vex_hm_put_v, sizeof(_vex_hm_put_v));                 \
    })

#define HashMap_get(map, key)                                                    \
    ({                                                                           \
        auto _vex_hm_get_k = (key);                                              \
        HashMap_getBytes((map), &_vex_hm_get_k, sizeof(_vex_hm_get_k));          \
    })

#define HashMap_getAs(map, key, V)       Map_getAs((map), (key), V)
#define HashMap_containsKey(map, key)    Map_containsKey((map), (key))
#define HashMap_remove(map, key)         Map_remove((map), (key))

#endif
