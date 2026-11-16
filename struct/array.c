#include "vex.h"
#include "nio/mem.h"

#include "struct/array.h"
#include "struct/stride.h"
#include "type/re_type.h"
#include "struct/exhaustion.h"
#include "exception/try_value.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Array
 * ============================================================================
 * The Array class, ported from struct/Array.java: a typed contiguous buffer
 * under a Collection header. Construction resolves the element stride from
 * the class registry, allocates the header and the data buffer from the
 * memory arena, and zero-fills the payload. Array_get/set read and write
 * slots as uint64 through the Collection slot machinery with bounds checks;
 * Array_slot returns a typed pointer for direct access. Free releases the
 * data buffer before the header.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Array (struct/array.c)
 * ============================================================================
 * the Array class, ported from struct/Array.java.
 *
 * STRUCT FIELDS (Mirroring struct/array.h):
 * ----------------------------------------------------------------------------
 *   Array {
 *     Collection collection; // instance state
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Array_2(element_class, length)
 *
 * Core Functions:
 *   - Array_free(array)
 *   - Array_slot(array, index)
 *   - Array_size(array)
 *   - Array_length(array)
 *   - Array_capacity(array)
 *   - Array_elementClassId(array)
 *   - Array_stride(array)
 *   - Array_dataBuffer(array)
 *
 * Setters:
 *   - Array_set(array, index, value)
 *
 * Getters:
 *   - Array_get(array, index)
 *   - Array_getTry(array, index)
 *   - Array_isEmpty(array)
 * ============================================================================
 */


// array.c — Array port (Legacy: struct/Array.java).

;;SETTER
static inline fn Collection_setTypeId(Collection *c, u64 typeId) { (*c).typeId = typeId; }
;;GETTER
static inline u32 Collection_activeCount(const Collection *c) { return (*c).activeCount; }
;;SETTER
static inline fn Collection_setActiveCount(Collection *c, u32 count) { (*c).activeCount = count; }
;;SETTER
static inline fn Collection_setElementClass(Collection *c, u32 cls) { (*c).elementClass = cls; }
;;SETTER
static inline fn Collection_setStride(Collection *c, u32 stride) { (*c).stride = stride; }
;;SETTER
static inline fn Collection_setCapacity(Collection *c, u32 cap) { (*c).capacity = cap; }
;;SETTER
static inline fn Collection_setHead(Collection *c, u32 head) { (*c).head = head; }
;;SETTER
static inline fn Collection_setExhaustionCount(Collection *c, u64 count) { (*c).exhaustionCount = count; }
;;SETTER
static inline fn Collection_setExhaustionReported(Collection *c, bool rep) { (*c).exhaustionReported = rep; }
;;GETTER
static inline u8 *Collection_data(const Collection *c) { return (*c).data; }
;;SETTER
static inline fn Collection_setData(Collection *c, u8 *data) { (*c).data = data; }

static Collection *asCollection(Array *array) {
    return (Collection*) array;
}

Array *Array_2(u32 elementClass, bytes length) {
    bytes stride = Stride_get(elementClass);

    Array *array = (Array*) Memory_alloc(TYPE_ARRAY, sizeof(Array));

    if (!array) {
        (discard) Struct_reportConstructionExhaustion("array", sizeof(Array));

        return nullptr;
    }

    Collection *c = asCollection(array);

    Collection_setTypeId(c, TYPE_ARRAY);

    Collection_setActiveCount(c, (u32)length);

    Collection_setElementClass(c, elementClass);

    Collection_setStride(c, (u32)stride);

    Collection_setCapacity(c, (u32)length);

    Collection_setHead(c, 0);

    Collection_setExhaustionCount(c, 0);

    Collection_setExhaustionReported(c, false);

    bytes Bytes = length * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    u8 *data = (u8*) Memory_alloc(bufType, Bytes);
    Collection_setData(c, data);

    if (!data) {
        (discard) Struct_reportConstructionExhaustion("array", Bytes);

        Memory_free(array);

        return nullptr;
    }

    memset(data, 0, Bytes);

    return array;
}

fn Array_free(Array *array) {
    if (!array) 
       return;

    Collection *c = asCollection(array);
    u8 *data = Collection_data(c);
    if (data) 
        Memory_free(data);

    Memory_free(array);
}

;;HOTCODE
u64 Array_get(Array *array, bytes index) {
    if (!array) 
       return 0;

    Collection *c = asCollection(array);

    if (index >= Collection_activeCount(c)) 
        return 0;

    return Collection_readSlot(c, index);
}

;;CHECKER
TryValue Array_getTry(Array *array, bytes index) {
    if (array == nullptr) {
        THROW("array getTry: null array");

        return TryValue_error(TRY_NULL_ARG);
    }

    Collection *c = asCollection(array);
    u32 activeCount = Collection_activeCount(c);
    if (index >= activeCount) {
        THROW("array getTry: index %zu out of range (count %u)", index, activeCount);

        return TryValue_error(TRY_BOUNDS);
    }

    return TryValue_ok(Collection_readSlot(c, index));
}

fn Array_set(Array *array, bytes index, u64 value) {
    if (!array) 
       return;

    Collection *c = asCollection(array);

    if (index >= Collection_activeCount(c)) 
        return;

    Collection_writeSlot(c, index, value);
}

u8 *Array_slot(Array *array, bytes index) {
    if (!array) 
       return nullptr;

    Collection *c = asCollection(array);

    if (index >= Collection_activeCount(c)) 
        return nullptr;

    return Collection_data(c) + index * Collection_stride(c);
}

bool Array_isEmpty(Array *array) {
    return Collection_isEmpty(asCollection(array));
}

bytes Array_size(Array *array) {
    return Collection_size(asCollection(array));
}

bytes Array_length(Array *array) {
    return Collection_length(asCollection(array));
}

bytes Array_capacity(Array *array) {
    return Collection_capacity(asCollection(array));
}

u32 Array_elementClassId(Array *array) {
    return Collection_elementClassId(asCollection(array));
}

bytes Array_stride(Array *array) {
    return Collection_stride(asCollection(array));
}

u8 *Array_dataBuffer(Array *array) {
    return Collection_dataBuffer(asCollection(array));
}
