#include "vex.h"
#include "nio/mem.h"
#include "struct/map.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Map
 * ============================================================================
 * Polymorphic key-value Map interface owned by Relational Engine (lifetime
 * tier). Concrete implementations (HashMap, LinkedHashMap) embed Map at offset
 * zero, register a MapOps vtable, and carry ElementLayout metadata for both
 * key and value types. String keys are automatically hashed and compared by
 * UTF-8 content via ElementLayout_of(string); scalars, structs, and pointers
 * hash and compare by exact byte representation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Map (struct/map.c)
 * ============================================================================
 * Polymorphic Map interface over Collection + MapOps vtable + key/val layouts.
 *
 * --FIELDS-------------------------------
 * public   Collection    collection        embedded Collection header, 1:1
 * public   const MapOps *ops               borrows MapOps vtable, 1:1
 * public   MemoryArena  *arena             borrows owning MemoryArena, 1:1
 * public   ElementLayout keyLayout         key type layout
 * public   ElementLayout valLayout         value type layout
 *
 * --CONSTRUCTORS-------------------------
 * public   Map_initBase(...)               initializes embedded Map header
 *
 * --FUNCTIONS----------------------------
 * public   Map_putBytes(...)               stride-checked insert/update
 * public   Map_emplaceBytes(...)           stride-checked get-or-insert slot
 * public   Map_getBytes(...)               stride-checked lookup returning value*
 * public   Map_copyOut(...)                stride-checked copy out for Map_getAs
 * public   Map_containsKeyBytes(...)       stride-checked key presence check
 * public   Map_removeBytes(...)            stride-checked key removal
 * public   Map_clear(...)                  clears all entries
 * public   Map_reserve(...)                reserves slot capacity
 * public   Map_keys(...)                   exports keys as ArrayList
 * public   Map_values(...)                 exports values as ArrayList
 * public   Map_forEach(...)                visits all entries
 * public   Map_free(...)                   dispatches concrete destructor
 * public   Map_toString(...)               cold value projection
 * public   Map_toStringStruct(...)         cold structure projection
 * private  formatWrite(...)                bounded snprintf helper
 *
 * --GETTERS------------------------------
 * public   Map_count(...)                  active entry count
 * public   Map_size(...)                   alias for Map_count
 * public   Map_capacity(...)               table capacity
 * public   Map_isEmpty(...)                true when count == 0
 * public   Map_keyStride(...)              key byte width
 * public   Map_valStride(...)              value byte width
 * public   Map_keyTypeName(...)            key type name
 * public   Map_valTypeName(...)            value type name
 * public   Map_kind(...)                   concrete MapKind
 * public   Map_kindName(...)               concrete backing name
 * public   Map_arena(...)                  borrowed MemoryArena
 * public   Map_ops(...)                    borrowed MapOps vtable
 * public   Map_keyLayout(...)              key ElementLayout
 * public   Map_valLayout(...)              value ElementLayout
 * public   Map_exhaustionCount(...)        refused-grow count
 *
 * --SETTERS------------------------------
 * public   MapOps_set*(...)                vtable population setters
 * private  Map_set*(...)                   internal field setters
 * ============================================================================
 */

;;GETTER
static inline MapKind MapOps_kind(const MapOps *ops)
{
    if(!ops)
        return MAP_KIND_UNKNOWN;

    return (*ops).kind;
}

;;GETTER
static inline string MapOps_kindName(const MapOps *ops)
{
    if(!ops || !(*ops).kindName)
        return "UnknownMap";

    return (*ops).kindName;
}

;;GETTER
static inline MapPutBytesFn MapOps_putBytes(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).putBytes;
}

;;GETTER
static inline MapEmplaceBytesFn MapOps_emplaceBytes(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).emplaceBytes;
}

;;GETTER
static inline MapGetBytesFn MapOps_getBytes(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).getBytes;
}

;;GETTER
static inline MapContainsBytesFn MapOps_containsBytes(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).containsBytes;
}

;;GETTER
static inline MapRemoveBytesFn MapOps_removeBytes(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).removeBytes;
}

;;GETTER
static inline MapClearFn MapOps_clear(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).clear;
}

