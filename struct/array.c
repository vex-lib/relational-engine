#include "vex.h"
#include <stdckdint.h>

#include "nio/mem.h"
#include "struct/array.h"
#include "struct/stride.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"
#include "exception/try_value.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Array / Array2D / Array3D / arrayOf
 * ============================================================================
 * Flat, contiguous, 32-byte-aligned arena arrays owned by Relational Engine
 * (lifetime tier). Ships four forms sharing the same ElementLayout contract:
 *   1. arrayOf(T, count[, arena]): raw typed T* backed directly by
 *      MemoryArena_alloc whose length is recovered in O(1) from the 32-byte
 *      self-describing MemoryHeader (arrayLength / arrayBytes / arrayResize).
 *   2. Array(T, length[, arena]): fixed-length 1D container over Collection.
 *   3. Array2D(T, w, h[, arena]): flat row-major 2D matrix (y * w + x).
 *   4. Array3D(T, w, h, d[, arena]): flat slice-major 3D volume ((z * h + y) * w + x).
 * All byte-count and index multiplications use checked C23 arithmetic so
 * overflows fail closed without wrapping.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Array / Array2D / Array3D (struct/array.c)
 * ============================================================================
 * Raw arena-backed typed arrays and fixed-length 1D/2D/3D DOD arrays.
 *
 * --FIELDS-------------------------------
 * public   Collection    collection        embedded Collection base, 1:1
 * public   MemoryArena  *arena             borrows owning MemoryArena, 1:1
 * public   ElementLayout layout            element stride, align, hash, name
 * public   bytes         width             X extent (Array2D, Array3D)
 * public   bytes         height            Y extent (Array2D, Array3D)
 * public   bytes         depth             Z extent (Array3D)
 *
 * --CONSTRUCTORS-------------------------
 * public   Array_allocRaw(...)             allocates raw typed arrayOf buffer
 * public   Array_newLayout(...)            constructs fixed 1D Array
 * public   Array_2(...)                    legacy ID-based 1D Array constructor
 * public   Array2D_newLayout(...)          constructs flat 2D Array2D
 * public   Array3D_newLayout(...)          constructs flat 3D Array3D
 *
 * --FUNCTIONS----------------------------
 * public   Array_resizeRaw(...)            resizes raw arrayOf buffer + zeros tail
 * public   Array_rawLength(...)            O(1) element count from MemoryHeader
 * public   Array_freeRaw(...)              releases raw arrayOf buffer
 * public   Array_freeRawIn(...)            releases raw buffer to explicit arena
 * public   Array_free(...)                 releases 1D Array and buffer
 * public   Array_get(...)                  O(1) bounds-checked slot pointer
 * public   Array_copyOut(...)              stride-checked copy out for Array_getAs
 * public   Array_setBytes(...)             stride-checked indexed write
 * public   Array_setPtr(...)               stride-sized indexed copy from ptr
 * public   Array_fillBytes(...)            fills every slot with one value
 * public   Array_getTry(...)               checked scalar read returning TryValue
 * public   Array_slot(...)                 mutable byte pointer to element
 * public   Array_toString(...)             cold value projection
 * public   Array_toStringStruct(...)       cold structure projection
 * public   Array2D_free(...)               releases 2D array and buffer
 * public   Array2D_get(...)                O(1) (x, y) slot pointer
 * public   Array2D_copyOut(...)            stride-checked (x, y) copy out
 * public   Array2D_setBytes(...)           stride-checked (x, y) write
 * public   Array2D_fillBytes(...)          fills every 2D cell
 * public   Array2D_toString(...)           cold value projection
 * public   Array2D_toStringStruct(...)     cold structure projection
 * public   Array3D_free(...)               releases 3D array and buffer
 * public   Array3D_get(...)                O(1) (x, y, z) slot pointer
 * public   Array3D_copyOut(...)            stride-checked (x, y, z) copy out
 * public   Array3D_setBytes(...)           stride-checked (x, y, z) write
 * public   Array3D_fillBytes(...)          fills every 3D voxel
 * public   Array3D_toString(...)           cold value projection
 * public   Array3D_toStringStruct(...)     cold structure projection
 * private  formatWrite(...)                bounded snprintf helper
 * private  initCollection(...)             populates embedded Collection fields
 *
 * --GETTERS------------------------------
 * public   Array_isEmpty(...)              true when count == 0
 * public   Array_count(...)                element count
 * public   Array_size(...)                 alias for count
 * public   Array_length(...)               alias for count
 * public   Array_capacity(...)             element capacity
 * public   Array_elementClassId(...)       legacy element class ID
 * public   Array_stride(...)               element stride in bytes
 * public   Array_alignment(...)            element alignment in bytes
 * public   Array_typeName(...)             element C type name
 * public   Array_data(...)                 borrowed flat data buffer
 * public   Array_dataBuffer(...)           alias for Array_data
 * public   Array_arena(...)                borrowed MemoryArena
 * public   Array2D_width(...)              X extent
 * public   Array2D_height(...)             Y extent
 * public   Array2D_count(...)              width * height
 * public   Array2D_stride(...)             element stride
 * public   Array2D_typeName(...)           element C type name
 * public   Array2D_data(...)               borrowed flat 2D buffer
 * public   Array2D_arena(...)              borrowed MemoryArena
 * public   Array3D_width(...)              X extent
 * public   Array3D_height(...)             Y extent
 * public   Array3D_depth(...)              Z extent
 * public   Array3D_count(...)              width * height * depth
 * public   Array3D_stride(...)             element stride
 * public   Array3D_typeName(...)           element C type name
 * public   Array3D_data(...)               borrowed flat 3D buffer
 * public   Array3D_arena(...)              borrowed MemoryArena
 *
 * --SETTERS------------------------------
 * private  ArrColl_set* / Arr_set*         internal field setters
 * ============================================================================
 */

