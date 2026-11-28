#include "vex.h"
#include "nio/mem.h"
#include "struct/list.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: List
 * ============================================================================
 * Polymorphic List interface and ElementLayout descriptor owned by Relational
 * Engine (lifetime tier). Concrete implementations (ArrayList, LinkedList,
 * ChunkedList) embed List at offset zero, populate a static ListOps vtable, and
 * carry their owning MemoryArena plus ElementLayout (stride, alignment, type
 * hash, type name). Hot reads and mutations dispatch cleanly through the
 * vtable without global registries or numeric element-class IDs; cold paths
 * validate alignment against the 32-byte payload floor and provide toString /
 * toStringStruct diagnostics.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: List (struct/list.c)
 * ============================================================================
 * Polymorphic List interface over Collection + ListOps vtable + ElementLayout.
 *
 * --FIELDS-------------------------------
 * public   Collection     collection       embedded Collection header, 1:1
 * public   const ListOps *ops              borrows ListOps vtable, 1:1
 * public   MemoryArena   *arena            borrows MemoryArena, 1:1
 * public   ElementLayout  layout           element stride, align, hash, name
 *
 * --CONSTRUCTORS-------------------------
 * public   ElementLayout_make(...)         builds layout from stride/align/name
 * public   List_initBase(...)              initializes embedded List base header
 *
 * --FUNCTIONS----------------------------
 * public   ElementLayout_isValid(...)      validates stride and 32B align floor
 * public   ElementLayout_equals(...)       compares two layouts for equality
 * public   List_get(...)                   polymorphic slot pointer read
 * public   List_copyOut(...)               stride-checked copy out into dest
 * public   List_first(...)                 pointer to element 0
 * public   List_last(...)                  pointer to last element
 * public   List_emplace(...)               appends zeroed slot, returns pointer
 * public   List_addBytes(...)              stride-checked append by bytes
 * public   List_addPtr(...)                append stride bytes from pointer
 * public   List_setBytes(...)              stride-checked overwrite by bytes
 * public   List_setPtr(...)                overwrite stride bytes from pointer
 * public   List_insertBytes(...)           stride-checked insert at index
 * public   List_insertPtr(...)             insert stride bytes at index
 * public   List_remove(...)                removes element at index
 * public   List_clear(...)                 clears all active elements
 * public   List_reserve(...)               ensures minimum element capacity
 * public   List_free(...)                  dispatches concrete destructor
 * public   List_compare(...)               element-wise equality across any Lists
 * public   List_indexOfBytes(...)          linear byte search for first match
 * public   List_containsBytes(...)         boolean containment check
 * public   List_forEach(...)               visits every element in order
 * public   List_matchesLayout(...)         checks layout compatibility
 * public   List_toString(...)              cold value projection
 * public   List_toStringStruct(...)        cold structure projection
 * private  formatWrite(...)                bounded snprintf helper with truncation
 * private  isPowerOfTwo(...)               checks power-of-two alignment
 * private  naturalAlignment(...)           computes fallback alignment for stride
 *
 * --GETTERS------------------------------
 * public   ElementLayout_stride(...)       element byte width
 * public   ElementLayout_alignment(...)    element alignment in bytes
 * public   ElementLayout_typeHash(...)     FNV-1a layout fingerprint
 * public   ElementLayout_typeName(...)     type spelling string
 * public   List_count(...)                 active element count
 * public   List_size(...)                  alias for List_count
 * public   List_length(...)                alias for List_count
 * public   List_capacity(...)              allocated element capacity
 * public   List_isEmpty(...)               true when count == 0
 * public   List_stride(...)                element stride in bytes
 * public   List_alignment(...)             element alignment in bytes
 * public   List_typeName(...)              element C type name
 * public   List_typeHash(...)              element layout hash
 * public   List_kind(...)                  concrete ListKind discriminator
 * public   List_kindName(...)              concrete backing name
 * public   List_arena(...)                 owning MemoryArena pointer
 * public   List_ops(...)                   borrowed ListOps vtable
 * public   List_layout(...)                ElementLayout by value
 * public   List_exhaustionCount(...)       refused-grow count since creation
 * private  ListOps_kind(...)               reads vtable kind
 * private  ListOps_kindName(...)           reads vtable kindName
 * private  ListOps_get(...)                reads vtable get callback
 * private  ListOps_writeBytes(...)         reads vtable setBytes callback
 * private  ListOps_emplace(...)            reads vtable emplace callback
 * private  ListOps_addBytes(...)           reads vtable addBytes callback
 * private  ListOps_insertBytes(...)        reads vtable insertBytes callback
 * private  ListOps_removeAt(...)           reads vtable removeAt callback
 * private  ListOps_clear(...)              reads vtable clear callback
 * private  ListOps_reserve(...)            reads vtable reserve callback
 * private  ListOps_count(...)              reads vtable count callback
 * private  ListOps_capacity(...)           reads vtable capacity callback
 * private  ListOps_destroy(...)            reads vtable destroy callback
 *
 * --SETTERS------------------------------
 * public   ListOps_setKind(...)            sets vtable kind
 * public   ListOps_setKindName(...)        sets vtable kindName
 * public   ListOps_setGet(...)             sets vtable get callback
 * public   ListOps_setSetBytes(...)        sets vtable setBytes callback
 * public   ListOps_setEmplace(...)         sets vtable emplace callback
 * public   ListOps_setAddBytes(...)        sets vtable addBytes callback
 * public   ListOps_setInsertBytes(...)     sets vtable insertBytes callback
 * public   ListOps_setRemoveAt(...)        sets vtable removeAt callback
 * public   ListOps_setClear(...)           sets vtable clear callback
 * public   ListOps_setReserve(...)         sets vtable reserve callback
 * public   ListOps_setCount(...)           sets vtable count callback
 * public   ListOps_setCapacity(...)        sets vtable capacity callback
 * public   ListOps_setDestroy(...)         sets vtable destroy callback
 * private  ElementLayout_setStride(...)    sets layout stride
 * private  ElementLayout_setAlignment(...) sets layout alignment
 * private  ElementLayout_setTypeHash(...)  sets layout typeHash
 * private  ElementLayout_setTypeName(...)  sets layout typeName
 * private  List_setOps(...)                binds vtable pointer
 * private  List_setArena(...)              binds arena pointer
 * private  List_setLayout(...)             binds element layout
 * private  ListColl_setTypeId(...)         initializes embedded Collection.typeId
 * private  ListColl_setActiveCount(...)    initializes embedded Collection.activeCount
 * private  ListColl_setElementClass(...)   initializes embedded Collection.elementClass
 * private  ListColl_setStride(...)         initializes embedded Collection.stride
 * private  ListColl_setCapacity(...)       initializes embedded Collection.capacity
 * private  ListColl_setHead(...)           initializes embedded Collection.head
 * private  ListColl_setData(...)           initializes embedded Collection.data
 * private  ListColl_setExhaustionCount(...)    zeros exhaustion counter
 * private  ListColl_setExhaustionReported(...) zeros exhaustion report flag
 * ============================================================================
 */

