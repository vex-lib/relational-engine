#ifndef STRUCT_ARRAY_H
#define STRUCT_ARRAY_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"
#include "exception/try_value.h"

/**
 * struct/array.h, arena-backed raw typed arrays (arrayOf) plus fixed-length
 * flat DOD Array, Array2D, and Array3D containers.
 *
 * Raw typed arena array (zero wrapper struct; length lives in the 32-byte
 * self-describing MemoryHeader):
 *   u32 *nums = arrayOf(u32, 64);
 *   Class *items = arrayOf(Class, 16, myArena);
 *   bytes n = arrayLength(nums);
 *   nums = arrayResize(nums, 128);
 *   arrayFree(nums);
 *
 * Managed fixed-length 1D / 2D / 3D flat DOD arrays:
 *   Array   *a1 = Array(Class, 32);
 *   Array2D *a2 = Array2D(Tile, 128, 128);
 *   Array3D *a3 = Array3D(Voxel, 16, 16, 16);
 */

/**
 * Fixed-length 1D array embedding Collection at offset zero.
 *
 * Collection collection - embedded Collection metadata and flat data buffer
 * MemoryArena *arena    - borrowed MemoryArena owning this array and buffer
 * ElementLayout layout  - element stride, alignment, type hash, and type name
 */
;;CLASSNAME("Array")
typedef struct Array {
    Collection collection;
    MemoryArena *arena;
    ElementLayout layout;
} Array;

/**
 * Flat contiguous 2D array backed by one row-major buffer (y * width + x).
 *
 * Collection collection - embedded Collection metadata and flat data buffer
 * MemoryArena *arena    - borrowed MemoryArena owning this array and buffer
 * ElementLayout layout  - element stride, alignment, type hash, and type name
 * bytes width           - number of columns (X extent)
 * bytes height          - number of rows (Y extent)
 */
;;CLASSNAME("Array2D")
typedef struct Array2D {
    Collection collection;
    MemoryArena *arena;
    ElementLayout layout;
    bytes width;
    bytes height;
} Array2D;

/**
 * Flat contiguous 3D array backed by one slice-major buffer ((z * height + y) * width + x).
 *
 * Collection collection - embedded Collection metadata and flat data buffer
 * MemoryArena *arena    - borrowed MemoryArena owning this array and buffer
 * ElementLayout layout  - element stride, alignment, type hash, and type name
 * bytes width           - X extent
 * bytes height          - Y extent
 * bytes depth           - Z extent
 */
;;CLASSNAME("Array3D")
typedef struct Array3D {
    Collection collection;
    MemoryArena *arena;
    ElementLayout layout;
    bytes width;
    bytes height;
    bytes depth;
} Array3D;

/** Raw arena-backed typed array helpers powering arrayOf / arrayLength / arrayResize / arrayFree. */
any Array_allocRaw(ElementLayout layout, bytes count, MemoryArena *arena);
any Array_resizeRaw(any ptr, bytes stride, bytes alignment, bytes newCount);
bytes Array_rawLength(const_any ptr, bytes stride);
fn Array_freeRaw(any ptr);
fn Array_freeRawIn(MemoryArena *arena, any ptr);

#define arrayOf_2(T, count)        ((typeof_unqual(T)*) Array_allocRaw(ElementLayout_of(T), (count), Re_arena()))
#define arrayOf_3(T, count, arena) ((typeof_unqual(T)*) Array_allocRaw(ElementLayout_of(T), (count), (arena)))
#define arrayOf(...)               CONSTRUCTOR_DISPATCH(arrayOf, __VA_ARGS__)

#define arrayLength(ptr)           Array_rawLength((const_any) (ptr), sizeof(*(ptr)))
#define arrayBytes(ptr)            Memory_length((any) (ptr))
#define arrayResize(ptr, newCount) ((typeof(ptr)) Array_resizeRaw((any) (ptr), sizeof(*(ptr)), alignof(typeof(*(ptr))), (newCount)))
#define arrayFree(ptr)             Array_freeRaw((any) (ptr))
#define arrayFreeIn(arena, ptr)    Array_freeRawIn((arena), (any) (ptr))

