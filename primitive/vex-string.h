#ifndef PRIMITIVE_VEX_STRING_H
#define PRIMITIVE_VEX_STRING_H

/**
 * primitive/vex-string.h -- arena-backed constructors and immutable operations
 * for `string` (`typedef string string;` from `lib/lang/primitive.h`).
 *
 * A `string` is either a static C literal (`string s = "hello";`) or an
 * immutable arena-allocated `TYPE_STRING_ARRAY` block (`string s = string("hello");`).
 * Unlike pooled runtimes, cloning always allocates a distinct block in the
 * owning `MemoryArena`. Content-based hashing and equality in `Map` and `Set`
 * inspect the `TYPE_STRING_ARRAY` block header.
 *
 * Introspection (`string_length`, `string_equals`, `string_compare`,
 * `string_contains`, `string_indexOf`, `string_substring`, `string_append`)
 * works uniformly on both literals and arena-allocated `string`s, and
 * `string_free` safely ignores `nullptr` and non-arena literals.
 */

#include "vex.h"

typedef struct MemoryArena MemoryArena;

/**
 * Allocates an empty immutable arena string ("") in Re_arena().
 * @return newly allocated empty immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_0();

/**
 * Clones a NUL-terminated immutable string into a fresh block in Re_arena() (never pooled).
 * @param value source immutable string (literal or arena-allocated)
 * @return newly allocated immutable string, or nullptr if value is nullptr or allocation fails
 */
;;CONSTRUCTOR
string string_1(string value);

/**
 * Allocates a NUL-terminated immutable string in Re_arena() copied from an explicit byte slice.
 * @param data       source byte pointer (may be nullptr only when byteLength == 0)
 * @param byteLength number of bytes to copy
 * @return newly allocated immutable string, or nullptr on invalid input or allocation failure
 */
;;CONSTRUCTOR
string string_2(const_any data, bytes byteLength);

#define string(...) CONSTRUCTOR_DISPATCH(string, __VA_ARGS__)

/**
 * Clones a NUL-terminated immutable string into an explicit MemoryArena (never pooled).
 * @param arena non-null backing MemoryArena
 * @param value source immutable string
 * @return newly allocated immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_in(MemoryArena *arena, string value);

/**
 * Allocates a NUL-terminated immutable string in an explicit MemoryArena from a byte slice.
 * @param arena      non-null backing MemoryArena
 * @param data       source byte pointer (may be nullptr only when byteLength == 0)
 * @param byteLength number of bytes to copy
 * @return newly allocated immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_sliceIn(MemoryArena *arena, const_any data, bytes byteLength);

/**
 * Clones an immutable string into a fresh allocation in Re_arena() (never pooled).
 * @param self source immutable string
 * @return distinct cloned immutable string, or nullptr if self is nullptr or allocation fails
 */
;;CONSTRUCTOR
string string_clone(string self);

/**
 * Clones an immutable string into a fresh allocation in an explicit MemoryArena.
 * @param arena non-null backing MemoryArena
 * @param self  source immutable string
 * @return distinct cloned immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_cloneIn(MemoryArena *arena, string self);

/**
 * Releases an arena-allocated immutable string; no-op for nullptr or static literals.
 * @param self immutable string to release
 */
;;DECONSTRUCT
fn string_free(string self);

/**
 * Returns the UTF-8 byte length excluding NUL (O(1) on arena blocks, strlen on literals).
 * @param self immutable string
 * @return byte count excluding trailing NUL, or 0 when self is nullptr
 */
bytes string_length(string self);

/**
 * Checks whether a string is nullptr or contains zero bytes.
 * @param self immutable string
 * @return true when self is nullptr or its byte length is 0
 */
bool string_isEmpty(string self);

/**
 * Returns total byte capacity including trailing NUL.
 * @param self immutable string
 * @return allocated block length for arena blocks, length + 1 for literals, or 0 for nullptr
 */
bytes string_capacity(string self);

/**
 * Returns the MemoryHeader type id for an arena-allocated string, or 0 for literals/nullptr.
 * @param self immutable string
 * @return 64-bit type id or 0
 */
u64 string_type(string self);

/**
 * Checks whether a string points to a live arena-owned allocation (as opposed to a static literal).
 * @param self immutable string
 * @return true when backed by a live MemoryHeader
 */
bool string_isOwned(string self);

/**
 * Checks whether an arena string carries the FORM_ARRAY type flag.
 * @param self immutable string
 * @return true when string_type(self) carries FORM_ARRAY
 */
bool string_isArray(string self);