;;GETTER
static inline ListKind ListOps_kind(const ListOps *ops)
{
    if(!ops)
        return LIST_KIND_UNKNOWN;

    return (*ops).kind;
}

;;GETTER
static inline string ListOps_kindName(const ListOps *ops)
{
    if(!ops || !(*ops).kindName)
        return "UnknownList";

    return (*ops).kindName;
}

;;GETTER
static inline ListGetFn ListOps_get(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).get;
}

;;GETTER
static inline ListSetBytesFn ListOps_writeBytes(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).setBytes;
}

;;GETTER
static inline ListEmplaceFn ListOps_emplace(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).emplace;
}

;;GETTER
static inline ListAddBytesFn ListOps_addBytes(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).addBytes;
}

;;GETTER
static inline ListInsertBytesFn ListOps_insertBytes(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).insertBytes;
}

;;GETTER
static inline ListRemoveAtFn ListOps_removeAt(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).removeAt;
}

;;GETTER
static inline ListClearFn ListOps_clear(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).clear;
}

;;GETTER
static inline ListReserveFn ListOps_reserve(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).reserve;
}

;;GETTER
static inline ListCountFn ListOps_count(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).count;
}

;;GETTER
static inline ListCapacityFn ListOps_capacity(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).capacity;
}

;;GETTER
static inline ListDestroyFn ListOps_destroy(const ListOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).destroy;
}

