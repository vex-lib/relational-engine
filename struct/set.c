#include "vex.h"
#include "nio/mem.h"
#include "struct/set.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Set
 * ============================================================================
 * Polymorphic unique-element Set interface owned by Relational Engine
 * (lifetime tier). Concrete implementations (HashSet, LinkedHashSet) embed Set
 * at offset zero, register a SetOps vtable, and carry ElementLayout metadata.
 * String elements automatically hash and compare by UTF-8 content via
 * ElementLayout_of(string); scalars, structs, and pointers hash and compare by
 * exact byte representation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Set (struct/set.c)
 * ============================================================================
 * Polymorphic Set interface over Collection + SetOps vtable + ElementLayout.
 *
 * --FIELDS-------------------------------
 * public   Collection    collection        embedded Collection header, 1:1
 * public   const SetOps *ops               borrows SetOps vtable, 1:1
 * public   MemoryArena  *arena             borrows owning MemoryArena, 1:1
 * public   ElementLayout layout            element type layout
 *
 * --CONSTRUCTORS-------------------------
 * public   Set_initBase(...)               initializes embedded Set header
 *
 * --FUNCTIONS----------------------------
 * public   Set_addBytes(...)               stride-checked unique insert
 * public   Set_containsBytes(...)          stride-checked membership predicate
 * public   Set_getBytes(...)               stride-checked lookup returning slot*
 * public   Set_removeBytes(...)            stride-checked removal
 * public   Set_clear(...)                  clears all elements
 * public   Set_reserve(...)                reserves slot capacity
 * public   Set_toList(...)                 exports elements as ArrayList
 * public   Set_forEach(...)                visits all elements
 * public   Set_free(...)                   dispatches concrete destructor
 * public   Set_toString(...)               cold value projection
 * public   Set_toStringStruct(...)         cold structure projection
 * private  formatWrite(...)                bounded snprintf helper
 *
 * --GETTERS------------------------------
 * public   Set_count(...)                  active element count
 * public   Set_size(...)                   alias for Set_count
 * public   Set_capacity(...)               table capacity
 * public   Set_isEmpty(...)                true when count == 0
 * public   Set_stride(...)                 element byte width
 * public   Set_typeName(...)               element type name
 * public   Set_kind(...)                   concrete SetKind
 * public   Set_kindName(...)               concrete backing name
 * public   Set_arena(...)                  borrowed MemoryArena
 * public   Set_ops(...)                    borrowed SetOps vtable
 * public   Set_layout(...)                 element ElementLayout
 * public   Set_exhaustionCount(...)        refused-grow count
 *
 * --SETTERS------------------------------
 * public   SetOps_set*(...)                vtable population setters
 * private  Set_set*(...)                   internal field setters
 * private  SetColl_set*(...)               embedded Collection setters
 * ============================================================================
 */

;;GETTER
static inline SetKind SetOps_kind(const SetOps *ops)
{
    if(!ops)
        return SET_KIND_UNKNOWN;

    return (*ops).kind;
}

;;GETTER
static inline string SetOps_kindName(const SetOps *ops)
{
    if(!ops || !(*ops).kindName)
        return "UnknownSet";

    return (*ops).kindName;
}

;;GETTER
static inline SetAddBytesFn SetOps_addBytes(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).addBytes;
}

;;GETTER
static inline SetContainsBytesFn SetOps_containsBytes(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).containsBytes;
}

;;GETTER
static inline SetGetBytesFn SetOps_getBytes(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).getBytes;
}

;;GETTER
static inline SetRemoveBytesFn SetOps_removeBytes(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).removeBytes;
}

;;GETTER
static inline SetClearFn SetOps_clear(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).clear;
}

;;GETTER
static inline SetReserveFn SetOps_reserve(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).reserve;
}

;;GETTER
static inline SetToListFn SetOps_toList(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).toList;
}

;;GETTER
static inline SetForEachFn SetOps_forEach(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).forEach;
}

;;GETTER
static inline SetCountFn SetOps_count(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).count;
}

;;GETTER
static inline SetCapacityFn SetOps_capacity(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).capacity;
}

;;GETTER
static inline SetDestroyFn SetOps_destroy(const SetOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).destroy;
}

;;SETTER
fn SetOps_setKind(SetOps *ops, SetKind kind)
{
    if(!ops)
        return;

    (*ops).kind = kind;
}

