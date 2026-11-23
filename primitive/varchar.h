#ifndef RE_PRIMITIVE_VARCHAR_H
#define RE_PRIMITIVE_VARCHAR_H

/**
 * primitive/varchar.h -- arena-backed constructors for immutable `varchar`,
 * owned by Relational Engine (`re`).
 *
 * The zero-dependency floor primitive body (`TextFormat`, `varchar_data`,
 * `varchar`, encoding/decoding, validation, comparisons, getters, `toString`)
 * lives in `lib/lang/varchar.h`. `re` owns the arena allocation surface so
 * every runtime byte stays inside `re`'s `MemoryPool` / `MemoryArena`.
 *
 * Usage:
 *   varchar empty = varchar(100);
 *   varchar hello = varchar(100, "hello world");
 *   varchar wide  = varchar(100, "hello world", FORMAT_UTF16);
 *   varchar copy  = varchar_clone(hello);
 */

#include "vex.h"
#include "nio/mem.h"

/**
 * Constructs an empty UTF-8 immutable varchar in Re_arena() with the given character limit.
 * @param limit maximum allowed Unicode characters (SQL VARCHAR(N))
 * @return allocated immutable varchar, or nullptr on allocation failure
 */
;;CONSTRUCTOR
varchar varchar_1(u32 limit);

/**
 * Constructs a UTF-8 immutable varchar in Re_arena() from a NUL-terminated UTF-8 string.
 * @param limit maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param text  NUL-terminated UTF-8 input string (nullptr produces empty varchar)
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_2(u32 limit, string text);

/**
 * Constructs an immutable varchar in Re_arena() by encoding a UTF-8 string into format.
 * @param limit  maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param text   NUL-terminated UTF-8 input string (nullptr produces empty varchar)
 * @param format target endian-agnostic TextFormat encoding
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_3(u32 limit, string text, TextFormat format);

#define varchar(...) CONSTRUCTOR_DISPATCH(varchar, __VA_ARGS__)

/**
 * Constructs an immutable varchar in an explicit MemoryArena from a UTF-8 string encoded into format.
 * @param arena  non-null backing MemoryArena
 * @param limit  maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param text   NUL-terminated UTF-8 input string (nullptr produces empty varchar)
 * @param format target endian-agnostic TextFormat encoding
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_in(MemoryArena *arena, u32 limit, string text, TextFormat format);

/**
 * Constructs an immutable varchar in Re_arena() from pre-encoded raw bytes.
 * @param limit      maximum allowed Unicode characters (SQL VARCHAR(N))
 * @param rawBytes   pre-encoded byte buffer in format (may be nullptr when byteLength == 0)
 * @param byteLength byte length of rawBytes
 * @param format     encoding of rawBytes
 * @return allocated immutable varchar, or nullptr on validation/limit/allocation failure
 */
;;CONSTRUCTOR
varchar varchar_fromBytes(u32 limit, const u8 *rawBytes, bytes byteLength, TextFormat format);

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
varchar varchar_fromBytesIn(MemoryArena *arena, u32 limit, const u8 *rawBytes, bytes byteLength, TextFormat format);

/**
 * Clones an immutable varchar into a fresh allocation in Re_arena() (never pooled).
 * @param self source varchar
 * @return distinct cloned varchar handle, or nullptr when self is nullptr or allocation fails
 */
;;CONSTRUCTOR
varchar varchar_clone(varchar self);

/**
 * Clones an immutable varchar into a fresh allocation in an explicit MemoryArena.
 * @param arena non-null backing MemoryArena
 * @param self  source varchar
 * @return distinct cloned varchar handle, or nullptr on failure
 */
;;CONSTRUCTOR
varchar varchar_cloneIn(MemoryArena *arena, varchar self);

/**
 * Converts an immutable varchar into a fresh allocation in Re_arena() using targetFormat.
 * @param self         source varchar
 * @param targetFormat target TextFormat encoding
 * @return new varchar in targetFormat preserving limit, or nullptr on failure
 */
;;CONSTRUCTOR
varchar varchar_convert(varchar self, TextFormat targetFormat);

/**
 * Converts an immutable varchar into a fresh allocation in an explicit MemoryArena using targetFormat.
 * @param arena        non-null backing MemoryArena
 * @param self         source varchar
 * @param targetFormat target TextFormat encoding
 * @return new varchar in targetFormat preserving limit, or nullptr on failure
 */
;;CONSTRUCTOR
varchar varchar_convertIn(MemoryArena *arena, varchar self, TextFormat targetFormat);

/**
 * Releases an arena-allocated immutable varchar back to its owning arena.
 * @param self varchar handle to release (nullptr is a no-op)
 */
;;DECONSTRUCT
fn varchar_free(varchar self);

#endif