;;SETTER
static inline fn ArrColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;GETTER
static inline u32 ArrColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;SETTER
static inline fn ArrColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn ArrColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn ArrColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn ArrColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn ArrColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn ArrColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn ArrColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

;;GETTER
static inline u8 *ArrColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn ArrColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn Array_setArena(Array *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn Array_setLayout(Array *self, ElementLayout layout)
{
    (*self).layout = layout;
}

;;GETTER
static inline ElementLayout Array_layoutField(const Array *self)
{
    return (*self).layout;
}

;;SETTER
static inline fn Array2D_setArena(Array2D *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn Array2D_setLayout(Array2D *self, ElementLayout layout)
{
    (*self).layout = layout;
}

;;GETTER
static inline ElementLayout Array2D_layoutField(const Array2D *self)
{
    return (*self).layout;
}

;;SETTER
static inline fn Array2D_setWidth(Array2D *self, bytes width)
{
    (*self).width = width;
}

;;SETTER
static inline fn Array2D_setHeight(Array2D *self, bytes height)
{
    (*self).height = height;
}

;;SETTER
static inline fn Array3D_setArena(Array3D *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn Array3D_setLayout(Array3D *self, ElementLayout layout)
{
    (*self).layout = layout;
}

;;GETTER
static inline ElementLayout Array3D_layoutField(const Array3D *self)
{
    return (*self).layout;
}

;;SETTER
static inline fn Array3D_setWidth(Array3D *self, bytes width)
{
    (*self).width = width;
}

;;SETTER
static inline fn Array3D_setHeight(Array3D *self, bytes height)
{
    (*self).height = height;
}

;;SETTER
static inline fn Array3D_setDepth(Array3D *self, bytes depth)
{
    (*self).depth = depth;
}

static fn initCollection(Collection *c, u64 typeId, u32 elementClass, bytes stride, bytes count, u8 *data)
{
    ArrColl_setTypeId(c, typeId);
    ArrColl_setActiveCount(c, (u32) count);
    ArrColl_setElementClass(c, elementClass);
    ArrColl_setStride(c, (u32) stride);
    ArrColl_setCapacity(c, (u32) count);
    ArrColl_setHead(c, 0);
    ArrColl_setExhaustionCount(c, 0);
    ArrColl_setExhaustionReported(c, false);
    ArrColl_setData(c, data);
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

;;CONSTRUCTOR
any Array_allocRaw(ElementLayout layout, bytes count, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("arrayOf: invalid layout for '%s' (stride=%zu, align=%zu)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout));

        return nullptr;
    }

    if(count == 0)
        return nullptr;

    bytes totalBytes = 0;

    if(ckd_mul(&totalBytes, count, ElementLayout_stride(layout))) {
        (discard) Struct_reportConstructionExhaustion("arrayOf", SIZE_MAX);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    any block = MemoryArena_alloc(targetArena, TYPE_ARRAY, totalBytes);

    if(!block) {
        (discard) Struct_reportConstructionExhaustion("arrayOf", totalBytes);

        return nullptr;
    }

    memset(block, 0, totalBytes);

    return block;
}

any Array_resizeRaw(any ptr, bytes stride, bytes alignment, bytes newCount)
{
    if(stride == 0 || alignment == 0 || alignment > MEMORY_PAYLOAD_ALIGN || (stride % alignment) != 0) {
        THROW("arrayResize: invalid element layout (stride=%zu, align=%zu)", stride, alignment);

        return nullptr;
    }

    if(!ptr)
        return Array_allocRaw(ElementLayout_make(stride, alignment, "raw"), newCount, Re_arena());

    if(newCount == 0) {
        (discard) Memory_free(ptr);

        return nullptr;
    }

    bytes oldBytes = Memory_length(ptr);
    bytes newBytes = 0;

    if(ckd_mul(&newBytes, newCount, stride)) {
        (discard) Struct_reportConstructionExhaustion("arrayResize", SIZE_MAX);

        return nullptr;
    }

    any resized = Memory_realloc(ptr, newBytes);

    if(!resized)
        return nullptr;

    if(newBytes > oldBytes)
        memset(((u8*) resized) + oldBytes, 0, newBytes - oldBytes);

    return resized;
}

;;HOTCODE
bytes Array_rawLength(const_any ptr, bytes stride)
{
    if(!ptr || stride == 0)
        return 0;

    return Memory_length((any) ptr) / stride;
}

;;DECONSTRUCT
fn Array_freeRaw(any ptr)
{
    if(!ptr)
        return;

    (discard) Memory_free(ptr);
}

;;DECONSTRUCT
fn Array_freeRawIn(MemoryArena *arena, any ptr)
{
    if(!ptr)
        return;

    if(arena) {
        (discard) MemoryArena_free(arena, ptr);
    } else {
        (discard) Memory_free(ptr);
    }
}

;;CONSTRUCTOR
Array *Array_newLayout(ElementLayout layout, bytes length, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("Array: invalid layout for '%s' (stride=%zu, align=%zu)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout));

        return nullptr;
    }

    if(length > (bytes) UINT32_MAX || ElementLayout_stride(layout) > (bytes) UINT32_MAX) {
        (discard) Struct_reportConstructionExhaustion("Array", length);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    Array *array = (Array*) MemoryArena_alloc(targetArena, TYPE_ARRAY, sizeof(Array));

    if(!array) {
        (discard) Struct_reportConstructionExhaustion("Array", sizeof(Array));

        return nullptr;
    }

    u8 *data = nullptr;
    bytes stride = ElementLayout_stride(layout);

    if(length > 0) {
        bytes byteCount = 0;

        if(ckd_mul(&byteCount, length, stride)) {
            (discard) Struct_reportConstructionExhaustion("Array", SIZE_MAX);
            (discard) MemoryArena_free(targetArena, array);

            return nullptr;
        }

        data = (u8*) MemoryArena_alloc(targetArena, TYPE_ARRAY, byteCount);

        if(!data) {
            (discard) Struct_reportConstructionExhaustion("Array", byteCount);
            (discard) MemoryArena_free(targetArena, array);

            return nullptr;
        }

        memset(data, 0, byteCount);
    }

    initCollection((Collection*) array, TYPE_ARRAY, 0, stride, length, data);
    Array_setArena(array, targetArena);
    Array_setLayout(array, layout);

    return array;
}

;;CONSTRUCTOR
Array *Array_2(u32 elementClass, bytes length)
{
    bytes stride = Stride_get(elementClass);
    ElementLayout layout = ElementLayout_make(stride, 0, "ArrayElement");
    Array *array = Array_newLayout(layout, length, Re_arena());

    if(array)
        ArrColl_setElementClass((Collection*) array, elementClass);

    return array;
}

;;DECONSTRUCT
fn Array_free(Array *array)
{
    if(!array)
        return;

    MemoryArena *arena = Array_arena(array);
    u8 *data = ArrColl_data((const Collection*) array);

    if(data) {
        ArrColl_setData((Collection*) array, nullptr);
        (discard) MemoryArena_free(arena, data);
    }

    (discard) MemoryArena_free(arena, array);
}

;;HOTCODE
any Array_get(const Array *array, bytes index)
{
    if(!array)
        return nullptr;

    const Collection *c = (const Collection*) array;

    if(index >= (bytes) ArrColl_activeCount(c))
        return nullptr;

    u8 *data = ArrColl_data(c);

    if(!data)
        return nullptr;

    bytes offset = 0;

    if(ckd_mul(&offset, index, Array_stride(array)))
        return nullptr;

    return data + offset;
}

bool Array_copyOut(const Array *array, bytes index, any dest, bytes elemBytes, string typeName)
{
    if(!array || !dest)
        return false;

    bytes stride = Array_stride(array);

    if(elemBytes != stride) {
        THROW("Array_getAs: size mismatch for '%s' (%zu bytes) on Array<%s> (%zu bytes)",
              typeName ? typeName : "unknown",
              elemBytes,
              Array_typeName(array),
              stride);

        return false;
    }

    any slot = Array_get(array, index);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

bool Array_setBytes(Array *array, bytes index, const_any src, bytes elemBytes)
{
    if(!array || !src)
        return false;

    bytes stride = Array_stride(array);

    if(elemBytes != stride) {
        THROW("Array_set: value width %zu does not match Array<%s> stride %zu",
              elemBytes,
              Array_typeName(array),
              stride);

        return false;
    }

    any slot = Array_get(array, index);

    if(!slot)
        return false;

    memcpy(slot, src, elemBytes);

    return true;
}

bool Array_setPtr(Array *array, bytes index, const_any src)
{
    if(!array || !src)
        return false;

    return Array_setBytes(array, index, src, Array_stride(array));
}

bool Array_fillBytes(Array *array, const_any src, bytes elemBytes)
{
    if(!array || !src)
        return false;

    bytes stride = Array_stride(array);

    if(elemBytes != stride) {
        THROW("Array_fill: value width %zu does not match Array<%s> stride %zu",
              elemBytes,
              Array_typeName(array),
              stride);

        return false;
    }

    bytes count = Array_count(array);
    u8 *data = ArrColl_data((const Collection*) array);

    if(!data)
        return count == 0;

    for(bytes i = 0; i < count; i++)
        memcpy(data + (i * stride), src, stride);

    return true;
}

;;CHECKER
TryValue Array_getTry(const Array *array, bytes index)
{
    if(!array) {
        THROW("array getTry: null array");

        return TryValue_error(TRY_NULL_ARG);
    }

    bytes activeCount = Array_count(array);

    if(index >= activeCount) {
        THROW("array getTry: index %zu out of range (count %zu)", index, activeCount);

        return TryValue_error(TRY_BOUNDS);
    }

    return TryValue_ok(Collection_readSlot((Collection*) array, index));
}

u8 *Array_slot(Array *array, bytes index)
{
    return (u8*) Array_get(array, index);
}

bool Array_isEmpty(const Array *array)
{
    return Array_count(array) == 0;
}

bytes Array_count(const Array *array)
{
    if(!array)
        return 0;

    return (bytes) ArrColl_activeCount((const Collection*) array);
}

bytes Array_size(const Array *array)
{
    return Array_count(array);
}

bytes Array_length(const Array *array)
{
    return Array_count(array);
}

bytes Array_capacity(const Array *array)
{
    return Array_count(array);
}

u32 Array_elementClassId(const Array *array)
{
    return Collection_elementClassId((Collection*) array);
}

bytes Array_stride(const Array *array)
{
    if(!array)
        return 0;

    return ElementLayout_stride(Array_layoutField(array));
}

bytes Array_alignment(const Array *array)
{
    if(!array)
        return 0;

    return ElementLayout_alignment(Array_layoutField(array));
}

string Array_typeName(const Array *array)
{
    if(!array)
        return "nullptr";

    return ElementLayout_typeName(Array_layoutField(array));
}

u8 *Array_data(const Array *array)
{
    if(!array)
        return nullptr;

    return ArrColl_data((const Collection*) array);
}

u8 *Array_dataBuffer(const Array *array)
{
    return Array_data(array);
}

;;GETTER
MemoryArena *Array_arena(const Array *array)
{
    if(!array)
        return nullptr;

    return (*array).arena;
}

fn Array_toString(const Array *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Array(nullptr)"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Array<%s>(length=%zu)", Array_typeName(self), Array_count(self)));
}

fn Array_toStringStruct(const Array *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Array{nullptr}"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Array{type=%s, stride=%zu, align=%zu, length=%zu}",
                         Array_typeName(self), Array_stride(self), Array_alignment(self), Array_count(self)));
}

;;CONSTRUCTOR
Array2D *Array2D_newLayout(ElementLayout layout, bytes width, bytes height, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("Array2D: invalid layout for '%s' (stride=%zu, align=%zu)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout));

        return nullptr;
    }

    if(width == 0 || height == 0)
        return nullptr;

    bytes totalCells = 0;

    if(ckd_mul(&totalCells, width, height) || totalCells > (bytes) UINT32_MAX) {
        (discard) Struct_reportConstructionExhaustion("Array2D", SIZE_MAX);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    Array2D *self = (Array2D*) MemoryArena_alloc(targetArena, TYPE_ARRAY_2D, sizeof(Array2D));

    if(!self) {
        (discard) Struct_reportConstructionExhaustion("Array2D", sizeof(Array2D));

        return nullptr;
    }

    u8 *data = nullptr;
    bytes stride = ElementLayout_stride(layout);

    if(totalCells > 0) {
        bytes byteCount = 0;

        if(ckd_mul(&byteCount, totalCells, stride)) {
            (discard) Struct_reportConstructionExhaustion("Array2D", SIZE_MAX);
            (discard) MemoryArena_free(targetArena, self);

            return nullptr;
        }

        data = (u8*) MemoryArena_alloc(targetArena, TYPE_ARRAY_2D, byteCount);

        if(!data) {
            (discard) Struct_reportConstructionExhaustion("Array2D", byteCount);
            (discard) MemoryArena_free(targetArena, self);

            return nullptr;
        }

        memset(data, 0, byteCount);
    }

    initCollection((Collection*) self, TYPE_ARRAY_2D, 0, stride, totalCells, data);
    Array2D_setArena(self, targetArena);
    Array2D_setLayout(self, layout);
    Array2D_setWidth(self, width);
    Array2D_setHeight(self, height);

    return self;
}

;;DECONSTRUCT
fn Array2D_free(Array2D *self)
{
    if(!self)
        return;

    MemoryArena *arena = Array2D_arena(self);
    u8 *data = ArrColl_data((const Collection*) self);

    if(data) {
        ArrColl_setData((Collection*) self, nullptr);
        (discard) MemoryArena_free(arena, data);
    }

    (discard) MemoryArena_free(arena, self);
}

;;HOTCODE
any Array2D_get(const Array2D *self, bytes x, bytes y)
{
    if(!self)
        return nullptr;

    bytes w = Array2D_width(self);
    bytes h = Array2D_height(self);

    if(x >= w || y >= h)
        return nullptr;

    u8 *data = ArrColl_data((const Collection*) self);

    if(!data)
        return nullptr;

    bytes index = (y * w) + x;

    return data + (index * Array2D_stride(self));
}

bool Array2D_copyOut(const Array2D *self, bytes x, bytes y, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    bytes stride = Array2D_stride(self);

    if(elemBytes != stride) {
        THROW("Array2D_getAs: size mismatch for '%s' (%zu bytes) on Array2D<%s> (%zu bytes)",
              typeName ? typeName : "unknown",
              elemBytes,
              Array2D_typeName(self),
              stride);

        return false;
    }

    any slot = Array2D_get(self, x, y);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

bool Array2D_setBytes(Array2D *self, bytes x, bytes y, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = Array2D_stride(self);

    if(elemBytes != stride) {
        THROW("Array2D_set: value width %zu does not match Array2D<%s> stride %zu",
              elemBytes,
              Array2D_typeName(self),
              stride);

        return false;
    }

    any slot = Array2D_get(self, x, y);

    if(!slot)
        return false;

    memcpy(slot, src, elemBytes);

    return true;
}

bool Array2D_fillBytes(Array2D *self, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = Array2D_stride(self);

    if(elemBytes != stride) {
        THROW("Array2D_fill: value width %zu does not match Array2D<%s> stride %zu",
              elemBytes,
              Array2D_typeName(self),
              stride);

        return false;
    }

    bytes count = Array2D_count(self);
    u8 *data = ArrColl_data((const Collection*) self);

    if(!data)
        return count == 0;

    for(bytes i = 0; i < count; i++)
        memcpy(data + (i * stride), src, stride);

    return true;
}

;;GETTER
bytes Array2D_width(const Array2D *self)
{
    if(!self)
        return 0;

    return (*self).width;
}

;;GETTER
bytes Array2D_height(const Array2D *self)
{
    if(!self)
        return 0;

    return (*self).height;
}

bytes Array2D_count(const Array2D *self)
{
    if(!self)
        return 0;

    return Array2D_width(self) * Array2D_height(self);
}

bytes Array2D_stride(const Array2D *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Array2D_layoutField(self));
}

string Array2D_typeName(const Array2D *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Array2D_layoutField(self));
}

u8 *Array2D_data(const Array2D *self)
{
    if(!self)
        return nullptr;

    return ArrColl_data((const Collection*) self);
}

;;GETTER
MemoryArena *Array2D_arena(const Array2D *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

fn Array2D_toString(const Array2D *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Array2D(nullptr)"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Array2D<%s>(%zux%zu)",
                         Array2D_typeName(self), Array2D_width(self), Array2D_height(self)));
}

fn Array2D_toStringStruct(const Array2D *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Array2D{nullptr}"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Array2D{type=%s, stride=%zu, width=%zu, height=%zu}",
                         Array2D_typeName(self), Array2D_stride(self), Array2D_width(self), Array2D_height(self)));
}