;;SETTER
fn SetOps_setKindName(SetOps *ops, string kindName)
{
    if(!ops)
        return;

    (*ops).kindName = kindName;
}

;;SETTER
fn SetOps_setAddBytes(SetOps *ops, SetAddBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).addBytes = fnPtr;
}

;;SETTER
fn SetOps_setContainsBytes(SetOps *ops, SetContainsBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).containsBytes = fnPtr;
}

;;SETTER
fn SetOps_setGetBytes(SetOps *ops, SetGetBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).getBytes = fnPtr;
}

;;SETTER
fn SetOps_setRemoveBytes(SetOps *ops, SetRemoveBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).removeBytes = fnPtr;
}

;;SETTER
fn SetOps_setClear(SetOps *ops, SetClearFn fnPtr)
{
    if(!ops)
        return;

    (*ops).clear = fnPtr;
}

;;SETTER
fn SetOps_setReserve(SetOps *ops, SetReserveFn fnPtr)
{
    if(!ops)
        return;

    (*ops).reserve = fnPtr;
}

;;SETTER
fn SetOps_setToList(SetOps *ops, SetToListFn fnPtr)
{
    if(!ops)
        return;

    (*ops).toList = fnPtr;
}

;;SETTER
fn SetOps_setForEach(SetOps *ops, SetForEachFn fnPtr)
{
    if(!ops)
        return;

    (*ops).forEach = fnPtr;
}

;;SETTER
fn SetOps_setCount(SetOps *ops, SetCountFn fnPtr)
{
    if(!ops)
        return;

    (*ops).count = fnPtr;
}

;;SETTER
fn SetOps_setCapacity(SetOps *ops, SetCapacityFn fnPtr)
{
    if(!ops)
        return;

    (*ops).capacity = fnPtr;
}

;;SETTER
fn SetOps_setDestroy(SetOps *ops, SetDestroyFn fnPtr)
{
    if(!ops)
        return;

    (*ops).destroy = fnPtr;
}

;;SETTER
static inline fn Set_setOps(Set *self, const SetOps *ops)
{
    (*self).ops = ops;
}