/** Managed 1D Array constructors and operations. */
Array *Array_newLayout(ElementLayout layout, bytes length, MemoryArena *arena);
Array *Array_2(u32 elementClass, bytes length);

#define Array_ctor_2(T, length)        Array_newLayout(ElementLayout_of(T), (length), Re_arena())
#define Array_ctor_3(T, length, arena) Array_newLayout(ElementLayout_of(T), (length), (arena))
#define Array(...)                     CONSTRUCTOR_DISPATCH(Array_ctor, __VA_ARGS__)

fn Array_free(Array *array);
any Array_get(const Array *array, bytes index);
bool Array_copyOut(const Array *array, bytes index, any dest, bytes elemBytes, string typeName);
bool Array_setBytes(Array *array, bytes index, const_any src, bytes elemBytes);
bool Array_setPtr(Array *array, bytes index, const_any src);
bool Array_fillBytes(Array *array, const_any src, bytes elemBytes);
TryValue Array_getTry(const Array *array, bytes index);
u8 *Array_slot(Array *array, bytes index);

bool Array_isEmpty(const Array *array);
bytes Array_count(const Array *array);
bytes Array_size(const Array *array);
bytes Array_length(const Array *array);
bytes Array_capacity(const Array *array);
u32 Array_elementClassId(const Array *array);
bytes Array_stride(const Array *array);
bytes Array_alignment(const Array *array);
string Array_typeName(const Array *array);
u8 *Array_data(const Array *array);
u8 *Array_dataBuffer(const Array *array);
MemoryArena *Array_arena(const Array *array);
fn Array_toString(const Array *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Array_toStringStruct(const Array *self, utf8 *dest, bytes cap, bool *outTruncated);

#define Array_set(arr, index, value)                                          \
    ({                                                                        \
        auto _vex_arr_set_val = (value);                                      \
        Array_setBytes((arr), (index), &_vex_arr_set_val,                     \
                       sizeof(_vex_arr_set_val));                             \
    })

#define Array_fill(arr, value)                                                \
    ({                                                                        \
        auto _vex_arr_fill_val = (value);                                     \
        Array_fillBytes((arr), &_vex_arr_fill_val,                            \
                        sizeof(_vex_arr_fill_val));                           \
    })

#define Array_getAs(arr, index, T)                                            \
    ({                                                                        \
        typeof_unqual(T) _vex_arr_get_out;                                    \
        memset(&_vex_arr_get_out, 0, sizeof(_vex_arr_get_out));               \
        (discard) Array_copyOut((arr), (index), &_vex_arr_get_out,            \
                                sizeof(_vex_arr_get_out), #T);                \
        _vex_arr_get_out;                                                     \
    })

/** Managed flat 2D Array2D constructors and operations. */
Array2D *Array2D_newLayout(ElementLayout layout, bytes width, bytes height, MemoryArena *arena);

#define Array2D_ctor_3(T, w, h)        Array2D_newLayout(ElementLayout_of(T), (w), (h), Re_arena())
#define Array2D_ctor_4(T, w, h, arena) Array2D_newLayout(ElementLayout_of(T), (w), (h), (arena))
#define Array2D(...)                   CONSTRUCTOR_DISPATCH(Array2D_ctor, __VA_ARGS__)

fn Array2D_free(Array2D *self);
any Array2D_get(const Array2D *self, bytes x, bytes y);
bool Array2D_copyOut(const Array2D *self, bytes x, bytes y, any dest, bytes elemBytes, string typeName);
bool Array2D_setBytes(Array2D *self, bytes x, bytes y, const_any src, bytes elemBytes);
bool Array2D_fillBytes(Array2D *self, const_any src, bytes elemBytes);
bytes Array2D_width(const Array2D *self);
bytes Array2D_height(const Array2D *self);
bytes Array2D_count(const Array2D *self);
bytes Array2D_stride(const Array2D *self);
string Array2D_typeName(const Array2D *self);
u8 *Array2D_data(const Array2D *self);
MemoryArena *Array2D_arena(const Array2D *self);
fn Array2D_toString(const Array2D *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Array2D_toStringStruct(const Array2D *self, utf8 *dest, bytes cap, bool *outTruncated);

#define Array2D_set(grid, x, y, value)                                        \
    ({                                                                        \
        auto _vex_a2d_set_val = (value);                                      \
        Array2D_setBytes((grid), (x), (y), &_vex_a2d_set_val,                 \
                         sizeof(_vex_a2d_set_val));                           \
    })

#define Array2D_fill(grid, value)                                             \
    ({                                                                        \
        auto _vex_a2d_fill_val = (value);                                     \
        Array2D_fillBytes((grid), &_vex_a2d_fill_val,                         \
                          sizeof(_vex_a2d_fill_val));                         \
    })

#define Array2D_getAs(grid, x, y, T)                                          \
    ({                                                                        \
        typeof_unqual(T) _vex_a2d_get_out;                                    \
        memset(&_vex_a2d_get_out, 0, sizeof(_vex_a2d_get_out));               \
        (discard) Array2D_copyOut((grid), (x), (y), &_vex_a2d_get_out,        \
                                  sizeof(_vex_a2d_get_out), #T);              \
        _vex_a2d_get_out;                                                     \
    })

/** Managed flat 3D Array3D constructors and operations. */
Array3D *Array3D_newLayout(ElementLayout layout, bytes width, bytes height, bytes depth, MemoryArena *arena);

#define Array3D_ctor_4(T, w, h, d)        Array3D_newLayout(ElementLayout_of(T), (w), (h), (d), Re_arena())
#define Array3D_ctor_5(T, w, h, d, arena) Array3D_newLayout(ElementLayout_of(T), (w), (h), (d), (arena))
#define Array3D(...)                      CONSTRUCTOR_DISPATCH(Array3D_ctor, __VA_ARGS__)

fn Array3D_free(Array3D *self);
any Array3D_get(const Array3D *self, bytes x, bytes y, bytes z);
bool Array3D_copyOut(const Array3D *self, bytes x, bytes y, bytes z, any dest, bytes elemBytes, string typeName);
bool Array3D_setBytes(Array3D *self, bytes x, bytes y, bytes z, const_any src, bytes elemBytes);
bool Array3D_fillBytes(Array3D *self, const_any src, bytes elemBytes);
bytes Array3D_width(const Array3D *self);
bytes Array3D_height(const Array3D *self);
bytes Array3D_depth(const Array3D *self);
bytes Array3D_count(const Array3D *self);
bytes Array3D_stride(const Array3D *self);
string Array3D_typeName(const Array3D *self);
u8 *Array3D_data(const Array3D *self);
MemoryArena *Array3D_arena(const Array3D *self);
fn Array3D_toString(const Array3D *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Array3D_toStringStruct(const Array3D *self, utf8 *dest, bytes cap, bool *outTruncated);

#define Array3D_set(vol, x, y, z, value)                                      \
    ({                                                                        \
        auto _vex_a3d_set_val = (value);                                      \
        Array3D_setBytes((vol), (x), (y), (z), &_vex_a3d_set_val,             \
                         sizeof(_vex_a3d_set_val));                           \
    })

#define Array3D_fill(vol, value)                                              \
    ({                                                                        \
        auto _vex_a3d_fill_val = (value);                                     \
        Array3D_fillBytes((vol), &_vex_a3d_fill_val,                          \
                          sizeof(_vex_a3d_fill_val));                         \
    })

#define Array3D_getAs(vol, x, y, z, T)                                        \
    ({                                                                        \
        typeof_unqual(T) _vex_a3d_get_out;                                    \
        memset(&_vex_a3d_get_out, 0, sizeof(_vex_a3d_get_out));               \
        (discard) Array3D_copyOut((vol), (x), (y), (z), &_vex_a3d_get_out,    \
                                  sizeof(_vex_a3d_get_out), #T);              \
        _vex_a3d_get_out;                                                     \
    })

#endif