;;GETTER
static inline MapReserveFn MapOps_reserve(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).reserve;
}

;;GETTER
static inline MapKeysFn MapOps_keys(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).keys;
}

;;GETTER
static inline MapValuesFn MapOps_values(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).values;
}

;;GETTER
static inline MapForEachFn MapOps_forEach(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).forEach;
}

;;GETTER
static inline MapCountFn MapOps_count(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).count;
}

;;GETTER
static inline MapCapacityFn MapOps_capacity(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).capacity;
}

;;GETTER
static inline MapDestroyFn MapOps_destroy(const MapOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).destroy;
}

;;SETTER
fn MapOps_setKind(MapOps *ops, MapKind kind)
{
    if(!ops)
        return;

    (*ops).kind = kind;
}

;;SETTER
fn MapOps_setKindName(MapOps *ops, string kindName)
{
    if(!ops)
        return;

    (*ops).kindName = kindName;
}

;;SETTER
fn MapOps_setPutBytes(MapOps *ops, MapPutBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).putBytes = fnPtr;
}

;;SETTER
fn MapOps_setEmplaceBytes(MapOps *ops, MapEmplaceBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).emplaceBytes = fnPtr;
}

;;SETTER
fn MapOps_setGetBytes(MapOps *ops, MapGetBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).getBytes = fnPtr;
}

;;SETTER
fn MapOps_setContainsBytes(MapOps *ops, MapContainsBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).containsBytes = fnPtr;
}

;;SETTER
fn MapOps_setRemoveBytes(MapOps *ops, MapRemoveBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).removeBytes = fnPtr;
}

;;SETTER
fn MapOps_setClear(MapOps *ops, MapClearFn fnPtr)
{
    if(!ops)
        return;

    (*ops).clear = fnPtr;
}

;;SETTER
fn MapOps_setReserve(MapOps *ops, MapReserveFn fnPtr)
{
    if(!ops)
        return;

    (*ops).reserve = fnPtr;
}

;;SETTER
fn MapOps_setKeys(MapOps *ops, MapKeysFn fnPtr)
{
    if(!ops)
        return;

    (*ops).keys = fnPtr;
}

;;SETTER
fn MapOps_setValues(MapOps *ops, MapValuesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).values = fnPtr;
}

;;SETTER
fn MapOps_setForEach(MapOps *ops, MapForEachFn fnPtr)
{
    if(!ops)
        return;

    (*ops).forEach = fnPtr;
}

;;SETTER
fn MapOps_setCount(MapOps *ops, MapCountFn fnPtr)
{
    if(!ops)
        return;

    (*ops).count = fnPtr;
}

;;SETTER
fn MapOps_setCapacity(MapOps *ops, MapCapacityFn fnPtr)
{
    if(!ops)
        return;

    (*ops).capacity = fnPtr;
}

;;SETTER
fn MapOps_setDestroy(MapOps *ops, MapDestroyFn fnPtr)
{
    if(!ops)
        return;

    (*ops).destroy = fnPtr;
}

;;SETTER
static inline fn Map_setOps(Map *self, const MapOps *ops)
{
    (*self).ops = ops;
}

