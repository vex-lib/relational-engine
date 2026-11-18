#include "vex.h"
#include "re.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: String
 * ============================================================================
 * The string class, ported from primitive/string.java: NUL-terminated
 * u8 payloads allocated from the memory arena with a trailing byte
 * reserved for the terminator. Provides allocate/free, length/type/capacity
 * introspection, copy/equals, and the String_* family of compare, contains,
 * indexOf, substring, and append operations. Appends are dest-last where a
 * destination buffer is supplied; in-place append requires caller capacity.
 * Lives in re (`primitive/`) as an engine-owned arena string block.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: String (primitive/vex-string.c)
 * ============================================================================
 * the string class, ported from primitive/string.java.
 *
 * STRUCT FIELDS: none — procedural (operates on BitPool/memory blocks of string payloads)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - string_allocate(value)
 *   - string_allocateBytes(Bytes, len)
 *   - string_allocateUninitialized(len)
 *   - string_free(ptr)
 *   - string_length(ptr)
 *   - string_type(ptr)
 *   - string_classId()
 *   - string_capacity(ptr)
 *   - string_copy(ptr)
 *   - string_equalsCString(ptr, value)
 *   - String_compare(a, b)
 *   - String_contains(haystack, needle)
 *   - String_containsLiteral(haystack, needle)
 *   - String_indexOf(haystack, needle)
 *   - String_indexOfLiteral(haystack, needle)
 *   - String_instring(index, haystack, needle)
 *   - String_instringLiteral(index, haystack, needle)
 *   - String_substring(str, start, len)
 *   - String_substringLiteral(str, start, len)
 *   - String_subFirstChar(str)
 *   - String_subLastChar(str)
 *   - String_subFirst(str, count)
 *   - String_subLast(str, count)
 *   - String_append(a, b)
 *   - String_appendLiteral(a, b)
 *   - String_appendLiterals(a, b)
 *   - String_appendInto(a, b, dest)
 *   - String_appendFirst(a, b)
 *   - String_appendFirstLiteral(a, b)
 *
 * Getters:
 *   - string_get(ptr)
 *   - string_isArray(ptr)
 * ============================================================================
 */


// vex-string.c — the string class, ported from primitive/string.java.

static u8 *allocate_raw(bytes len) {
    u8 *ptr = (u8*) Memory_alloc(TYPE_STRING_ARRAY, len + 1);
    if (ptr)
        memset(ptr, 0, len + 1);
    return ptr;
}

u8 *string_allocate(const char *value) {
    if (!value)
        return nullptr;
    return string_allocateBytes((const u8*) value, strlen(value));
}

u8 *string_allocateBytes(const u8 *Bytes, bytes len) {
    if (!Bytes && len > 0)
        return nullptr;
    u8 *ptr = allocate_raw(len);
    if (!ptr)
        return nullptr;
    if (len > 0)
        memcpy(ptr, Bytes, len);
    ptr[len] = '\0';
    return ptr;
}

u8 *string_allocateUninitialized(bytes len) {
    return allocate_raw(len);
}

fn string_free(u8 *ptr) {
    if (ptr)
        Memory_free(ptr);
}

const char *string_get(const u8 *ptr) {
    if (!ptr)
        return nullptr;
    return (const char*) ptr;
}

bytes string_length(const u8 *ptr) {
    if (!ptr)
        return 0;
    bytes n = Memory_length((any) (pointer)ptr);
    return n > 0 ? n - 1 : 0;
}

u64 string_type(const u8 *ptr) {
    if (!ptr)
        return 0;
    return Memory_type((any) (pointer)ptr);
}

bool string_isArray(const u8 *ptr) {
    u64 t = string_type(ptr);
    return t != 0 && (t & FORM_ARRAY) == FORM_ARRAY;
}

u32 string_classId() {
    return ID_STRING;
}

bytes string_capacity(const u8 *ptr) {
    if (!ptr)
        return 0;
    return Memory_length((any) (pointer)ptr);
}

u8 *string_copy(const u8 *ptr) {
    if (!ptr)
        return nullptr;
    bytes len = string_length(ptr);
    u8 *dup = string_allocateBytes(ptr, len);
    return dup;
}

bool string_equalsCString(const u8 *ptr, const char *value) {
    if (!ptr || !value)
        return false;
    bytes len = string_length(ptr);
    if (strlen(value) != len)
        return false;
    return memcmp(ptr, value, len) == 0;
}

i32 String_compare(const u8 *a, const u8 *b) {
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;
    const char *sa = (const char*) a;
    const char *sb = (const char*) b;
    return strcmp(sa, sb);
}

bool String_contains(const u8 *haystack, const u8 *needle) {
    if (!haystack || !needle)
        return false;
    const char *h = (const char*) haystack;
    const char *n = (const char*) needle;
    return strstr(h, n) != nullptr;
}

bool String_containsLiteral(const u8 *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    const char *h = (const char*) haystack;
    return strstr(h, needle) != nullptr;
}

bytes String_indexOf(const u8 *haystack, const u8 *needle) {
    if (!haystack || !needle)
        return (bytes) -1;
    const char *h = (const char*) haystack;
    const char *n = (const char*) needle;
    const char *p = strstr(h, n);
    if (!p)
        return (bytes) -1;
    return (bytes)(p - h);
}

