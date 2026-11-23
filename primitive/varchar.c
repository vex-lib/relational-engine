#include "vex.h"
#include "re.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VarcharConstructors
 * ============================================================================
 * Arena-backed constructors, cloning, format conversion, and teardown for the
 * floor's immutable `varchar` primitive. Lives in `re` (`primitive/varchar.c`)
 * so `lib` stays zero-dependency and every allocated payload is owned by `re`'s
 * `MemoryPool` / `MemoryArena` under `TYPE_VARCHAR`. Never pools or interns
 * strings; every clone yields a distinct immutable allocation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Varchar (re/primitive/varchar.c)
 * ============================================================================
 * Relational Engine allocator seam over lib's `varchar_data` floor.
 *
 * --CONSTRUCTORS-------------------------
 * public   varchar_1(...)                 empty UTF-8 varchar in Re_arena()
 * public   varchar_2(...)                 UTF-8 varchar in Re_arena()
 * public   varchar_3(...)                 UTF-8 transcoded to TextFormat in Re_arena()
 * public   varchar_in(...)                UTF-8 transcoded to TextFormat in explicit arena
 * public   varchar_fromBytes(...)         pre-encoded bytes varchar in Re_arena()
 * public   varchar_fromBytesIn(...)       pre-encoded bytes varchar in explicit arena
 * public   varchar_clone(...)             distinct immutable clone in Re_arena()
 * public   varchar_cloneIn(...)           distinct immutable clone in explicit arena
 * public   varchar_convert(...)           transcoded clone in Re_arena(), preserving limit
 * public   varchar_convertIn(...)         transcoded clone in explicit arena, preserving limit
 *
 * --FUNCTIONS----------------------------
 * public   varchar_free(...)              releases arena-allocated varchar
 * ============================================================================
 */

/**
 * Constructs an empty UTF-8 immutable varchar in Re_arena() with the given character limit.
 * @param limit maximum allowed Unicode characters (SQL VARCHAR(N))
 * @return allocated immutable varchar, or nullptr on allocation failure
 */
;;CONSTRUCTOR
varchar varchar_1(u32 limit)
{
    return varchar_in(Re_arena(), limit, nullptr, FORMAT_UTF8);
}

/**
 * Constructs a UTF-8 immutable varchar in Re_arena() from a NUL-terminated UTF-8 string.
 * @param limit maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param text  NUL-terminated UTF-8 input string (nullptr produces empty varchar)
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_2(u32 limit, string text)
{
    return varchar_in(Re_arena(), limit, text, FORMAT_UTF8);
}

/**
 * Constructs an immutable varchar in Re_arena() by encoding a UTF-8 string into format.
 * @param limit  maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param text   NUL-terminated UTF-8 input string (nullptr produces empty varchar)
 * @param format target endian-agnostic TextFormat encoding
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_3(u32 limit, string text, TextFormat format)
{
    return varchar_in(Re_arena(), limit, text, format);
}

/**
 * Constructs an immutable varchar in an explicit MemoryArena from a UTF-8 string encoded into format.
 * @param arena  non-null backing MemoryArena
 * @param limit  maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param text   NUL-terminated UTF-8 input string (nullptr produces empty varchar)
 * @param format target endian-agnostic TextFormat encoding
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_in(MemoryArena *arena, u32 limit, string text, TextFormat format)
{
    if (!arena) {
        THROW("varchar_in: arena is nullptr");
        return nullptr;
    }

    if (!TextFormat_isValid(format)) {
        THROW("varchar_in: invalid TextFormat %u", (u32) format);
        return nullptr;
    }

    bytes utf8Bytes = text ? strlen(text) : 0u;
    u32 charCount = 0u;
    bytes encodedBytes = 0u;

    if (!varchar_measureUtf8As(text, utf8Bytes, format, &charCount, &encodedBytes)) {
        THROW("varchar_in: UTF-8 input cannot be encoded as %s", TextFormat_name(format));
        return nullptr;
    }

    if (charCount > limit) {
        THROW("varchar_in: %u characters exceed limit %u", charCount, limit);
        return nullptr;
    }

    bytes totalBytes = varchar_requiredBytes(encodedBytes);

    if (totalBytes == 0u) {
        THROW("varchar_in: allocation size overflow");
        return nullptr;
    }

    any storage = Memory_alloc(TYPE_VARCHAR, arena, totalBytes);

    if (!storage)
        return nullptr;

    varchar out = varchar_initUtf8Into(storage, totalBytes, limit, text, utf8Bytes, format);

    if (!out) {
        varchar_free((varchar) storage);
        return nullptr;
    }

    return out;
}

/**
 * Constructs an immutable varchar in Re_arena() from pre-encoded raw bytes.
 * @param limit      maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param rawBytes   pre-encoded byte buffer in format (may be nullptr when byteLength == 0)
 * @param byteLength byte length of rawBytes
 * @param format     encoding of rawBytes
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_fromBytes(u32 limit, const u8 *rawBytes, bytes byteLength, TextFormat format)
{
    return varchar_fromBytesIn(Re_arena(), limit, rawBytes, byteLength, format);
}

/**
 * Constructs an immutable varchar in an explicit MemoryArena from pre-encoded raw bytes.
 * @param arena      non-null backing MemoryArena
 * @param limit      maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param rawBytes   pre-encoded byte buffer in format (may be nullptr when byteLength == 0)
 * @param byteLength byte length of rawBytes
 * @param format     encoding of rawBytes
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_fromBytesIn(MemoryArena *arena, u32 limit, const u8 *rawBytes, bytes byteLength, TextFormat format)
{
    if (!arena) {
        THROW("varchar_fromBytesIn: arena is nullptr");
        return nullptr;
    }

    if (!TextFormat_isValid(format)) {
        THROW("varchar_fromBytesIn: invalid TextFormat %u", (u32) format);
        return nullptr;
    }

    u32 charCount = 0u;

    if (!varchar_measureBytes(rawBytes, byteLength, format, &charCount)) {
        THROW("varchar_fromBytesIn: invalid %s byte sequence (%zu bytes)", TextFormat_name(format), byteLength);
        return nullptr;
    }

    if (charCount > limit) {
        THROW("varchar_fromBytesIn: %u characters exceed limit %u", charCount, limit);
        return nullptr;
    }

    bytes totalBytes = varchar_requiredBytes(byteLength);

    if (totalBytes == 0u) {
        THROW("varchar_fromBytesIn: allocation size overflow");
        return nullptr;
    }

    any storage = Memory_alloc(TYPE_VARCHAR, arena, totalBytes);

    if (!storage)
        return nullptr;

    varchar out = varchar_initInto(storage, totalBytes, limit, rawBytes, byteLength, format);

    if (!out) {
        varchar_free((varchar) storage);
        return nullptr;
    }

    return out;
}

/**
 * Clones an immutable varchar into a fresh allocation in Re_arena() (never pooled).
 * @param self source varchar
 * @return distinct cloned varchar handle, or nullptr when self is nullptr or allocation fails
 */
