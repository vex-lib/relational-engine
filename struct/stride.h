#ifndef RE_STRUCT_STRIDE_H
#define RE_STRUCT_STRIDE_H

#include "vex.h"

/**
 * re/struct/stride.h, the Stride registry (built-in scalars + custom structs).
 *
 * Maps a class id to the byte width of one slot of that class. Containers in
 * `re/struct/` ask Stride_get(element_class) when they allocate so they know
 * how many bytes each slot occupies. Unknown ids default to 8 (pointer-sized);
 * custom structs register their exact `sizeof(MyStruct)` via Stride_register().
 */

/**
 * Byte width of one element of the given class. Checks the built-in switch
 * table first, then the custom-struct registry, falling back to 8.
 */
bytes Stride_get(u32 class_id);

/**
 * Register (or update) a custom struct byte width for `class_id` (masked to
 * its 24-bit class field). Custom struct strides must be non-zero multiples
 * of 16 bytes (declare the struct with `alignas(16)` so `sizeof(Type)` matches
 * the 16-byte container slot stride); built-in scalars keep 1, 2, 4, 8 and
 * cannot be overridden.
 */
bool Stride_register(u64 class_id, bytes stride);

/**
 * Register a C struct type at compile time after asserting `_Alignof(Type) >= 16`
 * and `(sizeof(Type) & 15u) == 0u`, so pointer arithmetic and slot indexing match.
 */
#define STRIDE_REGISTER(class_id, Type)                                          \
    ({                                                                           \
        _Static_assert(_Alignof(Type) >= 16 && (sizeof(Type) & 15u) == 0u,       \
                       "STRIDE_REGISTER: custom struct must be alignas(16)");    \
        Stride_register((class_id), sizeof(Type));                               \
    })

/**
 * Custom stride table entry pairing a masked class id with its byte stride.
 */
typedef struct CustomStrideEntry {
    u64 id;
    bytes stride;
} CustomStrideEntry;

/** True if `class_id` currently has a custom registration in this process. */
bool Stride_isRegistered(u64 class_id);

;;TEST
fn Stride_resetCustom();

#endif
