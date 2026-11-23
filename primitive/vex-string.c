#include "vex.h"
#include "re.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: String
 * ============================================================================
 * Arena-backed constructors, cloning, introspection, bounded search, slicing,
 * and immutable concatenation over the floor's `string` (`string `) type.
 * Arena allocations carry `TYPE_STRING_ARRAY` (`ID_STRING`) so `Map` and `Set`
 * hash and compare owned strings by content. Static `""` literals and arena
 * allocations share the exact same `string` type; `Memory_safeHeader` lets
 * `string_length` stay O(1) on arena blocks while `string_free` safely no-ops
 * on static literals. Never pools or interns strings.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: String (re/primitive/vex-string.c)
 * ============================================================================
 * Relational Engine constructors and immutable UTF-8 operations over `string`.
 *
 * --CONSTRUCTORS-------------------------
 * public   string_0()                     empty arena string ("") in Re_arena()
 * public   string_1(...)                  arena clone of NUL-terminated string in Re_arena()
 * public   string_2(...)                  arena slice clone in Re_arena()
 * public   string_in(...)                 arena clone of NUL-terminated string in explicit arena
 * public   string_sliceIn(...)            arena slice clone in explicit arena
 * public   string_clone(...)              explicit clone in Re_arena()
 * public   string_cloneIn(...)            explicit clone in explicit arena
 * public   string_substring(...)          bounded substring allocation in Re_arena()
 * public   string_subFirstChar(...)       first-byte substring allocation in Re_arena()
 * public   string_subLastChar(...)        last-byte substring allocation in Re_arena()
 * public   string_subFirst(...)           prefix substring allocation in Re_arena()
 * public   string_subLast(...)            suffix substring allocation in Re_arena()
 * public   string_append(...)             immutable concatenation in Re_arena()
 * public   string_appendIn(...)           immutable concatenation in explicit arena
 *
 * --FUNCTIONS----------------------------
 * public   string_free(...)               releases arena string; safe no-op on literals/nullptr
 * public   string_length(...)             O(1) arena length or strlen on literals
 * public   string_isEmpty(...)            true when nullptr or length is 0
 * public   string_capacity(...)           arena capacity or length + 1 on literals
 * public   string_type(...)               block header type id or 0 on literals
 * public   string_isOwned(...)            true when backed by a live arena header
 * public   string_isArray(...)            true when block type carries FORM_ARRAY
 * public   string_classId()               returns ID_STRING
 * public   string_equals(...)             bounded byte equality
 * public   string_compare(...)            lexicographic comparison
 * public   string_startsWith(...)         bounded prefix match
 * public   string_endsWith(...)           bounded suffix match
 * public   string_findChar(...)           bounded single-byte search
 * public   string_contains(...)           bounded substring test
 * public   string_find(...)               bounded substring pointer search
 * public   string_indexOf(...)            bounded substring search
 * public   string_instring(...)           exact-offset substring match check
 * ============================================================================
 */

/**
 * Allocates an empty immutable arena string ("") in Re_arena().
 * @return newly allocated empty immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_0()
{
    return string_sliceIn(Re_arena(), "", 0u);
}

/**
 * Clones a NUL-terminated immutable string into a fresh block in Re_arena() (never pooled).
 * @param value source immutable string (literal or arena-allocated)
 * @return newly allocated immutable string, or nullptr if value is nullptr or allocation fails
 */
;;CONSTRUCTOR
string string_1(string value)
{
    return string_in(Re_arena(), value);
}

/**
 * Allocates a NUL-terminated immutable string in Re_arena() copied from an explicit byte slice.
 * @param data       source byte pointer (may be nullptr only when byteLength == 0)
 * @param byteLength number of bytes to copy
 * @return newly allocated immutable string, or nullptr on invalid input or allocation failure
 */
;;CONSTRUCTOR
string string_2(const_any data, bytes byteLength)
{
    return string_sliceIn(Re_arena(), data, byteLength);
}

