#ifndef SEARCH_PRIMITIVES_NAME_SEARCH_H
#define SEARCH_PRIMITIVES_NAME_SEARCH_H
#include <stddef.h>
#include <stdint.h>

// Fixed 24-byte name comparison over borrowed strided rows, on a cold path.
// 0 = found (writes outIndex), 1 = absent, 2 = invalid (one THROW).
// Missing/invalid preserve outIndex. Zero rows permit a null source.
// Non-null spans must be readable/live and outIndex writable, properly aligned,
// disjoint from input, and externally synchronized. No arbitrary-address check.
// Names are raw 24-byte keys: folding/grammar validation belong to VariableSlot.
// stride >= 24; spanBytes is a multiple of stride, <= PTRDIFF_MAX.
// No allocation, owned storage, pointer retention, or Rust atomic access.
int re_name_search(const uint8_t *source, size_t spanBytes, size_t stride,
                   const uint8_t *key, size_t keyBytes, size_t *outIndex);
#endif
