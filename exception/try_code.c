#include "vex.h"
#include "exception/try_code.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: TryCode
 * ============================================================================
 * The TryCode taxonomy: the named reason a value-or-error return carries. It is
 * the shared vocabulary of the Try family (TryValue for scalars, TryPtr for
 * pointers). Every code maps to a static symbolic name and a static sentence, so
 * a Try reports a reason without allocating and without a lookup table that can
 * drift (the Cold-Strict, Hot-Minimal Validation Law). TRY_OK is the one success
 * code; TryCode_isOk is the single success test used across the ecosystem.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: TryCode (exception/try_code.c)
 * ============================================================================
 * The named reasons a Try carries; no struct, an enum plus its two projections.
 *
 * ENUMERATORS (Mirroring exception/try_code.h):
 * ----------------------------------------------------------------------------
 *   TryCode {
 *     TRY_OK, TRY_NULL_ARG, TRY_BOUNDS, TRY_OVERFLOW, TRY_EMPTY,
 *     TRY_NOT_FOUND, TRY_IO, TRY_FORMAT, TRY_UNSUPPORTED, TRY_NO_MEMORY
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - TryCode_isOk(code)
 *   - TryCode_name(code)
 *   - TryCode_message(code)
 * ============================================================================
 */

// Reports whether code denotes successful completion.
bool TryCode_isOk(TryCode code) {
    return code == TRY_OK;
}

// Returns code's stable symbolic name, including an unknown fallback.
const char *TryCode_name(TryCode code) {
    switch (code) {
        case TRY_OK:          return "TRY_OK";
        case TRY_NULL_ARG:    return "TRY_NULL_ARG";
        case TRY_BOUNDS:      return "TRY_BOUNDS";
        case TRY_OVERFLOW:    return "TRY_OVERFLOW";
        case TRY_EMPTY:       return "TRY_EMPTY";
        case TRY_NOT_FOUND:   return "TRY_NOT_FOUND";
        case TRY_IO:          return "TRY_IO";
        case TRY_FORMAT:      return "TRY_FORMAT";
        case TRY_UNSUPPORTED: return "TRY_UNSUPPORTED";
        case TRY_NO_MEMORY:   return "TRY_NO_MEMORY";
        default:              return "TRY_UNKNOWN";
    }
}

// Returns the stable explanatory message associated with code.
const char *TryCode_message(TryCode code) {
    switch (code) {
        case TRY_OK:          return "ok";
        case TRY_NULL_ARG:    return "required pointer argument was nullptr";
        case TRY_BOUNDS:      return "index outside the active range";
        case TRY_OVERFLOW:    return "size arithmetic would wrap";
        case TRY_EMPTY:       return "source held no element";
        case TRY_NOT_FOUND:   return "named thing is absent";
        case TRY_IO:          return "file or stream operation failed";
        case TRY_FORMAT:      return "Bytes present but malformed";
        case TRY_UNSUPPORTED: return "operation not supported here";
        case TRY_NO_MEMORY:   return "owner backing storage could not satisfy the request";
        default:              return "unknown TryCode";
    }
}
