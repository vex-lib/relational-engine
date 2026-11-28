#ifndef STRUCT_LIST_H
#define STRUCT_LIST_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"

/**
 * struct/list.h, the polymorphic List interface and ElementLayout metadata.
 *
 * Concrete list backings (ArrayList, LinkedList, ChunkedList) embed List as their
 * first member and register a ListOps vtable. Callers construct a concrete
 * backing by passing any C type directly:
 *
 * List *x = ArrayList(Class);
 * List *y = LinkedList(Class);
 * List *z = ChunkedList(Class);
 * Class *slot = List_emplace(x);
 * List_add(x, myClass);
 * Class *item = List_get(x, 0);
 * Class *fast = ArrayList_get(x, 0);
 * u32 value = List_getAs(nums, 0, u32);
 * List_free(x);
 *
 * No per-type registry, no numeric element class IDs, and no double-parens
 * generic stamps are required: ElementLayout_of(T) captures sizeof(T),
 * alignof(T), and #T at the call site.
 */

/**
 * Concrete backing discriminator stored in ListOps.
 *
 * LIST_KIND_UNKNOWN - uninitialized or invalid backing
 * LIST_KIND_ARRAY   - contiguous growable DOD buffer (ArrayList)
 * LIST_KIND_LINKED  - arena-backed doubly-linked nodes (LinkedList)
 * LIST_KIND_CHUNKED - never-moved radix-paged backing (ChunkedList)
 */
;;CLASSNAME("ListKind")
typedef enum ListKind {
    LIST_KIND_UNKNOWN = 0,
    LIST_KIND_ARRAY = 1,
    LIST_KIND_LINKED = 2,
    LIST_KIND_CHUNKED = 3,
} ListKind;

typedef struct List List;

typedef any (*ListGetFn)(const List *self, bytes index);
typedef bool (*ListSetBytesFn)(List *self, bytes index, const_any src, bytes elemBytes);
typedef any (*ListEmplaceFn)(List *self);
typedef bool (*ListAddBytesFn)(List *self, const_any src, bytes elemBytes);
typedef bool (*ListInsertBytesFn)(List *self, bytes index, const_any src, bytes elemBytes);
typedef bool (*ListRemoveAtFn)(List *self, bytes index);
typedef fn (*ListClearFn)(List *self);
typedef bool (*ListReserveFn)(List *self, bytes minCapacity);
typedef bytes (*ListCountFn)(const List *self);
typedef bytes (*ListCapacityFn)(const List *self);
typedef fn (*ListDestroyFn)(List *self);
typedef fn (*ListVisitFn)(any element, bytes index, any userdata);

/**
 * Polymorphic dispatch table implemented by every concrete List backing.
 *
 * ListKind kind                 - concrete backing discriminator
 * string kindName               - human-readable backing name
 * ListGetFn get                 - returns pointer to element at index or nullptr
 * ListSetBytesFn setBytes       - overwrites element at index from byte span
 * ListEmplaceFn emplace         - appends zeroed slot and returns its pointer
 * ListAddBytesFn addBytes       - appends a copy of src after stride validation
 * ListInsertBytesFn insertBytes - inserts a copy of src at index [0..count]
 * ListRemoveAtFn removeAt       - removes element at index
 * ListClearFn clear             - removes all elements while keeping the handle valid
 * ListReserveFn reserve         - ensures storage for at least minCapacity elements
 * ListCountFn count             - returns active element count
 * ListCapacityFn capacity       - returns current element capacity
 * ListDestroyFn destroy         - releases backing storage and the List handle
 */
;;CLASSNAME("ListOps")
typedef struct ListOps {
    ListKind kind;
    string kindName;
    ListGetFn get;
    ListSetBytesFn setBytes;
    ListEmplaceFn emplace;
    ListAddBytesFn addBytes;
    ListInsertBytesFn insertBytes;
    ListRemoveAtFn removeAt;
    ListClearFn clear;
    ListReserveFn reserve;
    ListCountFn count;
    ListCapacityFn capacity;
    ListDestroyFn destroy;
} ListOps;

/**
 * Polymorphic List interface header embedded at offset zero of every backing.
 *
 * Collection collection - embedded Collection header (offset zero compatibility)
 * const ListOps *ops    - borrowed vtable describing the concrete backing
 * MemoryArena *arena    - borrowed MemoryArena owning allocations for this list
 * ElementLayout layout  - element stride, alignment, type hash, and type name
 */
;;CLASSNAME("List")
typedef struct List {
    Collection collection;
    const ListOps *ops;
    MemoryArena *arena;
    ElementLayout layout;
} List;

