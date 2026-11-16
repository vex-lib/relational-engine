#include "vex.h"
#include "hash/hash.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Hash
 * ============================================================================
 * Pure hash utility (Relational Engine): FNV-1a 64-bit over byte buffers and
 * Murmur3 final-mix avalanche functions for 64-bit and 32-bit keys. Stateless
 * and allocation-free — every function is a pure mapping from input to output,
 * safe to call from any thread at any time. The mixers are the relational
 * engine's symbol/identity hashing primitives.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Hash (hash/hash.c)
 * ============================================================================
 * The Hash utility: pure FNV-1a and Murmur3 mixing functions.
 *
 * STRUCT FIELDS: none — procedural (operates on byte buffers (pure hash functions))
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Getters:
 *   - Hash_fnv1a64(data, length)
 *   - Hash_pointer(p)
 *   - Hash_murmur3Mix64(k)
 *   - Hash_murmur3Mix32(k)
 * ============================================================================
 */

// hash.c — Hash port (Legacy: util/Hash.java). Pure functions, no state.

static const u64 FNV_OFFSET = 0xcbf29ce484222325ull;
static const u64 FNV_PRIME = 0x100000001b3ull;

// Computes the 64-bit FNV-1a hash of the byte span; empty or null input yields zero.
u64 Hash_fnv1a64(const u8 *data, bytes length) {

    if(!data || length == 0) 
       return 0;

    u64 hash = FNV_OFFSET;

    for(bytes i = 0; i < length; i++) {

        hash ^= (u64) (*data);

        hash *= FNV_PRIME;

        data++;

    }

    return hash;

}

// Mixes a pointer's integer representation into a 64-bit hash.
u64 Hash_pointer(const_any p) {

    return Hash_murmur3Mix64((u64) (pointer) p);

}

// Applies the MurmurHash3 64-bit finalizer to one word.
u64 Hash_murmur3Mix64(u64 k) {

    k ^= k >> 33;

    k *= 0xff51afd7ed558ccdull;

    k ^= k >> 33;

    k *= 0xc4ceb9fe1a85ec53ull;

    k ^= k >> 33;

    return k;

}

// Applies the MurmurHash3 32-bit finalizer to one word.
u32 Hash_murmur3Mix32(u32 k) {

    k ^= k >> 16;

    k *= 0x85ebca6bul;

    k ^= k >> 13;

    k *= 0xc2b2ae35ul;

    k ^= k >> 16;

    return k;

}