/**
 * Clones a NUL-terminated immutable string into an explicit MemoryArena (never pooled).
 * @param arena non-null backing MemoryArena
 * @param value source immutable string
 * @return newly allocated immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_in(MemoryArena *arena, string value)
{
    if (!value)
        return nullptr;

    return string_sliceIn(arena, value, string_length(value));
}

/**
 * Allocates a NUL-terminated immutable string in an explicit MemoryArena from a byte slice.
 * @param arena      non-null backing MemoryArena
 * @param data       source byte pointer (may be nullptr only when byteLength == 0)
 * @param byteLength number of bytes to copy
 * @return newly allocated immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_sliceIn(MemoryArena *arena, const_any data, bytes byteLength)
{
    if (!arena) {
        THROW("string_sliceIn: arena is nullptr");
        return nullptr;
    }

    if (!data && byteLength > 0u)
        return nullptr;

    if (byteLength == ((bytes) -1)) {
        THROW("string_sliceIn: byteLength overflow");
        return nullptr;
    }

    utf8 *ptr = (utf8*) Memory_alloc(TYPE_STRING_ARRAY, arena, byteLength + 1u);

    if (!ptr)
        return nullptr;

    if (byteLength > 0u)
        memcpy(ptr, data, byteLength);

    ptr[byteLength] = '\0';
    return (string) ptr;
}

/**
 * Clones an immutable string into a fresh allocation in Re_arena() (never pooled).
 * @param self source immutable string
 * @return distinct cloned immutable string, or nullptr if self is nullptr or allocation fails
 */
;;CONSTRUCTOR
string string_clone(string self)
{
    return string_in(Re_arena(), self);
}

/**
 * Clones an immutable string into a fresh allocation in an explicit MemoryArena.
 * @param arena non-null backing MemoryArena
 * @param self  source immutable string
 * @return distinct cloned immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_cloneIn(MemoryArena *arena, string self)
{
    return string_in(arena, self);
}

/**
 * Releases an arena-allocated immutable string; no-op for nullptr or static literals.
 * @param self immutable string to release
 */
;;DECONSTRUCT
fn string_free(string self)
{
    if (!self)
        return;

    if (!Memory_safeHeader(self))
        return;

    Memory_free((any) (pointer) self);
}

/**
 * Returns the UTF-8 byte length excluding NUL (O(1) on arena blocks, strlen on literals).
 * @param self immutable string
 * @return byte count excluding trailing NUL, or 0 when self is nullptr
 */
bytes string_length(string self)
{
    if (!self)
        return 0u;

    if (Memory_safeHeader(self) && Memory_type((any) (pointer) self) == TYPE_STRING_ARRAY) {
        bytes cap = Memory_length((any) (pointer) self);

        if (cap > 0u)
            return strnlen(self, cap - 1u);

        return 0u;
    }

    return strlen(self);
}

/**
 * Checks whether a string is nullptr or contains zero bytes.
 * @param self immutable string
 * @return true when self is nullptr or its byte length is 0
 */
bool string_isEmpty(string self)
{
    return string_length(self) == 0u;
}

/**
 * Returns total byte capacity including trailing NUL.
 * @param self immutable string
 * @return allocated block length for arena blocks, length + 1 for literals, or 0 for nullptr
 */
bytes string_capacity(string self)
{
    if (!self)
        return 0u;

    if (Memory_safeHeader(self))
        return Memory_length((any) (pointer) self);

    return strlen(self) + 1u;
}

/**
 * Returns the MemoryHeader type id for an arena-allocated string, or 0 for literals/nullptr.
 * @param self immutable string
 * @return 64-bit type id or 0
 */
u64 string_type(string self)
{
    if (!self)
        return 0u;

    if (!Memory_safeHeader(self))
        return 0u;

    return Memory_type((any) (pointer) self);
}

/**
 * Checks whether a string points to a live arena-owned allocation (as opposed to a static literal).
 * @param self immutable string
 * @return true when backed by a live MemoryHeader
 */
bool string_isOwned(string self)
{
    if (!self)
        return false;

    return Memory_safeHeader(self) != nullptr;
}

/**
 * Checks whether an arena string carries the FORM_ARRAY type flag.
 * @param self immutable string
 * @return true when string_type(self) carries FORM_ARRAY
 */
bool string_isArray(string self)
{
    u64 t = string_type(self);

    if (t == 0u)
        return false;

    return (t & FORM_ARRAY) == FORM_ARRAY;
}

/**
 * Returns the class id (ID_STRING) for arena strings.
 * @return ID_STRING constant
 */
u32 string_classId()
{
    return ID_STRING;
}

