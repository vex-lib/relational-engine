#include "vex.h"
#include "atomic/atomic.h"
#include "debug/throw.h"
#include "struct/stride.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Stride
 * ============================================================================
 * Class byte-width metadata registry: answers the slot width of any built-in
 * scalar/object class by a fast static switch, plus thread-safe runtime
 * registration for custom user/app structs via Stride_register().
 * Zero heap allocation: the custom table is a fixed static array guarded by
 * SpinLock so containers (`List`, `Array`, `Stack`, `Deque`, `Queue`,
 * `ChunkedList`) can store arbitrary value structs cleanly.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Stride (re/struct/stride.c)
 * ============================================================================
 * STRUCT FIELDS (custom entry):
 *   u64 id;
 *   bytes stride;
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 *   - Stride_get(class_id)
 *   - Stride_register(class_id, stride)
 *   - Stride_isRegistered(class_id)
 *   - Stride_resetCustom()   [;;TEST]
 * ============================================================================
 */

#define MAX_CUSTOM_STRIDES 512u

static SpinLock g_stride_lock = SPIN_LOCK_INIT;
static CustomStrideEntry g_custom_strides[MAX_CUSTOM_STRIDES];
static u32 g_custom_count = 0u;

static bytes builtinStride(u64 id, bool *out_found) {
    *out_found = true;

    switch (id) {
        case ID_BYTE:
            return 1;
        case ID_SHORT:
            return 2;
        case ID_INT:
        case ID_FLOAT:
        case ID_BIT8:
        case ID_BIT16:
        case ID_BIT32:
            return 4;
        case ID_LONG:
        case ID_f64:
        case ID_FIXED64:
        case ID_BIT64:
        case ID_STRING:
        case ID_ARGUMENTS:
            return 8;
        case ID_VARIABLE:
            return 40;
        case ID_VEC2:
            return 8;
        case ID_VEC3:
            return 12;
        case ID_VEC4:
            return 16;
        case ID_MAT3:
            return 36;
        case ID_MAT4:
            return 64;
        case ID_QUATERNION:
            return 16;
        case ID_ENTITY:
            return 8;
        case ID_RANDOM:
            return 16;
        case ID_LIST:
        case ID_MAP:
        case ID_SET:
        case ID_STACK:
        case ID_DEQUE:
        case ID_QUEUE:
        case ID_MIN_HEAP:
        case ID_SPARSE_SET:
        case ID_SPINLOCK:
        case ID_RING_BUFFER:
        case ID_INDEX_RANDOM:
            return 8;
        default:
            *out_found = false;

            return 8;
    }
}

static i32 findCustomSlotLocked(u64 id) {
    for (u32 i = 0; i < g_custom_count; i++) {
        if (g_custom_strides[i].id == id) 
            return (i32) i;
    }

    return -1;
}

bytes Stride_get(u32 class_id) {
    u64 id = (u64) class_id & MASK_CLASS;

    bool found = false;

    bytes width = builtinStride(id, &found);

    if (found) 
        return width;

    SpinLock_lock(&g_stride_lock);

    i32 slot = findCustomSlotLocked(id);

    bytes result = (slot >= 0) ? g_custom_strides[slot].stride : 8u;

    SpinLock_unlock(&g_stride_lock);

    return result;
}

bool Stride_register(u64 class_id, bytes stride) {
    u64 id = class_id & MASK_CLASS;

    if (id == 0u || stride == 0u || (stride & 15u) != 0u) {
        THROW("stride: custom stride must be a non-zero multiple of 16 (id=%" PRIu64 ", stride=%zu)",
              id, stride);

        return false;
    }

    bool builtin = false;

    (discard) builtinStride(id, &builtin);

    if (builtin) {
        THROW("stride: cannot override built-in class id %" PRIu64, id);

        return false;
    }

    SpinLock_lock(&g_stride_lock);

    i32 existing = findCustomSlotLocked(id);

    if (existing >= 0) {
        g_custom_strides[existing].stride = stride;

        SpinLock_unlock(&g_stride_lock);

        return true;
    }

    if (g_custom_count >= MAX_CUSTOM_STRIDES) {
        SpinLock_unlock(&g_stride_lock);

        THROW("stride: custom registry full (%u entries)", MAX_CUSTOM_STRIDES);

        return false;
    }

    g_custom_strides[g_custom_count] = (CustomStrideEntry){ .id = id, .stride = stride };

    g_custom_count++;

    SpinLock_unlock(&g_stride_lock);

    return true;
}

bool Stride_isRegistered(u64 class_id) {
    u64 id = class_id & MASK_CLASS;

    if (id == 0u) 
        return false;

    SpinLock_lock(&g_stride_lock);

    bool present = findCustomSlotLocked(id) >= 0;

    SpinLock_unlock(&g_stride_lock);

    return present;
}

;;TEST
fn Stride_resetCustom() {
    SpinLock_lock(&g_stride_lock);

    for (u32 i = 0; i < g_custom_count; i++) {
        g_custom_strides[i] = (CustomStrideEntry){0};
    }

    g_custom_count = 0u;

    SpinLock_unlock(&g_stride_lock);
}
