#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ArrayList
 * ============================================================================
 * Contiguous, growable DOD implementation of the polymorphic List interface
 * owned by Relational Engine (lifetime tier). Elements are stored inline in a
 * single 32-byte-aligned MemoryArena buffer sized by ElementLayout.stride so
 * hot loops iterate contiguous cache lines with zero pointer chasing. Growth
 * doubles capacity geometrically with checked multiplication guards; refused
 * growth preserves existing state, THROWs once per epoch, and increments the
 * Collection exhaustion counter.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ArrayList (struct/array_list.c)
 * ============================================================================
 * Contiguous growable List backing over a single arena-allocated flat buffer.
 *
 * --CONSTANTS----------------------------
 * public   const u32 ARRAY_LIST_DEFAULT_CAPACITY  default initial slots (16)
 *
 * --FIELDS-------------------------------
 * public   List base                              embedded polymorphic List, 1:1
 *
 * --CONSTRUCTORS-------------------------
 * public   ArrayList_newLayout(...)               allocates ArrayList in arena
 *
 * --FUNCTIONS----------------------------
 * public   ArrayList_isArrayList(...)             kind check against LIST_KIND_ARRAY
 * public   ArrayList_get(...)                     direct O(1) slot pointer read
 * public   ArrayList_emplace(...)                 appends zeroed slot in place
 * public   ArrayList_addBytes(...)                stride-checked append
 * public   ArrayList_setBytes(...)                stride-checked indexed write
 * public   ArrayList_insertBytes(...)             order-preserving insert at index
 * public   ArrayList_remove(...)                  order-preserving remove at index
 * public   ArrayList_swapRemove(...)              O(1) swap-with-last remove
 * public   ArrayList_reserve(...)                 cold buffer growth gate
 * public   ArrayList_shrinkToFit(...)             trims excess capacity to count
 * public   ArrayList_clear(...)                   zeros count without freeing buffer
 * public   ArrayList_free(...)                    releases buffer and handle
 * public   ArrayList_toString(...)                cold value projection
 * public   ArrayList_toStringStruct(...)          cold structure projection
 * private  arrayListOps(...)                      lazy thread-safe vtable singleton
 * private  arrayListGrow(...)                     checked geometric buffer growth
 * private  arrayListCount(...)                    vtable count callback
 * private  arrayListCapacity(...)                 vtable capacity callback
 *
 * --GETTERS------------------------------
 * public   ArrayList_data(...)                    borrowed raw backing buffer
 * private  AlColl_activeCount(...)                reads Collection.activeCount
 * private  AlColl_capacity(...)                   reads Collection.capacity
 * private  AlColl_data(...)                       reads Collection.data
 *
 * --SETTERS------------------------------
 * private  AlColl_setActiveCount(...)             writes Collection.activeCount
 * private  AlColl_setCapacity(...)                writes Collection.capacity
 * private  AlColl_setData(...)                    writes Collection.data
 * ============================================================================
 */

static ListOps s_arrayListOps;
static SpinLock s_arrayListOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_arrayListOpsReady = false;

;;GETTER
static inline u32 AlColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;SETTER
static inline fn AlColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;GETTER
static inline u32 AlColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;SETTER
static inline fn AlColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;GETTER
static inline u8 *AlColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn AlColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

static bool arrayListGrow(List *self, bytes minCapacity)
{
    Collection *c = (Collection*) self;
    bytes currentCap = (bytes) AlColl_capacity(c);

    if(minCapacity <= currentCap)
        return true;

    if(minCapacity > (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, minCapacity);

        return false;
    }

    bytes newCap = currentCap == 0 ? (bytes) ARRAY_LIST_DEFAULT_CAPACITY : currentCap;

    while(newCap < minCapacity) {
        if(newCap > ((bytes) UINT32_MAX / 2u)) {
            newCap = minCapacity;
            break;
        }

        newCap *= 2u;
    }

    if(newCap > (bytes) UINT32_MAX)
        newCap = (bytes) UINT32_MAX;

    bytes stride = List_stride(self);
    bytes byteCount = 0;

    if(ckd_mul(&byteCount, newCap, stride)) {
        (discard) Collection_reportExhaustion(c, SIZE_MAX);

        return false;
    }

    MemoryArena *arena = List_arena(self);
    u8 *next = (u8*) MemoryArena_alloc(arena, TYPE_ARRAY_LIST, byteCount);

    if(!next) {
        (discard) Collection_reportExhaustion(c, byteCount);

        return false;
    }

    u8 *oldData = AlColl_data(c);
    bytes active = (bytes) AlColl_activeCount(c);

    if(oldData && active > 0)
        memcpy(next, oldData, active * stride);

    if(oldData)
        (discard) MemoryArena_free(arena, oldData);

    AlColl_setData(c, next);
    AlColl_setCapacity(c, (u32) newCap);

    return true;
}

static bytes arrayListCount(const List *self)
{
    if(!self)
        return 0;

    return (bytes) AlColl_activeCount((const Collection*) self);
}