/**
 * Exact byte equality between two immutable strings (literals or arena-allocated).
 * @param a first immutable string
 * @param b second immutable string
 * @return true when both are non-null and have identical bytes
 */
bool string_equals(string a, string b)
{
    if (!a || !b)
        return false;

    bytes lLen = string_length(a);
    bytes rLen = string_length(b);

    if (lLen != rLen)
        return false;

    if (lLen == 0u)
        return true;

    return memcmp(a, b, lLen) == 0;
}

/**
 * Lexicographic comparison between two immutable strings (literals or arena-allocated).
 * @param a first immutable string
 * @param b second immutable string
 * @return negative if a < b, 0 if equal, positive if a > b
 */
i32 string_compare(string a, string b)
{
    if (!a && !b)
        return 0;

    if (!a)
        return -1;

    if (!b)
        return 1;

    bytes lLen = string_length(a);
    bytes rLen = string_length(b);
    bytes common = lLen < rLen ? lLen : rLen;

    if (common > 0u)
    {
        i32 order = memcmp(a, b, common);

        if (order != 0)
            return order;
    }

    if (lLen == rLen)
        return 0;

    return lLen < rLen ? -1 : 1;
}

/**
 * Checks whether self starts with the first length bytes of prefix.
 * @param self   immutable string to inspect
 * @param prefix prefix string to match
 * @param length byte count of prefix to compare
 * @return true when self is non-null and begins with prefix[0..length)
 */
bool string_startsWith_3(string self, string prefix, bytes length)
{
    if (!self || (!prefix && length > 0u))
        return false;

    bytes sLen = string_length(self);

    if (length > sLen)
        return false;

    if (length == 0u)
        return true;

    return memcmp(self, prefix, length) == 0;
}

/**
 * Checks whether self starts with prefix.
 * @param self   immutable string to inspect
 * @param prefix prefix string to match
 * @return true when both are non-null and self begins with prefix
 */
bool string_startsWith_2(string self, string prefix)
{
    if (!self || !prefix)
        return false;

    return string_startsWith_3(self, prefix, string_length(prefix));
}

/**
 * Checks whether self ends with suffix.
 * @param self   immutable string to inspect
 * @param suffix suffix string to match
 * @return true when both are non-null and self ends with suffix
 */
bool string_endsWith(string self, string suffix)
{
    if (!self || !suffix)
        return false;

    bytes sLen = string_length(self);
    bytes xLen = string_length(suffix);

    if (xLen > sLen)
        return false;

    if (xLen == 0u)
        return true;

    return memcmp(self + (sLen - xLen), suffix, xLen) == 0;
}

/**
 * Searches for the first occurrence of a single utf8 byte inside self.
 * @param self   immutable string to search
 * @param needle utf8 byte value to locate
 * @return zero-based byte offset of the first match, or SIZE_MAX if absent
 */
bytes string_findChar(string self, utf8 needle)
{
    if (!self)
        return SIZE_MAX;

    bytes sLen = string_length(self);

    if (sLen == 0u)
        return SIZE_MAX;

    string hit = (string) memchr(self, needle, sLen);

    if (!hit)
        return SIZE_MAX;

    return (bytes) (hit - self);
}

/**
 * Checks whether haystack contains needle (literals or arena-allocated).
 * @param haystack string to search
 * @param needle   substring to locate
 * @return true when both are non-null and needle occurs within haystack
 */
bool string_contains(string haystack, string needle)
{
    return string_indexOf(haystack, needle) != SIZE_MAX;
}

/**
 * Returns a pointer to the first occurrence of needle in haystack, or nullptr if absent.
 * @param haystack string to search
 * @param needle   substring to locate
 * @return pointer within haystack to the first match, or nullptr when absent
 */
string string_find(string haystack, string needle)
{
    bytes idx = string_indexOf(haystack, needle);

    if (idx == SIZE_MAX)
        return nullptr;

    return haystack + idx;
}

/**
 * Returns the byte offset of the first occurrence of needle in haystack, or SIZE_MAX if absent.
 * @param haystack string to search
 * @param needle   substring to locate
 * @return zero-based byte index, or SIZE_MAX when absent or either input is nullptr
 */