;;CONSTRUCTOR
Array3D *Array3D_newLayout(ElementLayout layout, bytes width, bytes height, bytes depth, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("Array3D: invalid layout for '%s' (stride=%zu, align=%zu)",
              ElementLayout_typeName(layout),
              ElementLayout_stride(layout),
              ElementLayout_alignment(layout));

        return nullptr;
    }

    if(width == 0 || height == 0 || depth == 0)
        return nullptr;

    bytes sliceCells = 0;
    bytes totalCells = 0;

    if(ckd_mul(&sliceCells, width, height)
       || ckd_mul(&totalCells, sliceCells, depth)
       || totalCells > (bytes) UINT32_MAX) {
        (discard) Struct_reportConstructionExhaustion("Array3D", SIZE_MAX);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    Array3D *self = (Array3D*) MemoryArena_alloc(targetArena, TYPE_ARRAY_3D, sizeof(Array3D));

    if(!self) {
        (discard) Struct_reportConstructionExhaustion("Array3D", sizeof(Array3D));

        return nullptr;
    }

    u8 *data = nullptr;
    bytes stride = ElementLayout_stride(layout);

    if(totalCells > 0) {
        bytes byteCount = 0;

        if(ckd_mul(&byteCount, totalCells, stride)) {
            (discard) Struct_reportConstructionExhaustion("Array3D", SIZE_MAX);
            (discard) MemoryArena_free(targetArena, self);

            return nullptr;
        }

        data = (u8*) MemoryArena_alloc(targetArena, TYPE_ARRAY_3D, byteCount);

        if(!data) {
            (discard) Struct_reportConstructionExhaustion("Array3D", byteCount);
            (discard) MemoryArena_free(targetArena, self);

            return nullptr;
        }

        memset(data, 0, byteCount);
    }

    initCollection((Collection*) self, TYPE_ARRAY_3D, 0, stride, totalCells, data);
    Array3D_setArena(self, targetArena);
    Array3D_setLayout(self, layout);
    Array3D_setWidth(self, width);
    Array3D_setHeight(self, height);
    Array3D_setDepth(self, depth);

    return self;
}

