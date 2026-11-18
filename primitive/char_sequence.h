#ifndef PRIMITIVE_CHAR_SEQUENCE_H
#define PRIMITIVE_CHAR_SEQUENCE_H

#include "vex.h"

// primitive/char_sequence.h — a non-owning view of text: bytes plus a length.
//
// The view is the safe way to look at text that is NOT NUL-terminated: a JSON
// string, a slice of a mapped file, a token inside a larger buffer. It
// allocates nothing, copies nothing, and owns nothing — it is valid exactly as
// long as the bytes it points at.
//
// This class owns the libc comparison and search calls. Callers compare views;
// the raw calls live in one file, the way malloc lives only in re/nio/mem.c.
//
//   CharSequence a = CharSequence_cstr("alpha");        // no allocation
//   CharSequence b = CharSequence_2(data, length);      // caller's bytes
//   if (CharSequence_equals(a, b)) ...
//
// Distinct from String (primitive/vex-string.h), which is an owned arena block
// with a header and a NUL. Use String when the text must outlive the call; use
// CharSequence when it only has to be read.

typedef struct CharSequence {
    const char *data;
    bytes length;
} CharSequence;

CharSequence CharSequence_2(const char *data, bytes length);
#define CharSequence(...) CONSTRUCTOR_DISPATCH(CharSequence, __VA_ARGS__)
#define CharSequence_zero() CharSequence_2(nullptr, 0)

// A NUL-terminated C string. Null is the empty view. This is the one place the
// class measures a C string.
CharSequence CharSequence_cstr(const char *text);
// An owned String block (primitive/vex-string.h). Null is the empty view.
CharSequence CharSequence_block(const u8 *block);

bytes CharSequence_length(CharSequence self);
bool CharSequence_isEmpty(CharSequence self);
// Lexicographic: negative, zero, positive. Length breaks a shared prefix.
i32 CharSequence_compare(CharSequence left, CharSequence right);
bool CharSequence_equals(CharSequence left, CharSequence right);
bool CharSequence_startsWith(CharSequence self, CharSequence prefix);
bool CharSequence_endsWith(CharSequence self, CharSequence suffix);
// Byte offset of the first occurrence, or SIZE_MAX when absent.
bytes CharSequence_find(CharSequence self, CharSequence needle);
bytes CharSequence_findChar(CharSequence self, char needle);
// Convenience for a literal prefix, without spelling a second view.
bool CharSequence_hasPrefix(CharSequence self, const char *literal);

#endif
