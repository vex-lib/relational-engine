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

    (*c).typeId = TYPE_ARRAY;

    (*c).activeCount = (u32)length;

    (*c).elementClass = elementClass;

    (*c).stride = (u32)stride;

    (*c).capacity = (u32)length;

    (*c).head = 0;

    (*c).exhaustionCount = 0;

    (*c).exhaustionReported = false;

    bytes Bytes = length * stride;

    u64 bufType = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, elementClass);

    (*c).data = (u8*) Memory_alloc(bufType, Bytes);

    if (!(*c).data) {
        (discard) Struct_reportConstructionExhaustion("array", Bytes);

        Memory_free(array);

        return nullptr;
    }

    memset((*c).data, 0, Bytes);

    return array;
}

fn Array_free(Array *array) {
    if (!array) 
       return;

    Collection *c = asCollection(array);

    if ((*c).data) 
        Memory_free((*c).data);

    Memory_free(array);
}

;;HOTCODE
u64 Array_get(Array *array, bytes index) {
    if (!array) 
       return 0;

    Collection *c = asCollection(array);

    if (index >= (*c).activeCount) 
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

    if (index >= (*c).activeCount) {
        THROW("array getTry: index %zu out of range (count %u)", index, (*c).activeCount);

        return TryValue_error(TRY_BOUNDS);
    }

    return TryValue_ok(Collection_readSlot(c, index));
}

fn Array_set(Array *array, bytes index, u64 value) {
    if (!array) 
       return;

    Collection *c = asCollection(array);

    if (index >= (*c).activeCount) 
        return;

    Collection_writeSlot(c, index, value);
}

u8 *Array_slot(Array *array, bytes index) {
    if (!array) 
       return nullptr;

    Collection *c = asCollection(array);

    if (index >= (*c).activeCount) 
        return nullptr;

    return (*c).data + index * (*c).stride;
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
