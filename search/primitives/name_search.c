#include "vex.h"
#include "search/primitives/name_search.h"
#include "debug/throw.h"
#include <string.h>

;;DEFINITION
// C compares Rust-owned primitive name bytes without owning or mutating a row.
// This cold linear scan is deliberately not a hot per-frame resolver. The caller
// retains the allocation and excludes mutation for the duration of the call.
;;OVERVIEW
// MODULE: name_search; no fields or helpers. Public re_name_search is declared
// by name_search.h; validates span arithmetic, scans first 24 bytes of each row,
// reports invalid admission once, and preserves outputs on absent/rejection.
// A future indexed search can replace this scan without changing row ownership.
// Per the Cold-Only Reflection Law: resolve once, hold the stable binding.
enum { NAME_BYTES = 24, SEARCH_FOUND = 0, SEARCH_MISSING = 1, SEARCH_INVALID = 2 };

/** Scan fixed-width name keys in borrowed rows, preserving outIndex unless a match is found. */
int re_name_search(const u8 *source, bytes spanBytes, bytes stride,
                   const u8 *key, bytes keyBytes, bytes *outIndex) {
    if (key == nullptr || outIndex == nullptr || keyBytes != NAME_BYTES ||
        stride < NAME_BYTES || spanBytes > PTRDIFF_MAX || spanBytes % stride != 0 ||
        (source == nullptr && spanBytes != 0)) {
        THROW("name search rejected invalid borrowed span");
        return SEARCH_INVALID;
    }
    bytes count = spanBytes / stride;
    for (bytes i = 0; i < count; ++i)
        if (memcmp(source + i * stride, key, NAME_BYTES) == 0) {
            *outIndex = i;
            return SEARCH_FOUND;
        }
    return SEARCH_MISSING;
}