;;SETTER
fn ListOps_setKind(ListOps *ops, ListKind kind)
{
    if(!ops)
        return;

    (*ops).kind = kind;
}

;;SETTER
fn ListOps_setKindName(ListOps *ops, string kindName)
{
    if(!ops)
        return;

    (*ops).kindName = kindName;
}

;;SETTER
fn ListOps_setGet(ListOps *ops, ListGetFn fnPtr)
{
    if(!ops)
        return;

    (*ops).get = fnPtr;
}

;;SETTER
fn ListOps_setSetBytes(ListOps *ops, ListSetBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).setBytes = fnPtr;
}

;;SETTER
fn ListOps_setEmplace(ListOps *ops, ListEmplaceFn fnPtr)
{
    if(!ops)
        return;

    (*ops).emplace = fnPtr;
}

;;SETTER
fn ListOps_setAddBytes(ListOps *ops, ListAddBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).addBytes = fnPtr;
}

;;SETTER
fn ListOps_setInsertBytes(ListOps *ops, ListInsertBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).insertBytes = fnPtr;
}

;;SETTER
fn ListOps_setRemoveAt(ListOps *ops, ListRemoveAtFn fnPtr)
{
    if(!ops)
        return;

    (*ops).removeAt = fnPtr;
}

;;SETTER
fn ListOps_setClear(ListOps *ops, ListClearFn fnPtr)
{
    if(!ops)
        return;

    (*ops).clear = fnPtr;
}

;;SETTER
fn ListOps_setReserve(ListOps *ops, ListReserveFn fnPtr)
{
    if(!ops)
        return;

    (*ops).reserve = fnPtr;
}

;;SETTER
fn ListOps_setCount(ListOps *ops, ListCountFn fnPtr)
{
    if(!ops)
        return;

    (*ops).count = fnPtr;
}

;;SETTER
fn ListOps_setCapacity(ListOps *ops, ListCapacityFn fnPtr)
{
    if(!ops)
        return;

    (*ops).capacity = fnPtr;
}

;;SETTER
fn ListOps_setDestroy(ListOps *ops, ListDestroyFn fnPtr)
{
    if(!ops)
        return;

    (*ops).destroy = fnPtr;
}

;;SETTER
static inline fn List_setOps(List *self, const ListOps *ops)
{
    (*self).ops = ops;
}