;;SETTER
static inline fn Map_setArena(Map *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn Map_setKeyLayout(Map *self, ElementLayout layout)
{
    (*self).keyLayout = layout;
}

;;SETTER
static inline fn Map_setValLayout(Map *self, ElementLayout layout)
{
    (*self).valLayout = layout;
}

;;SETTER
static inline fn MapColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn MapColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn MapColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn MapColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn MapColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn MapColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn MapColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn MapColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn MapColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

;;GETTER
const MapOps *Map_ops(const Map *self)
{
    if(!self)
        return nullptr;

    return (*self).ops;
}

;;GETTER
MemoryArena *Map_arena(const Map *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout Map_keyLayout(const Map *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).keyLayout;
}

;;GETTER
ElementLayout Map_valLayout(const Map *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).valLayout;
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

;;CONSTRUCTOR
bool Map_initBase(Map *self, u64 typeId, const MapOps *ops,
                  ElementLayout keyLayout, ElementLayout valLayout, MemoryArena *arena)
{
    if(!self || !ops)
        return false;

    if(!ElementLayout_isValid(keyLayout) || !ElementLayout_isValid(valLayout)) {
        THROW("Map_initBase: invalid layout for <%s, %s>",
              ElementLayout_typeName(keyLayout),
              ElementLayout_typeName(valLayout));

        return false;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return false;

    Collection *c = (Collection*) self;
    MapColl_setTypeId(c, typeId);
    MapColl_setActiveCount(c, 0);
    MapColl_setElementClass(c, 0);
    MapColl_setStride(c, (u32) ElementLayout_stride(valLayout));
    MapColl_setCapacity(c, 0);
    MapColl_setHead(c, 0);
    MapColl_setData(c, nullptr);
    MapColl_setExhaustionCount(c, 0);
    MapColl_setExhaustionReported(c, false);

    Map_setOps(self, ops);
    Map_setArena(self, targetArena);
    Map_setKeyLayout(self, keyLayout);
    Map_setValLayout(self, valLayout);

    return true;
}

bool Map_putBytes(Map *self, const_any key, bytes keyBytes, const_any val, bytes valBytes)
{
    if(!self || !key || !val)
        return false;

    if(keyBytes != Map_keyStride(self)) {
        THROW("Map_put: key width %zu does not match key '%s' stride %zu",
              keyBytes, Map_keyTypeName(self), Map_keyStride(self));

        return false;
    }

    if(valBytes != Map_valStride(self)) {
        THROW("Map_put: value width %zu does not match value '%s' stride %zu",
              valBytes, Map_valTypeName(self), Map_valStride(self));

        return false;
    }

    MapPutBytesFn putFn = MapOps_putBytes(Map_ops(self));

    if(!putFn)
        return false;

    return putFn(self, key, keyBytes, val, valBytes);
}

any Map_emplaceBytes(Map *self, const_any key, bytes keyBytes, bool *outInserted)
{
    if(outInserted)
        *outInserted = false;

    if(!self || !key)
        return nullptr;

    if(keyBytes != Map_keyStride(self)) {
        THROW("Map_emplace: key width %zu does not match key '%s' stride %zu",
              keyBytes, Map_keyTypeName(self), Map_keyStride(self));

        return nullptr;
    }

    MapEmplaceBytesFn emplaceFn = MapOps_emplaceBytes(Map_ops(self));

    if(!emplaceFn)
        return nullptr;

    return emplaceFn(self, key, keyBytes, outInserted);
}

;;HOTCODE
any Map_getBytes(const Map *self, const_any key, bytes keyBytes)
{
    if(!self || !key)
        return nullptr;

    if(keyBytes != Map_keyStride(self)) {
        THROW("Map_get: key width %zu does not match key '%s' stride %zu",
              keyBytes, Map_keyTypeName(self), Map_keyStride(self));

        return nullptr;
    }

    MapGetBytesFn getFn = MapOps_getBytes(Map_ops(self));

    if(!getFn)
        return nullptr;

    return getFn(self, key, keyBytes);
}

bool Map_copyOut(const Map *self, const_any key, bytes keyBytes,
                 any dest, bytes valBytes, string valTypeName)
{
    if(!self || !key || !dest)
        return false;

    if(valBytes != Map_valStride(self)) {
        THROW("Map_getAs: value width %zu for '%s' does not match map value '%s' stride %zu",
              valBytes,
              valTypeName ? valTypeName : "unknown",
              Map_valTypeName(self),
              Map_valStride(self));

        return false;
    }

    any slot = Map_getBytes(self, key, keyBytes);

    if(!slot)
        return false;

    memcpy(dest, slot, valBytes);

    return true;
}

bool Map_containsKeyBytes(const Map *self, const_any key, bytes keyBytes)
{
    if(!self || !key)
        return false;

    if(keyBytes != Map_keyStride(self)) {
        THROW("Map_containsKey: key width %zu does not match key '%s' stride %zu",
              keyBytes, Map_keyTypeName(self), Map_keyStride(self));

        return false;
    }

    MapContainsBytesFn containsFn = MapOps_containsBytes(Map_ops(self));

    if(!containsFn)
        return false;

    return containsFn(self, key, keyBytes);
}

bool Map_removeBytes(Map *self, const_any key, bytes keyBytes)
{
    if(!self || !key)
        return false;

    if(keyBytes != Map_keyStride(self)) {
        THROW("Map_remove: key width %zu does not match key '%s' stride %zu",
              keyBytes, Map_keyTypeName(self), Map_keyStride(self));

        return false;
    }

    MapRemoveBytesFn removeFn = MapOps_removeBytes(Map_ops(self));

    if(!removeFn)
        return false;

    return removeFn(self, key, keyBytes);
}

fn Map_clear(Map *self)
{
    if(!self)
        return;

    MapClearFn clearFn = MapOps_clear(Map_ops(self));

    if(clearFn)
        clearFn(self);
}

bool Map_reserve(Map *self, bytes minCapacity)
{
    if(!self)
        return false;

    MapReserveFn reserveFn = MapOps_reserve(Map_ops(self));

    if(!reserveFn)
        return false;

    return reserveFn(self, minCapacity);
}

List *Map_keys(const Map *self)
{
    if(!self)
        return nullptr;

    MapKeysFn keysFn = MapOps_keys(Map_ops(self));

    if(!keysFn)
        return nullptr;

    return keysFn(self);
}

List *Map_values(const Map *self)
{
    if(!self)
        return nullptr;

    MapValuesFn valuesFn = MapOps_values(Map_ops(self));

    if(!valuesFn)
        return nullptr;

    return valuesFn(self);
}

fn Map_forEach(Map *self, MapVisitFn visit, any userdata)
{
    if(!self || !visit)
        return;

    MapForEachFn forEachFn = MapOps_forEach(Map_ops(self));

    if(forEachFn)
        forEachFn(self, visit, userdata);
}

;;DECONSTRUCT
fn Map_free(Map *self)
{
    if(!self)
        return;

    MapDestroyFn destroyFn = MapOps_destroy(Map_ops(self));

    if(destroyFn)
        destroyFn(self);
}

bytes Map_count(const Map *self)
{
    if(!self)
        return 0;

    MapCountFn countFn = MapOps_count(Map_ops(self));

    if(!countFn)
        return 0;

    return countFn(self);
}

bytes Map_size(const Map *self)
{
    return Map_count(self);
}

bytes Map_capacity(const Map *self)
{
    if(!self)
        return 0;

    MapCapacityFn capFn = MapOps_capacity(Map_ops(self));

    if(!capFn)
        return 0;

    return capFn(self);
}

bool Map_isEmpty(const Map *self)
{
    return Map_count(self) == 0;
}

bytes Map_keyStride(const Map *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Map_keyLayout(self));
}

bytes Map_valStride(const Map *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Map_valLayout(self));
}

string Map_keyTypeName(const Map *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Map_keyLayout(self));
}

string Map_valTypeName(const Map *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Map_valLayout(self));
}

MapKind Map_kind(const Map *self)
{
    if(!self)
        return MAP_KIND_UNKNOWN;

    return MapOps_kind(Map_ops(self));
}

string Map_kindName(const Map *self)
{
    if(!self)
        return "nullptr";

    return MapOps_kindName(Map_ops(self));
}

;;TEST
u64 Map_exhaustionCount(const Map *self)
{
    return Collection_exhaustionCount((const Collection*) self);
}

fn Map_toString(const Map *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Map(nullptr)"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "%s<%s, %s>(count=%zu, capacity=%zu)",
                         Map_kindName(self), Map_keyTypeName(self),
                         Map_valTypeName(self), Map_count(self), Map_capacity(self)));
}

fn Map_toStringStruct(const Map *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Map{nullptr}"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Map{kind=%s, key=%s(%zu), val=%s(%zu), count=%zu, capacity=%zu}",
                         Map_kindName(self),
                         Map_keyTypeName(self), Map_keyStride(self),
                         Map_valTypeName(self), Map_valStride(self),
                         Map_count(self), Map_capacity(self)));
}
