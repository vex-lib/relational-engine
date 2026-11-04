#ifndef IO_CACHE_H
#define IO_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CacheEntryMeta {
    char key_hash[65];      // 64 hex characters + null terminator
    uint64_t cached_at_ms;  // Epoch timestamp in milliseconds
    uint64_t ttl_sec;       // Time-to-live in seconds (0 = never expire)
    uint64_t content_size;  // Size of content in Bytes
} CacheEntryMeta;

typedef struct Cache Cache;

// Initialize or open a cache namespace in <VexHome_root>/cache/<subsystem>/
bool Cache_open(const char *subsystem, Cache **cache_out);

// Close and release cache handle
void Cache_close(Cache *cache);

// Check if a key exists in cache and has not expired
bool Cache_has(const Cache *cache, const char *key);

// Get the absolute path on disk to the cached file for a key
// Returns false if missing or expired
bool Cache_get_path(const Cache *cache, const char *key, char *path_out, size_t cap);

// Read cached data into a dynamically allocated buffer (caller frees)
bool Cache_get_data(const Cache *cache, const char *key, void **data_out, size_t *size_out);

// Store raw Bytes in cache under key with an optional TTL (0 = permanent)
bool Cache_put_data(Cache *cache, const char *key, const void *data, size_t size, uint64_t ttl_sec);

// Store an existing file in cache under key by copying it into the cache store
bool Cache_put_file(Cache *cache, const char *key, const char *source_path, uint64_t ttl_sec);

// Evict a single entry from cache
bool Cache_evict(Cache *cache, const char *key);

// Clear all entries in this cache subsystem
bool Cache_clear(Cache *cache);

// Get cache subsystem directory path
const char *Cache_get_dir(const Cache *cache);

#ifdef __cplusplus
}
#endif

#endif // IO_CACHE_H