;;SETTER
static inline fn Set_setArena(Set *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn Set_setLayout(Set *self, ElementLayout layout)
{
    (*self).layout = layout;
}

;;SETTER
static inline fn SetColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn SetColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn SetColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn SetColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn SetColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn SetColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn SetColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn SetColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn SetColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

;;GETTER
const SetOps *Set_ops(const Set *self)
{
    if(!self)
        return nullptr;

    return (*self).ops;
}

;;GETTER
MemoryArena *Set_arena(const Set *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout Set_layout(const Set *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).layout;
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

;;CONSTRUCTOR
bool Set_initBase(Set *self, u64 typeId, const SetOps *ops,
                  ElementLayout layout, MemoryArena *arena)
{
    if(!self || !ops)
        return false;

    if(!ElementLayout_isValid(layout)) {
        THROW("Set_initBase: invalid layout for '%s'",
              ElementLayout_typeName(layout));

        return false;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return false;

    Collection *c = (Collection*) self;
    SetColl_setTypeId(c, typeId);
    SetColl_setActiveCount(c, 0);
    SetColl_setElementClass(c, 0);
    SetColl_setStride(c, (u32) ElementLayout_stride(layout));
    SetColl_setCapacity(c, 0);
    SetColl_setHead(c, 0);
    SetColl_setData(c, nullptr);
    SetColl_setExhaustionCount(c, 0);
    SetColl_setExhaustionReported(c, false);

    Set_setOps(self, ops);
    Set_setArena(self, targetArena);
    Set_setLayout(self, layout);

    return true;
}

bool Set_addBytes(Set *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return false;

    if(elemBytes != Set_stride(self)) {
        THROW("Set_add: element width %zu does not match '%s' stride %zu",
              elemBytes, Set_typeName(self), Set_stride(self));

        return false;
    }

    SetAddBytesFn addFn = SetOps_addBytes(Set_ops(self));

    if(!addFn)
        return false;

    return addFn(self, elem, elemBytes);
}

;;HOTCODE
bool Set_containsBytes(const Set *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return false;

    if(elemBytes != Set_stride(self)) {
        THROW("Set_contains: element width %zu does not match '%s' stride %zu",
              elemBytes, Set_typeName(self), Set_stride(self));

        return false;
    }

    SetContainsBytesFn hasFn = SetOps_containsBytes(Set_ops(self));

    if(!hasFn)
        return false;

    return hasFn(self, elem, elemBytes);
}

;;HOTCODE
any Set_getBytes(const Set *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return nullptr;

    if(elemBytes != Set_stride(self)) {
        THROW("Set_get: element width %zu does not match '%s' stride %zu",
              elemBytes, Set_typeName(self), Set_stride(self));

        return nullptr;
    }

    SetGetBytesFn getFn = SetOps_getBytes(Set_ops(self));

    if(!getFn)
        return nullptr;

    return getFn(self, elem, elemBytes);
}

bool Set_removeBytes(Set *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return false;

    if(elemBytes != Set_stride(self)) {
        THROW("Set_remove: element width %zu does not match '%s' stride %zu",
              elemBytes, Set_typeName(self), Set_stride(self));

        return false;
    }

    SetRemoveBytesFn remFn = SetOps_removeBytes(Set_ops(self));

    if(!remFn)
        return false;

    return remFn(self, elem, elemBytes);
}

fn Set_clear(Set *self)
{
    if(!self)
        return;

    SetClearFn clearFn = SetOps_clear(Set_ops(self));

    if(clearFn)
        clearFn(self);
}

bool Set_reserve(Set *self, bytes minCapacity)
{
    if(!self)
        return false;

    SetReserveFn resFn = SetOps_reserve(Set_ops(self));

    if(!resFn)
        return false;

    return resFn(self, minCapacity);
}

List *Set_toList(const Set *self)
{
    if(!self)
        return nullptr;

    SetToListFn listFn = SetOps_toList(Set_ops(self));

    if(!listFn)
        return nullptr;

    return listFn(self);
}

fn Set_forEach(Set *self, SetVisitFn visit, any userdata)
{
    if(!self || !visit)
        return;

    SetForEachFn eachFn = SetOps_forEach(Set_ops(self));

    if(eachFn)
        eachFn(self, visit, userdata);
}

;;DECONSTRUCT
fn Set_free(Set *self)
{
    if(!self)
        return;

    SetDestroyFn destroyFn = SetOps_destroy(Set_ops(self));

    if(destroyFn)
        destroyFn(self);
}

bytes Set_count(const Set *self)
{
    if(!self)
        return 0;

    SetCountFn countFn = SetOps_count(Set_ops(self));

    if(!countFn)
        return 0;

    return countFn(self);
}

bytes Set_size(const Set *self)
{
    return Set_count(self);
}

bytes Set_capacity(const Set *self)
{
    if(!self)
        return 0;

    SetCapacityFn capFn = SetOps_capacity(Set_ops(self));

    if(!capFn)
        return 0;

    return capFn(self);
}

bool Set_isEmpty(const Set *self)
{
    return Set_count(self) == 0;
}

bytes Set_stride(const Set *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Set_layout(self));
}

string Set_typeName(const Set *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Set_layout(self));
}

SetKind Set_kind(const Set *self)
{
    if(!self)
        return SET_KIND_UNKNOWN;

    return SetOps_kind(Set_ops(self));
}

string Set_kindName(const Set *self)
{
    if(!self)
        return "nullptr";

    return SetOps_kindName(Set_ops(self));
}

;;TEST
u64 Set_exhaustionCount(const Set *self)
{
    if(!self)
        return 0;

    return Collection_exhaustionCount((const Collection*) self);
}

fn Set_toString(const Set *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Set(nullptr)");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap, "%s<%s>(count=%zu, capacity=%zu)",
                          Set_kindName(self),
                          Set_typeName(self),
                          Set_count(self),
                          Set_capacity(self));
    formatWrite(dest, cap, outTruncated, needed);
}

fn Set_toStringStruct(const Set *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Set{nullptr}");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap,
                          "Set{kind=%s, type=%s, stride=%zu, count=%zu, capacity=%zu, exhausted=%llu}",
                          Set_kindName(self),
                          Set_typeName(self),
                          Set_stride(self),
                          Set_count(self),
                          Set_capacity(self),
                          (u64) Set_exhaustionCount(self));
    formatWrite(dest, cap, outTruncated, needed);
}
