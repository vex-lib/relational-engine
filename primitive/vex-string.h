#ifndef PRIMITIVE_VEX_STRING_H
#define PRIMITIVE_VEX_STRING_H

#include "vex.h"

// primitive/vex-string.h — the string class, ported from primitive/string.java.
//
// A string is a memory block of type TYPE_STRING_ARRAY whose length is the
// UTF-8 byte count + 1 (the trailing NUL lives inside the block). Content-based
// hashing and comparison in Map/Set walk the block header, so strings compare
// by value no matter where they live.
//
// DRAFT: legacy's small/medium/large free-list pools (ABA-tagged atomic heads)
// are deferred; every block currently comes from the memory arena (the Java
// "oversized" path). The API is identical, so the pool can slot in behind it.

#define STRING_SMALL_SLOT_SIZE 64L
#define STRING_MEDIUM_SLOT_SIZE 256L
#define STRING_LARGE_SLOT_SIZE 1024L

// Allocate a NUL-terminated string block copied from value (Java allocate).
u8 *string_allocate(const char *value);

// Allocate a string block copied from len Bytes (Java allocate(byte[])).
u8 *string_allocateBytes(const u8 *Bytes, bytes len);

// Allocate a len-byte string block without copying (Java allocateUninitialized).
// The block is zeroed and NUL-terminated.
u8 *string_allocateUninitialized(bytes len);

// Recycle a string block back to the allocator (Java free).
fn string_free(u8 *ptr);

// Read-only C view of the Bytes (Java get).
const char *string_get(const u8 *ptr);

// Byte length excluding the NUL terminator (Java length).
bytes string_length(const u8 *ptr);

// Block header type id (Java type).
u64 string_type(const u8 *ptr);

// True if the block type is an array form (Java isArray).
bool string_isArray(const u8 *ptr);

// Class id (ID_STRING) of any string block (Java classId).
u32 string_classId();

// Allocated capacity in Bytes, NUL included (Java capacity).
bytes string_capacity(const u8 *ptr);

// Fresh copy of the block (Java copy).
u8 *string_copy(const u8 *ptr);

// True when the string block's Bytes equal a NUL-terminated C string.
bool string_equalsCString(const u8 *ptr, const char *value);

// — Capital String class — vex reserved word String() — file stays string.h lowercase per prefs
// String("hello"), String(u8*), String() → ""
#define String_0(...) string_allocate("")
#define String_1(value) _Generic((value), const char*: string_allocate, char*: string_allocate, u8*: string_copy, const u8*: string_copy, default: string_allocate)(value)
#define String(...) CONSTRUCTOR_DISPATCH(String, __VA_ARGS__)

// Capital aliases for code-editor ergonomics (lowercase kept for compat)
#define String_allocate string_allocate
#define String_allocateBytes string_allocateBytes
#define String_allocateUninitialized string_allocateUninitialized
#define String_free string_free
#define String_get string_get
#define String_length string_length
#define String_type string_type
#define String_isArray string_isArray
#define String_classId string_classId
#define String_capacity string_capacity
#define String_copy string_copy
#define String_equals string_equalsCString

// Compare — strcmp style, negatives/zero/positives
i32 String_compare(const u8 *a, const u8 *b);
#define String_compare String_compare

// Search — contains / indexOf / instring(index, haystack, needle)
bool String_contains(const u8 *haystack, const u8 *needle);
bool String_containsLiteral(const u8 *haystack, const char *needle);
bytes String_indexOf(const u8 *haystack, const u8 *needle);
bytes String_indexOfLiteral(const u8 *haystack, const char *needle);
bool String_instring(bytes index, const u8 *haystack, const u8 *needle);
bool String_instringLiteral(bytes index, const u8 *haystack, const char *needle);

// Substring — new block, caller frees
u8 *String_substring(const u8 *str, bytes start, bytes len);
u8 *String_substringLiteral(const char *str, bytes start, bytes len);
u8 *String_subFirstChar(const u8 *str);
u8 *String_subLastChar(const u8 *str);
u8 *String_subFirst(const u8 *str, bytes count);
u8 *String_subLast(const u8 *str, bytes count);

// Append — new block or in-place first
u8 *String_append(const u8 *a, const u8 *b);
u8 *String_appendLiteral(const u8 *a, const char *b);
u8 *String_appendLiterals(const char *a, const char *b);
fn String_appendInto(const u8 *a, const u8 *b, u8 *dest);
u8 *String_appendFirst(u8 *a, const u8 *b);
u8 *String_appendFirstLiteral(u8 *a, const char *b);

#endif
