#ifndef RE_ROW_HANDLE_H
#define RE_ROW_HANDLE_H
#include "vex.h"

/* Process-local identity, not an address/type ID/serialized record. Do not change
 * issued fields. Zero is invalid; reserved must be zero. Exact repr(C) Rust peer:
 * nio/row_handle.rs. Owner distinguishes pools while engine code remains resident. */
typedef struct ReRowHandle {
    u64 owner;
    bytes index;
    u32 generation;
    u32 reserved;
} ReRowHandle;

#define ReRowHandle_zero() ((ReRowHandle) {0})
#endif