/**
 * Initializes the shared List base header inside a concrete implementation.
 * @param self      base header to initialize
 * @param typeId    block header type ID stored in Collection.typeId
 * @param ops       concrete vtable (must be non-null)
 * @param layout    validated element layout
 * @param arena     owning arena (nullptr falls back to Re_arena())
 * @return true on success, false if inputs are invalid
 */
bool List_initBase(List *self, u64 typeId, const ListOps *ops, ElementLayout layout, MemoryArena *arena);

/**
 * Returns a direct pointer to the element at index, or nullptr if out of bounds.
 * @param self  target list
 * @param index zero-based element index
 * @return pointer to element slot (implicitly convertible to T*), or nullptr
 */
any List_get(const List *self, bytes index);

/**
 * Copies the element at index into dest after verifying elemBytes matches stride.
 * @param self      target list
 * @param index     zero-based element index
 * @param dest      caller output buffer of elemBytes bytes
 * @param elemBytes expected byte width (sizeof(T))
 * @param typeName  caller type name for mismatch diagnostics
 * @return true if copied, false on null/bounds/size mismatch
 */
bool List_copyOut(const List *self, bytes index, any dest, bytes elemBytes, string typeName);

/**
 * Returns a direct pointer to the first element, or nullptr if empty.
 * @param self target list
 * @return pointer to index 0, or nullptr
 */
any List_first(const List *self);

/**
 * Returns a direct pointer to the last element, or nullptr if empty.
 * @param self target list
 * @return pointer to last element, or nullptr
 */
any List_last(const List *self);

/**
 * Appends one zero-initialized element slot and returns its address.
 * @param self target list
 * @return pointer to the new slot, or nullptr on exhaustion
 */
any List_emplace(List *self);

/**
 * Appends a copy of src after validating elemBytes == List_stride(self).
 * @param self      target list
 * @param src       pointer to source value
 * @param elemBytes byte width of source value
 * @return true on success, false on null, mismatch, or exhaustion
 */
bool List_addBytes(List *self, const_any src, bytes elemBytes);

/**
 * Appends List_stride(self) bytes copied from src.
 * @param self target list
 * @param src  pointer to source element
 * @return true on success, false on null or exhaustion
 */
bool List_addPtr(List *self, const_any src);

/**
 * Overwrites element at index with src after validating elemBytes == stride.
 * @param self      target list
 * @param index     zero-based element index
 * @param src       pointer to source value
 * @param elemBytes byte width of source value
 * @return true on success, false on null, bounds, or size mismatch
 */
bool List_setBytes(List *self, bytes index, const_any src, bytes elemBytes);

/**
 * Overwrites element at index with List_stride(self) bytes from src.
 * @param self  target list
 * @param index zero-based element index
 * @param src   pointer to source element
 * @return true on success, false on null or out-of-bounds
 */
bool List_setPtr(List *self, bytes index, const_any src);

/**
 * Inserts a copy of src at index [0..count] after validating elemBytes == stride.
 * @param self      target list
 * @param index     insertion index in [0..count]
 * @param src       pointer to source value
 * @param elemBytes byte width of source value
 * @return true on success, false on null, bounds, mismatch, or exhaustion
 */
bool List_insertBytes(List *self, bytes index, const_any src, bytes elemBytes);

/**
 * Inserts List_stride(self) bytes from src at index [0..count].
 * @param self  target list
 * @param index insertion index in [0..count]
 * @param src   pointer to source element
 * @return true on success, false on null, bounds, or exhaustion
 */
bool List_insertPtr(List *self, bytes index, const_any src);

/**
 * Removes the element at index.
 * @param self  target list
 * @param index zero-based element index
 * @return true if removed, false on null, bounds, or unsupported backing
 */
bool List_remove(List *self, bytes index);

/**
 * Removes all elements from the list while keeping it usable.
 * @param self target list
 */
fn List_clear(List *self);

/**
 * Pre-allocates backing capacity for at least minCapacity elements.
 * @param self        target list
 * @param minCapacity minimum element capacity required
 * @return true on success, false on exhaustion or invalid receiver
 */
bool List_reserve(List *self, bytes minCapacity);

/**
 * Releases the list and all backing storage back to its owning arena.
 * @param self target list (nullptr is a no-op)
 */
fn List_free(List *self);

/**
 * Element-wise byte comparison across any two List implementations.
 * @param a first list
 * @param b second list
 * @return true if both lists have equal count, stride, and element bytes
 */
bool List_compare(const List *a, const List *b);

/**
 * Finds the first index whose element bytes match needle, or -1 if absent.
 * @param self      target list
 * @param needle    pointer to value to find
 * @param elemBytes byte width of needle
 * @return zero-based index, or -1 if not found / mismatched
 */
