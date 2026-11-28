#ifndef STRUCT_LAYOUT_H
#define STRUCT_LAYOUT_H

#include "vex.h"

/**
 * struct/layout.h, compile-time-captured runtime element layout metadata
 * shared by every polymorphic container (List, Array, Map, Set, Queue, Deque,
 * Stack, Graph, Array2D, Array3D).
 *
 * ElementLayout_of(T) captures sizeof(T), alignof(T), #T, and automatically
 * classifies NUL-terminated string keys (string / utf8*) so hash tables compare
 * and hash string keys by UTF-8 content rather than pointer identity.
 */

/**
 * Hashing and equality strategy selected at compile time by ElementLayout_of(T).
 *
 * ELEMENT_KEY_BYTES  - hash and compare raw stride bytes via FNV-1a/Murmur3 + memcmp
 * ELEMENT_KEY_STRING - dereference stored string pointer and hash/compare UTF-8 content
 */
;;CLASSNAME("ElementKeyMode")
typedef enum ElementKeyMode {
    ELEMENT_KEY_BYTES = 0,
    ELEMENT_KEY_STRING = 1,
} ElementKeyMode;

/**
 * Compile-time-captured runtime layout descriptor for one element type T.
 *
 * bytes stride           - byte width of one element (sizeof(T))
 * bytes alignment        - required alignment in bytes (alignof(T), power of two <= 32)
 * u64 typeHash           - FNV-1a/Murmur3 fingerprint over typeName, stride, and alignment
 * string typeName        - human-readable C type spelling (#T)
 * ElementKeyMode keyMode - hashing/equality mode for Map/Set keys
 */
;;CLASSNAME("ElementLayout")
typedef struct ElementLayout {
    bytes stride;
    bytes alignment;
    u64 typeHash;
    string typeName;
    ElementKeyMode keyMode;
} ElementLayout;

/**
 * Constructs an ElementLayout with explicit key hashing mode.
 * @param stride    byte width of one element (must be > 0)
 * @param alignment required alignment in bytes (0 derives natural power-of-two <= 32)
 * @param typeName  human-readable type name (nullptr defaults to "unknown")
 * @param keyMode   hashing/equality mode (ELEMENT_KEY_BYTES or ELEMENT_KEY_STRING)
 * @return initialized ElementLayout value
 */
ElementLayout ElementLayout_makeMode(bytes stride, bytes alignment, string typeName, ElementKeyMode keyMode);

/**
 * Constructs an ElementLayout using raw byte hashing/equality.
 * @param stride    byte width of one element
 * @param alignment required alignment in bytes (0 derives natural power-of-two <= 32)
 * @param typeName  human-readable type name
 * @return initialized ElementLayout value
 */
ElementLayout ElementLayout_make(bytes stride, bytes alignment, string typeName);

/**
 * Captures the ElementLayout of any C type T at compile time.
 */
#define ElementLayout_of(T)                                                     \
    ElementLayout_makeMode(sizeof(T), alignof(T), #T,                           \
        _Generic((typeof_unqual(T)*) 0,                                         \
            string *: ELEMENT_KEY_STRING,                                       \
            utf8 **: ELEMENT_KEY_STRING,                                        \
            default: ELEMENT_KEY_BYTES))

bool ElementLayout_isValid(ElementLayout layout);
bool ElementLayout_equals(ElementLayout a, ElementLayout b);
u64 ElementLayout_hash(ElementLayout layout, const_any data);
bool ElementLayout_bytesEqual(ElementLayout layout, const_any a, const_any b);
bytes ElementLayout_alignUp(bytes value, bytes alignment);

bytes ElementLayout_stride(ElementLayout layout);
bytes ElementLayout_alignment(ElementLayout layout);
u64 ElementLayout_typeHash(ElementLayout layout);
string ElementLayout_typeName(ElementLayout layout);
ElementKeyMode ElementLayout_keyMode(ElementLayout layout);

#endif