bytes string_indexOf(string haystack, string needle)
{
    if (!haystack || !needle)
        return SIZE_MAX;

    bytes hLen = string_length(haystack);
    bytes nLen = string_length(needle);

    if (nLen == 0u)
        return 0u;

    if (nLen > hLen)
        return SIZE_MAX;

    bytes last = hLen - nLen;

    for (bytes index = 0u; index <= last; ++index)
    {
        if (haystack[index] != needle[0])
            continue;

        if (memcmp(haystack + index, needle, nLen) == 0)
            return index;
    }

    return SIZE_MAX;
}

/**
 * Checks whether needle first occurs in haystack at exact byte offset index.
 * @param index    expected zero-based byte offset
 * @param haystack string to search
 * @param needle   substring to locate
 * @return true when string_indexOf(haystack, needle) == index
 */
bool string_instring(bytes index, string haystack, string needle)
{
    if (!haystack || !needle)
        return false;

    bytes pos = string_indexOf(haystack, needle);

    if (pos == SIZE_MAX)
        return false;

    return pos == index;
}

/**
 * Allocates an immutable substring in Re_arena() spanning [start, start + len) clamped to length.
 * @param self  source immutable string
 * @param start starting byte offset
 * @param len   requested byte length
 * @return newly allocated immutable substring, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_substring(string self, bytes start, bytes len)
{
    if (!self)
        return nullptr;

    bytes total = string_length(self);

    if (start >= total)
        return string_0();

    bytes take = len;

    if (take > total - start)
        take = total - start;

    return string_sliceIn(Re_arena(), self + start, take);
}

/**
 * Allocates an immutable string containing the first byte of self.
 * @param self source immutable string
 * @return newly allocated single-byte (or empty) immutable string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subFirstChar(string self)
{
    return string_substring(self, 0u, 1u);
}

/**
 * Allocates an immutable string containing the last byte of self.
 * @param self source immutable string
 * @return newly allocated single-byte (or empty) immutable string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subLastChar(string self)
{
    if (!self)
        return nullptr;

    bytes total = string_length(self);

    if (total == 0u)
        return string_0();

    return string_substring(self, total - 1u, 1u);
}

/**
 * Allocates an immutable string containing the first count bytes of self.
 * @param self  source immutable string
 * @param count byte count from the start
 * @return newly allocated immutable prefix string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subFirst(string self, bytes count)
{
    return string_substring(self, 0u, count);
}

/**
 * Allocates an immutable string containing the last count bytes of self.
 * @param self  source immutable string
 * @param count byte count from the end
 * @return newly allocated immutable suffix string, or nullptr if self is nullptr
 */
;;CONSTRUCTOR
string string_subLast(string self, bytes count)
{
    if (!self)
        return nullptr;

    bytes total = string_length(self);

    if (count >= total)
        return string_clone(self);

    return string_substring(self, total - count, count);
}

/**
 * Concatenates two immutable strings into a freshly allocated immutable string in Re_arena().
 * @param a left immutable string (may be nullptr)
 * @param b right immutable string (may be nullptr)
 * @return newly allocated concatenated immutable string
 */
;;CONSTRUCTOR
string string_append(string a, string b)
{
    return string_appendIn(Re_arena(), a, b);
}

/**
 * Concatenates two immutable strings into a freshly allocated immutable string in an explicit arena.
 * @param arena non-null backing MemoryArena
 * @param a     left immutable string (may be nullptr)
 * @param b     right immutable string (may be nullptr)
 * @return newly allocated concatenated immutable string, or nullptr on failure
 */
;;CONSTRUCTOR
string string_appendIn(MemoryArena *arena, string a, string b)
{
    if (!arena) {
        THROW("string_appendIn: arena is nullptr");
        return nullptr;
    }

    if (!a && !b)
        return string_sliceIn(arena, "", 0u);

    if (!a)
        return string_in(arena, b);

    if (!b)
        return string_in(arena, a);

    bytes aLen = string_length(a);
    bytes bLen = string_length(b);

    if (aLen > ((bytes) -1) - bLen - 1u) {
        THROW("string_appendIn: concatenated length overflow");
        return nullptr;
    }

    utf8 *out = (utf8*) Memory_alloc(TYPE_STRING_ARRAY, arena, aLen + bLen + 1u);

    if (!out)
        return nullptr;

    if (aLen > 0u)
        memcpy(out, a, aLen);

    if (bLen > 0u)
        memcpy(out + aLen, b, bLen);

    out[aLen + bLen] = '\0';
    return (string) out;
}
