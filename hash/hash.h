#ifndef RE_HASH_HASH_H
#define RE_HASH_HASH_H

#include "vex.h"

// hash/hash.h — the Hash utility (Relational Engine's two-hash primitives).
//
// Fast 64-bit non-cryptographic hashing. FNV-1a feeds variable-length blocks
// (map keys and symbol tails); the Murmur3 finalizers turn a scalar 64-bit or
// 32-bit key into a well-mixed digest in a few multiply-xor rounds.

// FNV-1a 64-bit over a byte block. Returns 0 on a nullptr/empty block.
u64 Hash_fnv1a64(const u8 *data, bytes length);

// Pointer-identity hash: mixes the 8-byte address itself, never the pointee.
// Stable for a run (addresses are process-local); the whole-pointer hash of the
// Relational Engine's two-hash design — symbol names use the avalanched FNV/
// Murmur path instead.
u64 Hash_pointer(const_any p);

// MurmurHash3 64-bit finalizer mix for a single 64-bit value.
u64 Hash_murmur3Mix64(u64 k);

// MurmurHash3 32-bit finalizer mix for a single 32-bit value.
u32 Hash_murmur3Mix32(u32 k);

#endif
