#include "vex.h"
#include "hash/hash.h"
#include "nio/mem.h"
#include "struct/layout.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ElementLayout
 * ============================================================================
 * Shared runtime type-layout value object owned by Relational Engine (lifetime
 * tier). Captured at compile time via ElementLayout_of(T) without central type
 * registries or numeric ID_* tables. Provides 32-byte payload-floor alignment
 * validation, alignment rounding, and uniform hashing/equality for both raw
 * byte values (scalars, structs, pointers) and UTF-8 string keys.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ElementLayout (struct/layout.c)
 * ============================================================================
 * Compile-time-captured layout metadata for generic C23 containers.
 *
 * --FIELDS-------------------------------
 * public   bytes          stride           element byte width (sizeof(T))
 * public   bytes          alignment        required alignment (alignof(T) <= 32)
 * public   u64            typeHash         layout fingerprint over name/size/align
 * public   string         typeName         human-readable type spelling (#T)
 * public   ElementKeyMode keyMode          bytes vs UTF-8 string content hashing
 *
 * --CONSTRUCTORS-------------------------
 * public   ElementLayout_makeMode(...)     constructs layout with explicit keyMode
 * public   ElementLayout_make(...)         constructs byte-mode layout
 *
 * --FUNCTIONS----------------------------
 * public   ElementLayout_isValid(...)      checks stride > 0 and 32B align floor
 * public   ElementLayout_equals(...)       compares two layouts for exact match
 * public   ElementLayout_hash(...)         hashes one element by keyMode
 * public   ElementLayout_bytesEqual(...)   compares two elements by keyMode
 * public   ElementLayout_alignUp(...)      rounds byte offset up to alignment
 * private  isPowerOfTwo(...)               checks power-of-two alignment
 * private  naturalAlignment(...)           derives alignment when 0 is passed
 *
 * --GETTERS------------------------------
 * public   ElementLayout_stride(...)       reads stride
 * public   ElementLayout_alignment(...)    reads alignment
 * public   ElementLayout_typeHash(...)     reads typeHash
 * public   ElementLayout_typeName(...)     reads typeName
 * public   ElementLayout_keyMode(...)      reads keyMode
 *
 * --SETTERS------------------------------
 * private  ElementLayout_setStride(...)    sets stride
 * private  ElementLayout_setAlignment(...) sets alignment
 * private  ElementLayout_setTypeHash(...)  sets typeHash
 * private  ElementLayout_setTypeName(...)  sets typeName
 * private  ElementLayout_setKeyMode(...)   sets keyMode
 * ============================================================================
 */

;;SETTER
static inline fn ElementLayout_setStride(ElementLayout *layout, bytes stride)
{
    (*layout).stride = stride;
}

;;SETTER
static inline fn ElementLayout_setAlignment(ElementLayout *layout, bytes alignment)
{
    (*layout).alignment = alignment;
}

;;SETTER
static inline fn ElementLayout_setTypeHash(ElementLayout *layout, u64 typeHash)
{
    (*layout).typeHash = typeHash;
}

;;SETTER
static inline fn ElementLayout_setTypeName(ElementLayout *layout, string typeName)
{
    (*layout).typeName = typeName;
}

;;SETTER
static inline fn ElementLayout_setKeyMode(ElementLayout *layout, ElementKeyMode mode)
{
    (*layout).keyMode = mode;
}

;;GETTER
bytes ElementLayout_stride(ElementLayout layout)
{
    return layout.stride;
}

;;GETTER
bytes ElementLayout_alignment(ElementLayout layout)
{
    return layout.alignment;
}

;;GETTER
u64 ElementLayout_typeHash(ElementLayout layout)
{
    return layout.typeHash;
}

;;GETTER
string ElementLayout_typeName(ElementLayout layout)
{
    if(!layout.typeName)
        return "unknown";

    return layout.typeName;
}

;;GETTER
ElementKeyMode ElementLayout_keyMode(ElementLayout layout)
{
    return layout.keyMode;
}

static bool isPowerOfTwo(bytes value)
{
    if(value == 0)
        return false;

    return (value & (value - 1u)) == 0;
}

static bytes naturalAlignment(bytes stride)
{
    if(stride == 0)
        return 1;

    bytes align = 1;

    while(align < MEMORY_PAYLOAD_ALIGN && (stride % (align * 2u)) == 0)
        align *= 2u;

    return align;
}

;;CONSTRUCTOR
ElementLayout ElementLayout_makeMode(bytes stride, bytes alignment, string typeName, ElementKeyMode keyMode)
{
    ElementLayout layout;
    memset(&layout, 0, sizeof(layout));

    string label = typeName ? typeName : "unknown";
    bytes resolvedAlign = alignment == 0 ? naturalAlignment(stride) : alignment;

    bytes nameLen = strlen(label);
    u64 nameHash = Hash_fnv1a64((const u8*) label, nameLen);
    u64 mixed = Hash_murmur3Mix64(nameHash ^ ((u64) stride * 0x9E3779B97F4A7C15ull) ^ (u64) resolvedAlign);

    ElementLayout_setStride(&layout, stride);
    ElementLayout_setAlignment(&layout, resolvedAlign);
    ElementLayout_setTypeHash(&layout, mixed);
    ElementLayout_setTypeName(&layout, label);
    ElementLayout_setKeyMode(&layout, keyMode);

    return layout;
}

;;CONSTRUCTOR
ElementLayout ElementLayout_make(bytes stride, bytes alignment, string typeName)
{
    return ElementLayout_makeMode(stride, alignment, typeName, ELEMENT_KEY_BYTES);
}

;;VALIDATOR
bool ElementLayout_isValid(ElementLayout layout)
{
    bytes stride = ElementLayout_stride(layout);
    bytes alignment = ElementLayout_alignment(layout);

    if(stride == 0)
        return false;

    if(!isPowerOfTwo(alignment))
        return false;

    if(alignment > MEMORY_PAYLOAD_ALIGN)
        return false;

    if((stride % alignment) != 0)
        return false;

    return true;
}

bool ElementLayout_equals(ElementLayout a, ElementLayout b)
{
    if(ElementLayout_stride(a) != ElementLayout_stride(b))
        return false;

    if(ElementLayout_alignment(a) != ElementLayout_alignment(b))
        return false;

    return ElementLayout_typeHash(a) == ElementLayout_typeHash(b);
}

;;HOTCODE
u64 ElementLayout_hash(ElementLayout layout, const_any data)
{
    if(!data)
        return 0;

    if(ElementLayout_keyMode(layout) == ELEMENT_KEY_STRING) {
        string text = nullptr;
        memcpy(&text, data, sizeof(string));

        if(!text)
            return 0x9E3779B97F4A7C15ull;

        bytes len = strlen(text);
        u64 h = Hash_fnv1a64((const u8*) text, len);

        return h == 0 ? 1ull : h;
    }

    bytes stride = ElementLayout_stride(layout);

    if(stride == 1) {
        u8 v = *(const u8*) data;

        return Hash_murmur3Mix64((u64) v);
    }

    if(stride == 2) {
        u16 v = 0;
        memcpy(&v, data, 2);

        return Hash_murmur3Mix64((u64) v);
    }

    if(stride == 4) {
        u32 v = 0;
        memcpy(&v, data, 4);

        return Hash_murmur3Mix64((u64) v);
    }

    if(stride == 8) {
        u64 v = 0;
        memcpy(&v, data, 8);

        return Hash_murmur3Mix64(v);
    }

    return Hash_fnv1a64((const u8*) data, stride);
}

;;HOTCODE
bool ElementLayout_bytesEqual(ElementLayout layout, const_any a, const_any b)
{
    if(a == b)
        return true;

    if(!a || !b)
        return false;

    if(ElementLayout_keyMode(layout) == ELEMENT_KEY_STRING) {
        string sa = nullptr;
        string sb = nullptr;
        memcpy(&sa, a, sizeof(string));
        memcpy(&sb, b, sizeof(string));

        if(sa == sb)
            return true;

        if(!sa || !sb)
            return false;

        bytes la = strlen(sa);
        bytes lb = strlen(sb);

        if(la != lb)
            return false;

        return memcmp(sa, sb, la) == 0;
    }

    return memcmp(a, b, ElementLayout_stride(layout)) == 0;
}

bytes ElementLayout_alignUp(bytes value, bytes alignment)
{
    if(alignment <= 1)
        return value;

    bytes mask = alignment - 1u;

    if(value > (SIZE_MAX - mask))
        return value;

    return (value + mask) & ~mask;
}
