#include "vex.h"
#include "exception/try_value.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: TryValue
 * ============================================================================
 * The scalar value-or-error return. TryValue_2 builds the pair; TryValue_ok and
 * TryValue_error are the two ergonomic shorthands a call site reads. It is a
 * by-value POD, so it lives on the stack, is returned in registers, and is never
 * freed. Setters and getters are complete and null-safe per the Symmetric
 * Getter/Setter Completeness Law; both string projections are bounded and
 * truncation-flagged per the toString Law. A caller must read `value` only when
 * `code == TRY_OK`.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: TryValue (exception/try_value.c)
 * ============================================================================
 * the scalar value-or-error return, paired with TryPtr for the pointer form.
 *
 * STRUCT FIELDS (Mirroring exception/try_value.h):
 * ----------------------------------------------------------------------------
 *   TryValue {
 *     u64 value; // success payload; undefined unless code == TRY_OK
 *     TryCode  code;  // TRY_OK or the named reason
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - TryValue_2(value, code)
 *   - TryValue_ok(value)
 *   - TryValue_error(code)
 *
 * Setters:
 *   - TryValue_setValue(self, value)
 *   - TryValue_setCode(self, code)
 *
 * Getters:
 *   - TryValue_getValue(self)
 *   - TryValue_getCode(self)
 *   - TryValue_isOk(self)
 *
 * String projections:
 *   - TryValue_toString(self, dest, cap, outTruncated)
 *   - TryValue_toStringStruct(self, dest, cap, outTruncated)
 * ============================================================================
 */

// Constructs a result containing the scalar payload and status code.
TryValue TryValue_2(u64 value, TryCode code) {
    TryValue self;

    self.value = value;

    self.code = code;

    return self;
}

// Constructs a successful result carrying value.
TryValue TryValue_ok(u64 value) {
    return TryValue_2(value, TRY_OK);
}

// Constructs an error result carrying zero and code.
TryValue TryValue_error(TryCode code) {
    return TryValue_2(0u, code);
}

;;GETTER
static inline u64 TryValue_value(const TryValue *self) { return (*self).value; }
;;GETTER
static inline TryCode TryValue_code(const TryValue *self) { return (*self).code; }

// Returns the stored scalar, or zero when self is nullptr.
u64 TryValue_getValue(const TryValue *self) {
    if (!self) 
        return 0u;

    return TryValue_value(self);
}

// Replaces the scalar payload when self is non-null.
fn TryValue_setValue(TryValue *self, u64 value) {
    if (!self) 
        return;

    (*self).value = value;
}

// Returns the stored code, or TRY_NULL_ARG when self is nullptr.
TryCode TryValue_getCode(const TryValue *self) {
    if (!self) 
        return TRY_NULL_ARG;

    return TryValue_code(self);
}

// Replaces the result code when self is non-null.
fn TryValue_setCode(TryValue *self, TryCode code) {
    if (!self) 
        return;

    (*self).code = code;
}

// Reports whether a non-null result carries TRY_OK.
bool TryValue_isOk(const TryValue *self) {
    if (!self) 
        return false;

    return TryValue_code(self) == TRY_OK;
}

// Formats a value-oriented summary into dest and reports capacity truncation.
fn TryValue_toString(const TryValue *self, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated) 
        *outTruncated = false;

    if (!dest || cap == 0u) 
        return;

    if (!self) {
        snprintf(dest, cap, "nullptr");

        return;
    }

    i32 written;
    TryCode code = TryValue_code(self);
    if (code == TRY_OK) {
        written = snprintf(dest, cap, "TryValue(value=%llu, code=TRY_OK)",
                           (u64) TryValue_value(self));
    } else {
        written = snprintf(dest, cap, "TryValue(code=%s: %s)",
                           TryCode_name(code),
                           TryCode_message(code));
    }

    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated) 
            *outTruncated = true;
    }
}

// Formats the result fields into dest and reports capacity truncation.
fn TryValue_toStringStruct(const TryValue *self, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated) 
        *outTruncated = false;

    if (!dest || cap == 0u) 
        return;

    if (!self) {
        snprintf(dest, cap, "nullptr");

        return;
    }

    i32 written = snprintf(dest, cap, "TryValue { value=%llu, code=%s }",
                           (u64) TryValue_value(self), TryCode_name(TryValue_code(self)));
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated) 
            *outTruncated = true;
    }
}