;;CONSTRUCTOR
varchar varchar_clone(varchar self)
{
    return varchar_cloneIn(Re_arena(), self);
}

/**
 * Clones an immutable varchar into a fresh allocation in an explicit MemoryArena.
 * @param arena non-null backing MemoryArena
 * @param self  source varchar
 * @return distinct cloned varchar handle, or nullptr on failure
 */
;;CONSTRUCTOR
varchar varchar_cloneIn(MemoryArena *arena, varchar self)
{
    if (!self)
        return nullptr;

    return varchar_fromBytesIn(
        arena,
        varchar_limit(self),
        varchar_value(self),
        (bytes) varchar_byteLength(self),
        varchar_format(self));
}

/**
 * Converts an immutable varchar into a fresh allocation in Re_arena() using targetFormat.
 * @param self         source varchar
 * @param targetFormat target TextFormat encoding
 * @return new varchar in targetFormat preserving limit, or nullptr on failure
 */
;;CONSTRUCTOR
varchar varchar_convert(varchar self, TextFormat targetFormat)
{
    return varchar_convertIn(Re_arena(), self, targetFormat);
}

/**
 * Converts an immutable varchar into a fresh allocation in an explicit MemoryArena using targetFormat.
 * @param arena        non-null backing MemoryArena
 * @param self         source varchar
 * @param targetFormat target TextFormat encoding
 * @return new varchar in targetFormat preserving limit, or nullptr on failure
 */
;;CONSTRUCTOR
varchar varchar_convertIn(MemoryArena *arena, varchar self, TextFormat targetFormat)
{
    if (!arena) {
        THROW("varchar_convertIn: arena is nullptr");
        return nullptr;
    }

    if (!self)
        return nullptr;

    if (!TextFormat_isValid(targetFormat)) {
        THROW("varchar_convertIn: invalid target TextFormat %u", (u32) targetFormat);
        return nullptr;
    }

    u32 charCount = 0u;
    bytes encodedBytes = 0u;

    if (!varchar_measureConvert(self, targetFormat, &charCount, &encodedBytes)) {
        THROW("varchar_convertIn: cannot convert %s payload into %s",
              TextFormat_name(varchar_format(self)), TextFormat_name(targetFormat));
        return nullptr;
    }

    if (charCount > varchar_limit(self)) {
        THROW("varchar_convertIn: %u characters exceed limit %u", charCount, varchar_limit(self));
        return nullptr;
    }

    bytes totalBytes = varchar_requiredBytes(encodedBytes);

    if (totalBytes == 0u) {
        THROW("varchar_convertIn: allocation size overflow");
        return nullptr;
    }

    any storage = Memory_alloc(TYPE_VARCHAR, arena, totalBytes);

    if (!storage)
        return nullptr;

    varchar out = varchar_convertInto(self, targetFormat, storage, totalBytes);

    if (!out) {
        varchar_free((varchar) storage);
        return nullptr;
    }

    return out;
}

/**
 * Releases an arena-allocated immutable varchar back to its owning arena.
 * @param self varchar handle to release (nullptr is a no-op)
 */
;;DECONSTRUCT
fn varchar_free(varchar self)
{
    if (!self)
        return;

    Memory_free((any) (pointer) self);
}