bytes String_indexOfLiteral(const u8 *haystack, const char *needle) {
    if (!haystack || !needle)
        return (bytes) -1;
    const char *h = (const char*) haystack;
    const char *p = strstr(h, needle);
    if (!p)
        return (bytes) -1;
    return (bytes)(p - h);
}

bool String_instring(bytes index, const u8 *haystack, const u8 *needle) {
    if (!haystack || !needle)
        return false;
    bytes pos = String_indexOf(haystack, needle);
    if (pos == (bytes) -1)
        return false;
    return pos == index;
}

bool String_instringLiteral(bytes index, const u8 *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    bytes pos = String_indexOfLiteral(haystack, needle);
    if (pos == (bytes) -1)
        return false;
    return pos == index;
}

u8 *String_substring(const u8 *str, bytes start, bytes len) {
    if (!str)
        return nullptr;
    bytes sLen = string_length(str);
    if (start >= sLen)
        return String("");
    if (start + len > sLen)
        len = sLen - start;
    return string_allocateBytes(str + start, len);
}

u8 *String_substringLiteral(const char *str, bytes start, bytes len) {
    if (!str)
        return nullptr;
    bytes sLen = strlen(str);
    if (start >= sLen)
        return String("");
    if (start + len > sLen)
        len = sLen - start;
    return string_allocateBytes((const u8*) str + start, len);
}

u8 *String_subFirstChar(const u8 *str) {
    return String_substring(str, 0, 1);
}

u8 *String_subLastChar(const u8 *str) {
    if (!str)
        return nullptr;
    bytes sLen = string_length(str);
    if (sLen == 0)
        return String("");
    return String_substring(str, sLen - 1, 1);
}

u8 *String_subFirst(const u8 *str, bytes count) {
    return String_substring(str, 0, count);
}

u8 *String_subLast(const u8 *str, bytes count) {
    if (!str)
        return nullptr;
    bytes sLen = string_length(str);
    if (count >= sLen)
        return string_copy(str);
    return String_substring(str, sLen - count, count);
}

u8 *String_append(const u8 *a, const u8 *b) {
    if (!a && !b)
        return String("");
    if (!a)
        return string_copy(b);
    if (!b)
        return string_copy(a);
    bytes aLen = string_length(a);
    bytes bLen = string_length(b);
    u8 *out = string_allocateUninitialized(aLen + bLen);
    if (!out)
        return nullptr;
    memcpy(out, a, aLen);
    memcpy(out + aLen, b, bLen);
    out[aLen + bLen] = '\0';
    return out;
}

u8 *String_appendLiteral(const u8 *a, const char *b) {
    if (!a && !b)
        return String("");
    if (!a)
        return string_allocate(b);
    if (!b)
        return string_copy(a);
    bytes aLen = string_length(a);
    bytes bLen = strlen(b);
    u8 *out = string_allocateUninitialized(aLen + bLen);
    if (!out)
        return nullptr;
    memcpy(out, a, aLen);
    memcpy(out + aLen, b, bLen);
    out[aLen + bLen] = '\0';
    return out;
}

u8 *String_appendLiterals(const char *a, const char *b) {
    if (!a && !b)
        return String("");
    if (!a)
        return string_allocate(b);
    if (!b)
        return string_allocate(a);
    bytes aLen = strlen(a);
    bytes bLen = strlen(b);
    u8 *out = string_allocateUninitialized(aLen + bLen);
    if (!out)
        return nullptr;
    memcpy(out, a, aLen);
    memcpy(out + aLen, b, bLen);
    out[aLen + bLen] = '\0';
    return out;
}

fn String_appendInto(const u8 *a, const u8 *b, u8 *dest) {
    if (!a || !b || !dest)
        return;
    bytes aLen = string_length(a);
    bytes bLen = string_length(b);
    bytes cap = string_capacity(dest);
    bytes need = aLen + bLen + 1;
    if (cap < need)
        return;
    memmove(dest, a, aLen);
    memcpy(dest + aLen, b, bLen);
    dest[aLen + bLen] = '\0';
}

u8 *String_appendFirst(u8 *a, const u8 *b) {
    if (!a)
        return b ? string_copy(b) : nullptr;
    if (!b || b[0] == '\0')
        return a;
    bytes aLen = string_length(a);
    bytes bLen = string_length(b);
    bytes need = aLen + bLen + 1;
    if (string_capacity(a) < need) {
        u8 *grown = Memory_realloc(a, need);
        if (!grown)
            return a;
        a = grown;
    }
    memcpy(a + aLen, b, bLen + 1);
    return a;
}

u8 *String_appendFirstLiteral(u8 *a, const char *b) {
    if (!a)
        return b ? string_allocate(b) : nullptr;
    if (!b || b[0] == '\0')
        return a;
    bytes aLen = string_length(a);
    bytes bLen = strlen(b);
    bytes need = aLen + bLen + 1;
    if (string_capacity(a) < need) {
        u8 *grown = Memory_realloc(a, need);
        if (!grown)
            return a;
        a = grown;
    }
    memcpy(a + aLen, b, bLen + 1);
    return a;
}