;;DECONSTRUCT
fn Array3D_free(Array3D *self)
{
    if(!self)
        return;

    MemoryArena *arena = Array3D_arena(self);
    u8 *data = ArrColl_data((const Collection*) self);

    if(data) {
        ArrColl_setData((Collection*) self, nullptr);
        (discard) MemoryArena_free(arena, data);
    }

    (discard) MemoryArena_free(arena, self);
}

;;HOTCODE
any Array3D_get(const Array3D *self, bytes x, bytes y, bytes z)
{
    if(!self)
        return nullptr;

    bytes w = Array3D_width(self);
    bytes h = Array3D_height(self);
    bytes d = Array3D_depth(self);

    if(x >= w || y >= h || z >= d)
        return nullptr;

    u8 *data = ArrColl_data((const Collection*) self);

    if(!data)
        return nullptr;

    bytes index = (((z * h) + y) * w) + x;

    return data + (index * Array3D_stride(self));
}

bool Array3D_copyOut(const Array3D *self, bytes x, bytes y, bytes z, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    bytes stride = Array3D_stride(self);

    if(elemBytes != stride) {
        THROW("Array3D_getAs: size mismatch for '%s' (%zu bytes) on Array3D<%s> (%zu bytes)",
              typeName ? typeName : "unknown",
              elemBytes,
              Array3D_typeName(self),
              stride);

        return false;
    }

    any slot = Array3D_get(self, x, y, z);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

bool Array3D_setBytes(Array3D *self, bytes x, bytes y, bytes z, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = Array3D_stride(self);

    if(elemBytes != stride) {
        THROW("Array3D_set: value width %zu does not match Array3D<%s> stride %zu",
              elemBytes,
              Array3D_typeName(self),
              stride);

        return false;
    }

    any slot = Array3D_get(self, x, y, z);

    if(!slot)
        return false;

    memcpy(slot, src, elemBytes);

    return true;
}

bool Array3D_fillBytes(Array3D *self, const_any src, bytes elemBytes)
{
    if(!self || !src)
        return false;

    bytes stride = Array3D_stride(self);

    if(elemBytes != stride) {
        THROW("Array3D_fill: value width %zu does not match Array3D<%s> stride %zu",
              elemBytes,
              Array3D_typeName(self),
              stride);

        return false;
    }

    bytes count = Array3D_count(self);
    u8 *data = ArrColl_data((const Collection*) self);

    if(!data)
        return count == 0;

    for(bytes i = 0; i < count; i++)
        memcpy(data + (i * stride), src, stride);

    return true;
}

;;GETTER
bytes Array3D_width(const Array3D *self)
{
    if(!self)
        return 0;

    return (*self).width;
}

;;GETTER
bytes Array3D_height(const Array3D *self)
{
    if(!self)
        return 0;

    return (*self).height;
}

;;GETTER
bytes Array3D_depth(const Array3D *self)
{
    if(!self)
        return 0;

    return (*self).depth;
}

bytes Array3D_count(const Array3D *self)
{
    if(!self)
        return 0;

    return Array3D_width(self) * Array3D_height(self) * Array3D_depth(self);
}

bytes Array3D_stride(const Array3D *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Array3D_layoutField(self));
}

string Array3D_typeName(const Array3D *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Array3D_layoutField(self));
}

u8 *Array3D_data(const Array3D *self)
{
    if(!self)
        return nullptr;

    return ArrColl_data((const Collection*) self);
}

;;GETTER
MemoryArena *Array3D_arena(const Array3D *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

fn Array3D_toString(const Array3D *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Array3D(nullptr)"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Array3D<%s>(%zux%zux%zu)",
                         Array3D_typeName(self), Array3D_width(self),
                         Array3D_height(self), Array3D_depth(self)));
}

fn Array3D_toStringStruct(const Array3D *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        formatWrite(dest, cap, outTruncated, snprintf(dest, cap, "Array3D{nullptr}"));

        return;
    }

    formatWrite(dest, cap, outTruncated,
                snprintf(dest, cap, "Array3D{type=%s, stride=%zu, width=%zu, height=%zu, depth=%zu}",
                         Array3D_typeName(self), Array3D_stride(self), Array3D_width(self),
                         Array3D_height(self), Array3D_depth(self)));
}
