#ifndef EXCEPTION_TRY_VALUE_H
#define EXCEPTION_TRY_VALUE_H

#include "vex.h"
#include "exception/try_code.h"

;;DRAFT
// exception/try_value.h — the scalar value-or-error return.
//
// TryValue is a by-value result: { value, code }. It exists because a getter
// whose domain is full — i32, uint64, bool, any scalar — has no spare codepoint
// left for a sentinel; a Try is the only way to distinguish "value is 0" from
// "index out of bounds" without an out-parameter. It returns in registers, never
// allocates, and is copied freely up the call chain (the Vertical Integration
// Law: a leaf forwards the Try, the host catches it at its edge).
//
// CONTRACT: `value` is valid ONLY when `code == TRY_OK`.

typedef struct TryValue {
    u64 value;   // success payload; undefined unless code == TRY_OK
    TryCode  code;    // TRY_OK or the named reason
} TryValue;

// --- Constructors (by value: no allocation, no pointer) ---
TryValue TryValue_2(u64 value, TryCode code);
TryValue TryValue_ok(u64 value);
TryValue TryValue_error(TryCode code);
#define TryValue(...) CONSTRUCTOR_DISPATCH(TryValue, __VA_ARGS__)

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law) ---
u64 TryValue_getValue(const TryValue *self);
fn     TryValue_setValue(TryValue *self, u64 value);
TryCode  TryValue_getCode(const TryValue *self);
fn     TryValue_setCode(TryValue *self, TryCode code);
bool     TryValue_isOk(const TryValue *self);

// --- String projections (the toString Law) ---
fn TryValue_toString(const TryValue *self, char *dest, bytes cap, bool *outTruncated);
fn TryValue_toStringStruct(const TryValue *self, char *dest, bytes cap, bool *outTruncated);

#endif
