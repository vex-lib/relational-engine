#ifndef EXCEPTION_TRY_PTR_H
#define EXCEPTION_TRY_PTR_H

#include "vex.h"
#include "exception/try_code.h"

;;DRAFT
// exception/try_ptr.h — the pointer value-or-error return.
//
// TryPtr is the pointer-shaped sibling of TryValue: { value, code }. It earns
// its keep over a bare null pointer only when nullptr is itself a legal value,
// or when the reason a lookup failed must survive to the caller (the reason does
// not fit a sentinel). For a plain "absent" pointer, a bare nullptr is still the
// free, universal sentinel and needs no Try. Returns in registers, never
// allocates.
//
// CONTRACT: `value` is valid ONLY when `code == TRY_OK`.

typedef struct TryPtr {
    any value;   // success payload; undefined unless code == TRY_OK
    TryCode  code;    // TRY_OK or the named reason
} TryPtr;

// --- Constructors (by value: no allocation) ---
TryPtr TryPtr_2(any value, TryCode code);
TryPtr TryPtr_ok(any value);
TryPtr TryPtr_error(TryCode code);
#define TryPtr(...) CONSTRUCTOR_DISPATCH(TryPtr, __VA_ARGS__)

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law) ---
any TryPtr_getValue(const TryPtr *self);
fn    TryPtr_setValue(TryPtr *self, any value);
TryCode TryPtr_getCode(const TryPtr *self);
fn    TryPtr_setCode(TryPtr *self, TryCode code);
bool    TryPtr_isOk(const TryPtr *self);

// --- String projections (the toString Law) ---
fn TryPtr_toString(const TryPtr *self, char *dest, bytes cap, bool *outTruncated);
fn TryPtr_toStringStruct(const TryPtr *self, char *dest, bytes cap, bool *outTruncated);

#endif