static bytes arrayListCapacity(const List *self)
{
    if(!self)
        return 0;

    return (bytes) AlColl_capacity((const Collection*) self);
}

static const ListOps *arrayListOps()
{
    if(atomic_load_explicit(&s_arrayListOpsReady, memory_order_acquire))
        return &s_arrayListOps;

    SpinLock_lock(&s_arrayListOpsLock);

    if(!atomic_load_explicit(&s_arrayListOpsReady, memory_order_relaxed)) {
        ListOps_setKind(&s_arrayListOps, LIST_KIND_ARRAY);
        ListOps_setKindName(&s_arrayListOps, "ArrayList");
        ListOps_setGet(&s_arrayListOps, ArrayList_get);
        ListOps_setSetBytes(&s_arrayListOps, ArrayList_setBytes);
        ListOps_setEmplace(&s_arrayListOps, ArrayList_emplace);
        ListOps_setAddBytes(&s_arrayListOps, ArrayList_addBytes);
        ListOps_setInsertBytes(&s_arrayListOps, ArrayList_insertBytes);
        ListOps_setRemoveAt(&s_arrayListOps, ArrayList_remove);
        ListOps_setClear(&s_arrayListOps, ArrayList_clear);
        ListOps_setReserve(&s_arrayListOps, ArrayList_reserve);
        ListOps_setCount(&s_arrayListOps, arrayListCount);
        ListOps_setCapacity(&s_arrayListOps, arrayListCapacity);
        ListOps_setDestroy(&s_arrayListOps, ArrayList_free);
        atomic_store_explicit(&s_arrayListOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_arrayListOpsLock);

    return &s_arrayListOps;
}

;;CONSTRUCTOR
List *ArrayList_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("ArrayList: invalid layout for '%s' (stride=%zu, align=%zu)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout));

        return nullptr;
    }

    if(initialCapacity > (bytes) UINT32_MAX) {
        (discard) Struct_reportConstructionExhaustion("ArrayList", initialCapacity);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    ArrayList *obj = (ArrayList*) MemoryArena_alloc(targetArena, TYPE_ARRAY_LIST, sizeof(ArrayList));

    if(!obj) {
        (discard) Struct_reportConstructionExhaustion("ArrayList", sizeof(ArrayList));

        return nullptr;
    }

    List *list = (List*) obj;

    if(!List_initBase(list, TYPE_ARRAY_LIST, arrayListOps(), layout, targetArena)) {
        (discard) MemoryArena_free(targetArena, obj);

        return nullptr;
    }

    if(initialCapacity > 0) {
        bytes byteCount = 0;
        bytes stride = ElementLayout_stride(layout);

        if(ckd_mul(&byteCount, initialCapacity, stride)) {
            (discard) Struct_reportConstructionExhaustion("ArrayList", SIZE_MAX);
            (discard) MemoryArena_free(targetArena, obj);

            return nullptr;
        }

        u8 *data = (u8*) MemoryArena_alloc(targetArena, TYPE_ARRAY_LIST, byteCount);

        if(!data) {
            (discard) Struct_reportConstructionExhaustion("ArrayList", byteCount);
            (discard) MemoryArena_free(targetArena, obj);

            return nullptr;
        }

        memset(data, 0, byteCount);
        AlColl_setData((Collection*) list, data);
        AlColl_setCapacity((Collection*) list, (u32) initialCapacity);
    }

    return list;
}

bool ArrayList_isArrayList(const List *self)
{
    if(!self)
        return false;

    return List_kind(self) == LIST_KIND_ARRAY;
}

;;HOTCODE
any ArrayList_get(const List *self, bytes index)
{
    if(!self)
        return nullptr;

    if(List_kind(self) != LIST_KIND_ARRAY)
        return nullptr;

    const Collection *c = (const Collection*) self;
    bytes active = (bytes) AlColl_activeCount(c);

    if(index >= active)
        return nullptr;

    u8 *data = AlColl_data(c);

    if(!data)
        return nullptr;

    bytes stride = List_stride(self);
    bytes offset = 0;

    if(ckd_mul(&offset, index, stride))
        return nullptr;

    return data + offset;
}

any ArrayList_emplace(List *self)
{
    if(!ArrayList_isArrayList(self))
        return nullptr;

    Collection *c = (Collection*) self;
    bytes active = (bytes) AlColl_activeCount(c);

    if(active >= (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, active + 1u);

        return nullptr;
    }

    if(active >= (bytes) AlColl_capacity(c)) {
        if(!arrayListGrow(self, active + 1u))
            return nullptr;
    }

    bytes stride = List_stride(self);
    u8 *slot = AlColl_data(c) + (active * stride);
    memset(slot, 0, stride);
    AlColl_setActiveCount(c, (u32) (active + 1u));

    return slot;
}

bool ArrayList_addBytes(List *self, const_any src, bytes elemBytes)
{
    if(!ArrayList_isArrayList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("ArrayList_add: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    any slot = ArrayList_emplace(self);

    if(!slot)
        return false;

    memcpy(slot, src, stride);

    return true;
}

bool ArrayList_setBytes(List *self, bytes index, const_any src, bytes elemBytes)
{
    if(!ArrayList_isArrayList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("ArrayList_set: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    any slot = ArrayList_get(self, index);

    if(!slot)
        return false;

    memcpy(slot, src, stride);

    return true;
}

bool ArrayList_insertBytes(List *self, bytes index, const_any src, bytes elemBytes)
{
    if(!ArrayList_isArrayList(self) || !src)
        return false;

    bytes stride = List_stride(self);

    if(elemBytes != stride) {
        THROW("ArrayList_insert: value width %zu does not match '%s' stride %zu",
              elemBytes,
              List_typeName(self),
              stride);

        return false;
    }

    Collection *c = (Collection*) self;
    bytes active = (bytes) AlColl_activeCount(c);

    if(index > active)
        return false;

    if(active >= (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, active + 1u);

        return false;
    }

    if(active >= (bytes) AlColl_capacity(c)) {
        if(!arrayListGrow(self, active + 1u))
            return false;
    }

    u8 *data = AlColl_data(c);
    bytes tail = active - index;

    if(tail > 0) {
        memmove(data + ((index + 1u) * stride),
                data + (index * stride),
                tail * stride);
    }

    memcpy(data + (index * stride), src, stride);
    AlColl_setActiveCount(c, (u32) (active + 1u));

    return true;
}

bool ArrayList_remove(List *self, bytes index)
{
    if(!ArrayList_isArrayList(self))
        return false;

    Collection *c = (Collection*) self;
    bytes active = (bytes) AlColl_activeCount(c);

    if(index >= active)
        return false;

    bytes stride = List_stride(self);
    u8 *data = AlColl_data(c);
    bytes tail = active - index - 1u;

    if(tail > 0) {
        memmove(data + (index * stride),
                data + ((index + 1u) * stride),
                tail * stride);
    }

    AlColl_setActiveCount(c, (u32) (active - 1u));

    return true;
}

bool ArrayList_swapRemove(List *self, bytes index)
{
    if(!ArrayList_isArrayList(self))
        return false;

    Collection *c = (Collection*) self;
    bytes active = (bytes) AlColl_activeCount(c);

    if(index >= active)
        return false;

    bytes last = active - 1u;

    if(index != last) {
        bytes stride = List_stride(self);
        u8 *data = AlColl_data(c);
        memcpy(data + (index * stride), data + (last * stride), stride);
    }

    AlColl_setActiveCount(c, (u32) last);

    return true;
}

bool ArrayList_reserve(List *self, bytes minCapacity)
{
    if(!ArrayList_isArrayList(self))
        return false;

    return arrayListGrow(self, minCapacity);
}

bool ArrayList_shrinkToFit(List *self)
{
    if(!ArrayList_isArrayList(self))
        return false;

    Collection *c = (Collection*) self;
    bytes active = (bytes) AlColl_activeCount(c);
    bytes cap = (bytes) AlColl_capacity(c);

    if(active == cap)
        return true;

    MemoryArena *arena = List_arena(self);
    u8 *oldData = AlColl_data(c);

    if(active == 0) {
        if(oldData)
            (discard) MemoryArena_free(arena, oldData);

        AlColl_setData(c, nullptr);
        AlColl_setCapacity(c, 0);

        return true;
    }

    bytes stride = List_stride(self);
    bytes byteCount = active * stride;
    u8 *shrunk = (u8*) MemoryArena_alloc(arena, TYPE_ARRAY_LIST, byteCount);

    if(!shrunk) {
        (discard) Collection_reportExhaustion(c, byteCount);

        return false;
    }

    memcpy(shrunk, oldData, byteCount);
    (discard) MemoryArena_free(arena, oldData);

    AlColl_setData(c, shrunk);
    AlColl_setCapacity(c, (u32) active);

    return true;
}

fn ArrayList_clear(List *self)
{
    if(!ArrayList_isArrayList(self))
        return;

    AlColl_setActiveCount((Collection*) self, 0);
}

;;DECONSTRUCT
fn ArrayList_free(List *self)
{
    if(!self)
        return;

    if(!ArrayList_isArrayList(self)) {
        List_free(self);

        return;
    }

    Collection *c = (Collection*) self;
    MemoryArena *arena = List_arena(self);
    u8 *data = AlColl_data(c);

    if(data) {
        AlColl_setData(c, nullptr);
        (discard) MemoryArena_free(arena, data);
    }

    (discard) MemoryArena_free(arena, self);
}

;;GETTER
u8 *ArrayList_data(const List *self)
{
    if(!ArrayList_isArrayList(self))
        return nullptr;

    return AlColl_data((const Collection*) self);
}

fn ArrayList_toString(const List *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    List_toString(self, dest, cap, outTruncated);
}

fn ArrayList_toStringStruct(const List *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    List_toStringStruct(self, dest, cap, outTruncated);
}
