#ifndef STRUCT_LINKED_HASH_MAP_H
#define STRUCT_LINKED_HASH_MAP_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/map.h"

/**
 * struct/linked_hash_map.h, insertion-order-preserving LinkedHashMap
 * implementing the polymorphic Map interface.
 *
 * Combines a flat open-addressing hash index with doubly-linked arena nodes so
 * lookup/put/remove are O(1), value pointers stay stable across rehashes, and
 * Map_keys / Map_values / Map_forEach iterate in deterministic insertion order:
 *
 * Map *m = LinkedHashMap(string, Class);
 * Map *a = LinkedHashMap(u32, f32, 64, myArena);
 */

#define LINKED_HASH_MAP_DEFAULT_CAPACITY 16u

/**
 * 32-byte doubly-linked entry header preceding inline [key][val] payload bytes.
 *
 * struct LinkedHashMapNode *prev - previous entry in insertion order
 * struct LinkedHashMapNode *next - next entry in insertion order
 * u64 hash                       - cached key hash for O(1) rehash
 * u64 pad                        - padding to 32-byte payload alignment floor
 */
;;CLASSNAME("LinkedHashMapNode")
typedef struct LinkedHashMapNode {
    struct LinkedHashMapNode *prev;
    struct LinkedHashMapNode *next;
    u64 hash;
    u64 pad;
} LinkedHashMapNode;

_Static_assert(sizeof(LinkedHashMapNode) == 32u, "LinkedHashMapNode header must be 32 bytes");

/**
 * Insertion-ordered hash map embedding Map at offset zero.
 *
 * Map base                   - polymorphic Map header
 * LinkedHashMapNode **table  - open-addressing pointer index (power-of-two)
 * LinkedHashMapNode *head    - oldest entry in insertion order
 * LinkedHashMapNode *tail    - newest entry in insertion order
 * bytes valOffset            - aligned byte offset of value inside node
 * bytes nodeBytes            - total allocation size per node
 * u32 tombstones             - tombstone count in pointer index
 */
;;CLASSNAME("LinkedHashMap")
typedef struct LinkedHashMap {
    Map base;
    LinkedHashMapNode **table;
    LinkedHashMapNode *head;
    LinkedHashMapNode *tail;
    bytes valOffset;
    bytes nodeBytes;
    u32 tombstones;
} LinkedHashMap;

Map *LinkedHashMap_newLayout(ElementLayout keyLayout, ElementLayout valLayout,
                             bytes initialCapacity, MemoryArena *arena);

#define LinkedHashMap_ctor_2(K, V)             LinkedHashMap_newLayout(ElementLayout_of(K), ElementLayout_of(V), LINKED_HASH_MAP_DEFAULT_CAPACITY, Re_arena())
#define LinkedHashMap_ctor_3(K, V, cap)        LinkedHashMap_newLayout(ElementLayout_of(K), ElementLayout_of(V), (cap), Re_arena())
#define LinkedHashMap_ctor_4(K, V, cap, arena) LinkedHashMap_newLayout(ElementLayout_of(K), ElementLayout_of(V), (cap), (arena))
#define LinkedHashMap(...)                     CONSTRUCTOR_DISPATCH(LinkedHashMap_ctor, __VA_ARGS__)

bool LinkedHashMap_isLinkedHashMap(const Map *self);
any LinkedHashMap_firstKey(const Map *self);
any LinkedHashMap_lastKey(const Map *self);
any LinkedHashMap_firstValue(const Map *self);
any LinkedHashMap_lastValue(const Map *self);

bool LinkedHashMap_putBytes(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes);
any LinkedHashMap_emplaceBytes(Map *self, const_any key, bytes keyBytes, bool *outInserted);
any LinkedHashMap_getBytes(const Map *self, const_any key, bytes keyBytes);
bool LinkedHashMap_containsKeyBytes(const Map *self, const_any key, bytes keyBytes);
bool LinkedHashMap_removeBytes(Map *self, const_any key, bytes keyBytes);
fn LinkedHashMap_clear(Map *self);
bool LinkedHashMap_reserve(Map *self, bytes minCapacity);
List *LinkedHashMap_keys(const Map *self);
List *LinkedHashMap_values(const Map *self);
fn LinkedHashMap_forEach(Map *self, MapVisitFn visit, any userdata);
fn LinkedHashMap_free(Map *self);

#endif
