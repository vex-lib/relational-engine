// primitive/char_sequence.c — non-owning text views.
//
// Byte comparison over explicit lengths, so a view never reads past its bound
// and never cares whether the bytes are terminated. This file is the owner of
// the libc string measurements: no other file in the tree calls strcmp,
// strncmp, strstr or strchr.

#include "vex.h"
#include "re.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: CharSequence
 * ============================================================================
 * A length plus a borrowed pointer. Every operation is bounded by the length,
 * so a view over a JSON string, a mapped file slice or a stack buffer behaves
 * identically, and none of them needs a terminator. Comparison is
 * lexicographic over the shared prefix and then by length; search is a bounded
 * scan that never reads beyond `length`. No allocation, no copy, no ownership:
 * a view outlives nothing.
 * ============================================================================
 */

CharSequence CharSequence_2(const char *data, bytes length)
{
    CharSequence view = {0};
    view.data = data;
    view.length = data != nullptr ? length : 0;
    return view;
}

CharSequence CharSequence_cstr(const char *text)
{
    if (text == nullptr)
        return CharSequence_zero();
    return CharSequence_2(text, strlen(text));
}

CharSequence CharSequence_block(const u8 *block)
{
    if (block == nullptr)
        return CharSequence_zero();
    return CharSequence_2(string_get(block), string_length(block));
}

bytes CharSequence_length(CharSequence self)
{
    return self.length;
}

bool CharSequence_isEmpty(CharSequence self)
{
    return self.length == 0;
}

i32 CharSequence_compare(CharSequence left, CharSequence right)
{
    bytes common = left.length < right.length ? left.length : right.length;
    if (common > 0)
    {
        i32 order = memcmp(left.data, right.data, common);
        if (order != 0)
            return order;
    }
    if (left.length == right.length)
        return 0;
    return left.length < right.length ? -1 : 1;
}

bool CharSequence_equals(CharSequence left, CharSequence right)
{
    if (left.length != right.length)
        return false;
    if (left.length == 0)
        return true;
    return memcmp(left.data, right.data, left.length) == 0;
}

bool CharSequence_startsWith(CharSequence self, CharSequence prefix)
{
    if (prefix.length > self.length)
        return false;
    if (prefix.length == 0)
        return true;
    return memcmp(self.data, prefix.data, prefix.length) == 0;
}

bool CharSequence_endsWith(CharSequence self, CharSequence suffix)
{
    if (suffix.length > self.length)
        return false;
    if (suffix.length == 0)
        return true;
    return memcmp(self.data + (self.length - suffix.length), suffix.data,
                  suffix.length) == 0;
}

bytes CharSequence_find(CharSequence self, CharSequence needle)
{
    if (needle.length == 0)
        return 0;
    if (needle.length > self.length)
        return SIZE_MAX;

    bytes last = self.length - needle.length;
    for (bytes index = 0; index <= last; ++index)
    {
        if (self.data[index] != needle.data[0])
            continue;
        if (memcmp(self.data + index, needle.data, needle.length) == 0)
            return index;
    }
    return SIZE_MAX;
}

bytes CharSequence_findChar(CharSequence self, char needle)
{
    if (self.length == 0)
        return SIZE_MAX;
    const char *hit = memchr(self.data, needle, self.length);
    if (hit == nullptr)
        return SIZE_MAX;
    return (bytes) (hit - self.data);
}

bool CharSequence_hasPrefix(CharSequence self, const char *literal)
{
    return CharSequence_startsWith(self, CharSequence_cstr(literal));
}