;;SETTER
static inline fn List_setArena(List *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn List_setLayout(List *self, ElementLayout layout)
{
    (*self).layout = layout;
}

;;SETTER
static inline fn ListColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn ListColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn ListColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn ListColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn ListColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn ListColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn ListColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn ListColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn ListColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

;;GETTER
const ListOps *List_ops(const List *self)
{
    if(!self)
        return nullptr;

    return (*self).ops;
}

;;GETTER
MemoryArena *List_arena(const List *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout List_layout(const List *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).layout;
}

;;CONSTRUCTOR
bool List_initBase(List *self, u64 typeId, const ListOps *ops, ElementLayout layout, MemoryArena *arena)
{
    if(!self || !ops)
        return false;

    if(!ElementLayout_isValid(layout)) {
        THROW("List_initBase: invalid layout for '%s' (stride=%zu, align=%zu, floor=%u)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout),
              MEMORY_PAYLOAD_ALIGN);

        return false;
    }

    bytes stride = ElementLayout_stride(layout);

    if(stride > (bytes) UINT32_MAX) {
        THROW("List_initBase: element stride %zu exceeds u32 range for '%s'",
              stride,
              ElementLayout_typeName(layout));

        return false;
    }

    MemoryArena *resolvedArena = arena ? arena : Re_arena();

    if(!resolvedArena)
        return false;

    Collection *c = (Collection*) self;
    ListColl_setTypeId(c, typeId);
    ListColl_setActiveCount(c, 0);
    ListColl_setElementClass(c, 0);
    ListColl_setStride(c, (u32) stride);
    ListColl_setCapacity(c, 0);
    ListColl_setHead(c, 0);
    ListColl_setData(c, nullptr);
    ListColl_setExhaustionCount(c, 0);
    ListColl_setExhaustionReported(c, false);

    List_setOps(self, ops);
    List_setArena(self, resolvedArena);
    List_setLayout(self, layout);

    return true;
}

;;HOTCODE
any List_get(const List *self, bytes index)
{
    if(!self)
        return nullptr;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return nullptr;

    ListGetFn getFn = ListOps_get(ops);

    if(!getFn)
        return nullptr;

    return getFn(self, index);
}

bool List_copyOut(const List *self, bytes index, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("List_getAs: size mismatch for '%s' (%zu bytes) on list of '%s' (%zu bytes)",
              typeName ? typeName : "unknown",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    any slot = List_get(self, index);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

;;HOTCODE
any List_first(const List *self)
{
    if(!self)
        return nullptr;

    if(List_count(self) == 0)
        return nullptr;

    return List_get(self, 0);
}

;;HOTCODE
any List_last(const List *self)
{
    if(!self)
        return nullptr;

    bytes count = List_count(self);

    if(count == 0)
        return nullptr;

    return List_get(self, count - 1u);
}

any List_emplace(List *self)
{
    if(!self)
        return nullptr;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return nullptr;

    ListEmplaceFn emplaceFn = ListOps_emplace(ops);

    if(!emplaceFn)
        return nullptr;

    return emplaceFn(self);
}

bool List_addBytes(List *self, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("List_add: value width %zu does not match list '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    const ListOps *ops = List_ops(self);

    if(!ops)
        return false;

    ListAddBytesFn addFn = ListOps_addBytes(ops);

    if(!addFn)
        return false;

    return addFn(self, src, elemBytes);
}

bool List_addPtr(List *self, const_any src)
{
    if(!self || !src)
        return false;

    return List_addBytes(self, src, List_stride(self));
}

bool List_setBytes(List *self, bytes index, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("List_set: value width %zu does not match list '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    const ListOps *ops = List_ops(self);

    if(!ops)
        return false;

    ListSetBytesFn setFn = ListOps_writeBytes(ops);

    if(!setFn)
        return false;

    return setFn(self, index, src, elemBytes);
}

bool List_setPtr(List *self, bytes index, const_any src)
{
    if(!self || !src)
        return false;

    return List_setBytes(self, index, src, List_stride(self));
}

bool List_insertBytes(List *self, bytes index, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("List_insert: value width %zu does not match list '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    const ListOps *ops = List_ops(self);

    if(!ops)
        return false;

    ListInsertBytesFn insertFn = ListOps_insertBytes(ops);

    if(!insertFn)
        return false;

    return insertFn(self, index, src, elemBytes);
}

bool List_insertPtr(List *self, bytes index, const_any src)
{
    if(!self || !src)
        return false;

    return List_insertBytes(self, index, src, List_stride(self));
}

bool List_remove(List *self, bytes index)
{
    if(!self)
        return false;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return false;

    ListRemoveAtFn removeFn = ListOps_removeAt(ops);

    if(!removeFn)
        return false;

    return removeFn(self, index);
}

fn List_clear(List *self)
{
    if(!self)
        return;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return;

    ListClearFn clearFn = ListOps_clear(ops);

    if(!clearFn)
        return;

    clearFn(self);
}

bool List_reserve(List *self, bytes minCapacity)
{
    if(!self)
        return false;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return false;

    ListReserveFn reserveFn = ListOps_reserve(ops);

    if(!reserveFn)
        return false;

    return reserveFn(self, minCapacity);
}

;;DECONSTRUCT
fn List_free(List *self)
{
    if(!self)
        return;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return;

    ListDestroyFn destroyFn = ListOps_destroy(ops);

    if(!destroyFn)
        return;

    destroyFn(self);
}

bool List_compare(const List *a, const List *b)
{
    if(a == b)
        return true;

    if(!a || !b)
        return false;

    bytes countA = List_count(a);

    if(countA != List_count(b))
        return false;

    bytes strideA = List_stride(a);

    if(strideA != List_stride(b))
        return false;

    for(bytes i = 0; i < countA; i++) {
        const_any slotA = List_get(a, i);
        const_any slotB = List_get(b, i);

        if(!slotA || !slotB)
            return false;

        if(memcmp(slotA, slotB, strideA) != 0)
            return false;
    }

    return true;
}

i64 List_indexOfBytes(const List *self, const_any needle, bytes elemBytes)
{
    if(!self || !needle)
        return -1;

    bytes stride = List_stride(self);

    if(elemBytes != stride)
        return -1;

    bytes count = List_count(self);

    for(bytes i = 0; i < count; i++) {
        const_any slot = List_get(self, i);

        if(slot && memcmp(slot, needle, stride) == 0)
            return (i64) i;
    }

    return -1;
}

bool List_containsBytes(const List *self, const_any needle, bytes elemBytes)
{
    return List_indexOfBytes(self, needle, elemBytes) >= 0;
}

fn List_forEach(List *self, ListVisitFn visit, any userdata)
{
    if(!self || !visit)
        return;

    bytes count = List_count(self);

    for(bytes i = 0; i < count; i++) {
        any slot = List_get(self, i);

        if(slot)
            visit(slot, i, userdata);
    }
}

bool List_matchesLayout(const List *self, ElementLayout layout)
{
    if(!self)
        return false;

    return ElementLayout_equals(List_layout(self), layout);
}

;;HOTCODE
bytes List_count(const List *self)
{
    if(!self)
        return 0;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return 0;

    ListCountFn countFn = ListOps_count(ops);

    if(!countFn)
        return 0;

    return countFn(self);
}

bytes List_size(const List *self)
{
    return List_count(self);
}

bytes List_length(const List *self)
{
    return List_count(self);
}

bytes List_capacity(const List *self)
{
    if(!self)
        return 0;

    const ListOps *ops = List_ops(self);

    if(!ops)
        return 0;

    ListCapacityFn capFn = ListOps_capacity(ops);

    if(!capFn)
        return 0;

    return capFn(self);
}

bool List_isEmpty(const List *self)
{
    return List_count(self) == 0;
}

bytes List_stride(const List *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(List_layout(self));
}

bytes List_alignment(const List *self)
{
    if(!self)
        return 0;

    return ElementLayout_alignment(List_layout(self));
}

string List_typeName(const List *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(List_layout(self));
}

u64 List_typeHash(const List *self)
{
    if(!self)
        return 0;

    return ElementLayout_typeHash(List_layout(self));
}

ListKind List_kind(const List *self)
{
    if(!self)
        return LIST_KIND_UNKNOWN;

    return ListOps_kind(List_ops(self));
}

string List_kindName(const List *self)
{
    if(!self)
        return "nullptr";

    return ListOps_kindName(List_ops(self));
}

;;TEST
u64 List_exhaustionCount(const List *self)
{
    return Collection_exhaustionCount((const Collection*) self);
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

fn List_toString(const List *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 n = snprintf(dest, cap, "List(nullptr)");
        formatWrite(dest, cap, outTruncated, n);

        return;
    }

    i32 n = snprintf(dest, cap, "%s<%s>(count=%zu, capacity=%zu)",
                     List_kindName(self),
                     List_typeName(self),
                     List_count(self),
                     List_capacity(self));
    formatWrite(dest, cap, outTruncated, n);
}

fn List_toStringStruct(const List *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 n = snprintf(dest, cap, "List{nullptr}");
        formatWrite(dest, cap, outTruncated, n);

        return;
    }

    i32 n = snprintf(dest, cap,
                     "List{kind=%s, type=%s, stride=%zu, align=%zu, count=%zu, capacity=%zu, hash=0x%" PRIx64 "}",
                     List_kindName(self),
                     List_typeName(self),
                     List_stride(self),
                     List_alignment(self),
                     List_count(self),
                     List_capacity(self),
                     List_typeHash(self));
    formatWrite(dest, cap, outTruncated, n);
}