/**
 * Returns the class id (ID_STRING) for arena strings.
 * @return ID_STRING constant
 */
u32 string_classId();

/**
 * Exact byte equality between two immutable strings (literals or arena-allocated).
 * @param a first immutable string
 * @param b second immutable string
 * @return true when both are non-null and have identical bytes
 */
bool string_equals(string a, string b);

/**
 * Lexicographic comparison between two immutable strings (literals or arena-allocated).
 * @param a first immutable string
 * @param b second immutable string
 * @return negative if a < b, 0 if equal, positive if a > b
 */
i32 string_compare(string a, string b);

/**
 * Checks whether self starts with prefix.
 * @param self   immutable string to inspect
 * @param prefix prefix string to match
 * @return true when both are non-null and self begins with prefix
 */
bool string_startsWith_2(string self, string prefix);

/**
 * Checks whether self starts with the first length bytes of prefix.
 * @param self   immutable string to inspect
 * @param prefix prefix string to match
 * @param length byte count of prefix to compare
 * @return true when self is non-null and begins with prefix[0..length)
 */
bool string_startsWith_3(string self, string prefix, bytes length);

#define string_startsWith(...) CONSTRUCTOR_DISPATCH(string_startsWith, __VA_ARGS__)

/**
 * Checks whether self ends with suffix.
 * @param self   immutable string to inspect
 * @param suffix suffix string to match
 * @return true when both are non-null and self ends with suffix
 */
bool string_endsWith(string self, string suffix);

/**
 * Searches for the first occurrence of a single utf8 byte inside self.
 * @param self   immutable string to search
 * @param needle utf8 byte value to locate
 * @return zero-based byte offset of the first match, or SIZE_MAX if absent
 */
bytes string_findChar(string self, utf8 needle);

/**
 * Checks whether haystack contains needle (literals or arena-allocated).
 * @param haystack string to search
 * @param needle   substring to locate
 * @return true when both are non-null and needle occurs within haystack
 */
bool string_contains(string haystack, string needle);

/**
 * Returns a pointer to the first occurrence of needle in haystack, or nullptr if absent.
 * @param haystack string to search
 * @param needle   substring to locate
 * @return pointer within haystack to the first match, or nullptr when absent
 */
string string_find(string haystack, string needle);

/**
 * Returns the byte offset of the first occurrence of needle in haystack, or SIZE_MAX if absent.
 * @param haystack string to search
 * @param needle   substring to locate
 * @return zero-based byte index, or SIZE_MAX when absent or either input is nullptr
 */
bytes string_indexOf(string haystack, string needle);

/**
 * Checks whether needle first occurs in haystack at exact byte offset index.
 * @param index    expected zero-based byte offset
 * @param haystack string to search
 * @param needle   substring to locate
 * @return true when string_indexOf(haystack, needle) == index
 */
bool string_instring(bytes index, string haystack, string needle);

/**
 * Allocates an immutable substring in Re_arena() spanning [start, start + len) clamped to length.
 * @param self  source immutable string
 * @param start starting byte offset
 * @param len   requested byte length
 * @return newly allocated immutable substring, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_substring(string self, bytes start, bytes len);

/**
 * Allocates an immutable string containing the first byte of self.
 * @param self source immutable string
 * @return newly allocated single-byte (or empty) immutable string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subFirstChar(string self);

/**
 * Allocates an immutable string containing the last byte of self.
 * @param self source immutable string
 * @return newly allocated single-byte (or empty) immutable string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subLastChar(string self);

/**
 * Allocates an immutable string containing the first count bytes of self.
 * @param self  source immutable string
 * @param count byte count from the start
 * @return newly allocated immutable prefix string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subFirst(string self, bytes count);

/**
 * Allocates an immutable string containing the last count bytes of self.
 * @param self  source immutable string
 * @param count byte count from the end
 * @return newly allocated immutable suffix string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subLast(string self, bytes count);

/**
 * Concatenates two immutable strings into a freshly allocated immutable string in Re_arena().
 * @param a left immutable string (may be nullptr)
 * @param b right immutable string (may be nullptr)
 * @return newly allocated concatenated immutable string
 */
;;CONSTRUCTOR
string string_append(string a, string b);

/**
 * Concatenates two immutable strings into a freshly allocated immutable string in an explicit arena.
 * @param arena non-null backing MemoryArena
 * @param a     left immutable string (may be nullptr)
 * @param b     right immutable string (may be nullptr)
 * @return newly allocated concatenated immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_appendIn(MemoryArena *arena, string a, string b);

#endif
