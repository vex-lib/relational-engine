#ifndef STRUCT_ARRAY_LIST_H
#define STRUCT_ARRAY_LIST_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/list.h"

/**
 * struct/array_list.h, the contiguous DOD ArrayList implementation of List.
 *
 * Stores elements inline in one 32-byte-aligned contiguous arena buffer that
 * doubles geometrically on growth. Construct with any C type directly:
 *
 * List *x = ArrayList(Class);
 * List *y = ArrayList(u32, 64);
 * List *z = ArrayList(Vec4, 128, myArena);
 *
 * All polymorphic List_* operations work on x; ArrayList_* operations provide
 * direct non-vtable access plus array-specific primitives (data, swapRemove,
 * shrinkToFit).
 */

#define ARRAY_LIST_DEFAULT_CAPACITY 16u

/**
 * Contiguous growable list embedding the List interface at offset zero.
 *
 * List base - polymorphic List header and Collection backing buffer metadata
 */
;;CLASSNAME("ArrayList")
typedef struct ArrayList {
    List base;
} ArrayList;

/**
 * Constructs a new ArrayList for layout with initialCapacity slots in arena.
 * @param layout          validated element layout (use ElementLayout_of(T))
 * @param initialCapacity initial slot count (0 allocates lazily on first push)
 * @param arena           backing MemoryArena (nullptr defaults to Re_arena())
 * @return owned List* handle backed by ArrayList, or nullptr on refusal
 */
List *ArrayList_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena);

#define ArrayList_1(T)             ArrayList_newLayout(ElementLayout_of(T), ARRAY_LIST_DEFAULT_CAPACITY, Re_arena())
#define ArrayList_2(T, cap)        ArrayList_newLayout(ElementLayout_of(T), (cap), Re_arena())
#define ArrayList_3(T, cap, arena) ArrayList_newLayout(ElementLayout_of(T), (cap), (arena))
#define ArrayList(...)             CONSTRUCTOR_DISPATCH(ArrayList, __VA_ARGS__)

/**
 * Returns true when self is a non-null List backed by ArrayList.
 * @param self candidate List handle
 * @return true if List_kind(self) == LIST_KIND_ARRAY
 */
bool ArrayList_isArrayList(const List *self);

/**
 * Direct fast-path slot lookup without vtable indirection.
 * @param self  ArrayList-backed List handle
 * @param index zero-based element index
 * @return pointer to element slot, or nullptr if out of bounds or wrong kind
 */
any ArrayList_get(const List *self, bytes index);

/**
 * Direct fast-path zeroed-slot append returning the new element address.
 * @param self ArrayList-backed List handle
 * @return pointer to appended zeroed slot, or nullptr on exhaustion
 */
any ArrayList_emplace(List *self);

/**
 * Direct fast-path append copying elemBytes from src after stride check.
 * @param self      ArrayList-backed List handle
 * @param src       pointer to source value
 * @param elemBytes byte width of source value
 * @return true on success, false on mismatch, wrong kind, or exhaustion
 */
bool ArrayList_addBytes(List *self, const_any src, bytes elemBytes);

/**
 * Direct fast-path overwrite at index after stride check.
 * @param self      ArrayList-backed List handle
 * @param index     zero-based element index
 * @param src       pointer to source value
 * @param elemBytes byte width of source value
 * @return true on success, false on bounds/mismatch/wrong kind
 */
bool ArrayList_setBytes(List *self, bytes index, const_any src, bytes elemBytes);

/**
 * Direct fast-path insertion at index [0..count], shifting the tail right.
 * @param self      ArrayList-backed List handle
 * @param index     insertion index in [0..count]
 * @param src       pointer to source value
 * @param elemBytes byte width of source value
 * @return true on success, false on bounds/mismatch/exhaustion
 */
bool ArrayList_insertBytes(List *self, bytes index, const_any src, bytes elemBytes);

/**
 * Removes the element at index, preserving order by shifting the tail left.
 * @param self  ArrayList-backed List handle
 * @param index zero-based element index
 * @return true if removed, false if out of bounds or wrong kind
 */
bool ArrayList_remove(List *self, bytes index);

/**
 * Removes the element at index in O(1) by moving the last element into its slot.
 * @param self  ArrayList-backed List handle
 * @param index zero-based element index
 * @return true if removed, false if out of bounds or wrong kind
 */
bool ArrayList_swapRemove(List *self, bytes index);

/**
 * Ensures backing storage holds at least minCapacity elements without shrinking.
 * @param self        ArrayList-backed List handle
 * @param minCapacity minimum element capacity required
 * @return true on success, false on exhaustion or wrong kind
 */
bool ArrayList_reserve(List *self, bytes minCapacity);

/**
 * Reallocates the backing buffer down to match active element count.
 * @param self ArrayList-backed List handle
 * @return true on success, false on wrong kind or refusal (data preserved)
 */
bool ArrayList_shrinkToFit(List *self);

/**
 * Resets active count to zero while retaining allocated buffer capacity.
 * @param self ArrayList-backed List handle
 */
fn ArrayList_clear(List *self);

/**
 * Releases the backing buffer and ArrayList handle to its owning arena.
 * @param self ArrayList-backed List handle (nullptr is a no-op)
 */
fn ArrayList_free(List *self);

/**
 * Returns the contiguous raw element buffer for flat DOD loops.
 * @param self ArrayList-backed List handle
 * @return borrowed buffer pointer, or nullptr when empty/unallocated/wrong kind
 */
u8 *ArrayList_data(const List *self);

fn ArrayList_toString(const List *self, utf8 *dest, bytes cap, bool *outTruncated);
fn ArrayList_toStringStruct(const List *self, utf8 *dest, bytes cap, bool *outTruncated);

#define ArrayList_add(list, value)                                               \
    ({                                                                           \
        auto _vex_alist_add_val = (value);                                       \
        ArrayList_addBytes((list), &_vex_alist_add_val,                          \
                           sizeof(_vex_alist_add_val));                          \
    })

#define ArrayList_set(list, index, value)                                        \
    ({                                                                           \
        auto _vex_alist_set_val = (value);                                       \
        ArrayList_setBytes((list), (index), &_vex_alist_set_val,                 \
                           sizeof(_vex_alist_set_val));                          \
    })

#define ArrayList_insert(list, index, value)                                     \
    ({                                                                           \
        auto _vex_alist_ins_val = (value);                                       \
        ArrayList_insertBytes((list), (index), &_vex_alist_ins_val,              \
                              sizeof(_vex_alist_ins_val));                       \
    })

#define ArrayList_getAs(list, index, T) List_getAs((list), (index), T)

#endif