i64 List_indexOfBytes(const List *self, const_any needle, bytes elemBytes);

/**
 * Returns true if any element's bytes match needle.
 * @param self      target list
 * @param needle    pointer to value to find
 * @param elemBytes byte width of needle
 * @return true if found
 */
bool List_containsBytes(const List *self, const_any needle, bytes elemBytes);

/**
 * Invokes visit(element, index, userdata) for every active element in order.
 * @param self     target list
 * @param visit    callback function
 * @param userdata opaque caller context forwarded to visit
 */
fn List_forEach(List *self, ListVisitFn visit, any userdata);

/**
 * Returns true if list's ElementLayout matches layout.
 * @param self   target list
 * @param layout expected layout
 * @return true when stride, alignment, and type hash match
 */
bool List_matchesLayout(const List *self, ElementLayout layout);

bytes List_count(const List *self);
bytes List_size(const List *self);
bytes List_length(const List *self);
bytes List_capacity(const List *self);
bool List_isEmpty(const List *self);
bytes List_stride(const List *self);
bytes List_alignment(const List *self);
string List_typeName(const List *self);
u64 List_typeHash(const List *self);
ListKind List_kind(const List *self);
string List_kindName(const List *self);
MemoryArena *List_arena(const List *self);
const ListOps *List_ops(const List *self);
ElementLayout List_layout(const List *self);

;;TEST
u64 List_exhaustionCount(const List *self);

fn List_toString(const List *self, utf8 *dest, bytes cap, bool *outTruncated);
fn List_toStringStruct(const List *self, utf8 *dest, bytes cap, bool *outTruncated);

fn ListOps_setKind(ListOps *ops, ListKind kind);
fn ListOps_setKindName(ListOps *ops, string kindName);
fn ListOps_setGet(ListOps *ops, ListGetFn fnPtr);
fn ListOps_setSetBytes(ListOps *ops, ListSetBytesFn fnPtr);
fn ListOps_setEmplace(ListOps *ops, ListEmplaceFn fnPtr);
fn ListOps_setAddBytes(ListOps *ops, ListAddBytesFn fnPtr);
fn ListOps_setInsertBytes(ListOps *ops, ListInsertBytesFn fnPtr);
fn ListOps_setRemoveAt(ListOps *ops, ListRemoveAtFn fnPtr);
fn ListOps_setClear(ListOps *ops, ListClearFn fnPtr);
fn ListOps_setReserve(ListOps *ops, ListReserveFn fnPtr);
fn ListOps_setCount(ListOps *ops, ListCountFn fnPtr);
fn ListOps_setCapacity(ListOps *ops, ListCapacityFn fnPtr);
fn ListOps_setDestroy(ListOps *ops, ListDestroyFn fnPtr);

/**
 * Value-oriented C23 convenience macros over the stride-checked byte ABI.
 * auto performs array-to-pointer decay (so string literals decay to string)
 * and preserves exact struct/scalar/pointer widths for runtime stride checks.
 */
#define List_add(list, value)                                                \
    ({                                                                       \
        auto _vex_list_add_val = (value);                                    \
        List_addBytes((list), &_vex_list_add_val, sizeof(_vex_list_add_val)); \
    })

#define List_set(list, index, value)                                         \
    ({                                                                       \
        auto _vex_list_set_val = (value);                                    \
        List_setBytes((list), (index), &_vex_list_set_val,                   \
                      sizeof(_vex_list_set_val));                            \
    })

#define List_insert(list, index, value)                                      \
    ({                                                                       \
        auto _vex_list_ins_val = (value);                                    \
        List_insertBytes((list), (index), &_vex_list_ins_val,                \
                         sizeof(_vex_list_ins_val));                         \
    })

#define List_getAs(list, index, T)                                           \
    ({                                                                       \
        typeof_unqual(T) _vex_list_get_out;                                  \
        memset(&_vex_list_get_out, 0, sizeof(_vex_list_get_out));            \
        (discard) List_copyOut((list), (index), &_vex_list_get_out,          \
                               sizeof(_vex_list_get_out), #T);               \
        _vex_list_get_out;                                                   \
    })

#define List_indexOf(list, value)                                            \
    ({                                                                       \
        auto _vex_list_idx_val = (value);                                    \
        List_indexOfBytes((list), &_vex_list_idx_val,                        \
                          sizeof(_vex_list_idx_val));                        \
    })

#define List_contains(list, value)                                           \
    ({                                                                       \
        auto _vex_list_has_val = (value);                                    \
        List_containsBytes((list), &_vex_list_has_val,                       \
                           sizeof(_vex_list_has_val));                       \
    })

#define List_isType(list, T) List_matchesLayout((list), ElementLayout_of(T))

#endif
