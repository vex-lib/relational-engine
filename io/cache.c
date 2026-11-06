#include "io/cache.h"
// Production R2 I/O owner: Relational Engine; existing Cache ABI preserved.

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "util/annotation.h"
#include "io/vexhome.h"
#include "io/file.h"
#include "security/crypto.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Cache
 * ============================================================================
 * Persistent on-disk key-value cache confined to the VexHome cache directory:
 * keys are SHA-256 hashed into <dir>/<hash>.bin payload files plus .meta side
 * files carrying cached-at time, TTL, and content size. Writes go through a
 * pid-suffixed temp file and atomic rename so a crash never leaves a torn
 * payload; reads validate TTL and unlink expired entries on touch. The Cache
 * struct is a fixed 512+64 byte dir/subsystem descriptor, malloc'd at open
 * and freed at close — every lookup/put path is cold and allocation-light.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Cache (io/cache.c)
 * ============================================================================
 * High-performance on-disk content cache with cryptographic SHA-256 keys,
 * TTL validation, atomic filesystem writes, and zero-redownload caching.
 *
 * STRUCT FIELDS:
 *   - Cache: dir_path, subsystem_name
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Lifecycle:
 *   - Cache_open(subsystem, cache_out)
 *   - Cache_close(cache)
 * Lookup & Read:
 *   - Cache_has(cache, key)
 *   - Cache_get_path(cache, key, path_out, cap)
 *   - Cache_get_data(cache, key, data_out, size_out)
 * Store & Invalidate:
 *   - Cache_put_data(cache, key, data, size, ttl_sec)
 *   - Cache_put_file(cache, key, source_path, ttl_sec)
 *   - Cache_evict(cache, key)
 *   - Cache_clear(cache)
 *   - Cache_get_dir(cache)
 * ============================================================================
 */

;;INTENTION("Cryptographically keyed disk cache avoiding duplicate downloads and recomputations")


/** Return wall-clock time in milliseconds for cache TTL metadata. */
static uint64_t current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((uint64_t) tv.tv_sec * 1000ULL) + ((uint64_t) tv.tv_usec / 1000ULL);
}

/** Hash a cache key and build its payload and metadata paths below the cache directory. */
static void compute_key_paths(
    const Cache *cache,
    const char *key,
    char out_bin[512],
    char out_meta[512]
) {
    char hash[CRYPTO_SHA256_HEX_SIZE];
    Crypto_sha256Hex(key, strlen(key), hash);
    snprintf(out_bin, 512, "%s/%s.bin", (*cache).dir_path, hash);
    snprintf(out_meta, 512, "%s/%s.meta", (*cache).dir_path, hash);
}

/** Read the cached key hash, creation time, TTL, and content size from a metadata file. */
static bool read_meta(const char *meta_path, CacheEntryMeta *meta_out) {
    FILE *f = fopen(meta_path, "r");
    if (!f) {
        return false;
    }
    if (fscanf(f, "%64s %llu %llu %llu",
               (*meta_out).key_hash,
               (unsigned long long*) &(*meta_out).cached_at_ms,
               (unsigned long long*) &(*meta_out).ttl_sec,
               (unsigned long long*) &(*meta_out).content_size) != 4) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

/** Write the cache entry metadata fields to its sidecar file. */
static bool write_meta(const char *meta_path, const CacheEntryMeta *meta) {
    FILE *f = fopen(meta_path, "w");
    if (!f) {
        return false;
    }
    fprintf(f, "%s %llu %llu %llu\n",
            (*meta).key_hash,
            (unsigned long long) (*meta).cached_at_ms,
            (unsigned long long) (*meta).ttl_sec,
            (unsigned long long) (*meta).content_size);
    fclose(f);
    return true;
}

/** Open a cache rooted in the VexHome cache directory for a subsystem name. */
bool Cache_open(const char *subsystem, Cache **cache_out) {
    if (!cache_out) {
        return false;
    }
    *cache_out = nullptr;

    const char *sub = subsystem ? subsystem : "default";
    VexHome_cacheEnsure(sub);
    const char *dir = VexHome_cache(sub);
    if (!dir) {
        return false;
    }
    File_mkdirs(dir);

    Cache *c = (Cache*) malloc(sizeof(Cache));
    if (!c) {
        return false;
    }

    snprintf((*c).dir_path, sizeof((*c).dir_path), "%s", dir);
    snprintf((*c).subsystem, sizeof((*c).subsystem), "%s", sub);

    *cache_out = c;
    return true;
}

/** Release the descriptor returned by Cache_open; null is accepted. */
void Cache_close(Cache *cache) {
    if (cache) {
        free(cache);
    }
}

/** Check whether a keyed payload exists and remove it when its readable TTL is expired. */
bool Cache_has(const Cache *cache, const char *key) {
    if (!cache || !key) {
        return false;
    }

    char bin_path[512];
    char meta_path[512];
    compute_key_paths(cache, key, bin_path, meta_path);

    struct stat st;
    if (stat(bin_path, &st) != 0) {
        return false;
    }

    CacheEntryMeta meta;
    if (read_meta(meta_path, &meta)) {
        if (meta.ttl_sec > 0) {
            uint64_t now = current_time_ms();
            uint64_t elapsed_sec = (now > meta.cached_at_ms) ? ((now - meta.cached_at_ms) / 1000ULL) : 0;
            if (elapsed_sec > meta.ttl_sec) {
                unlink(bin_path);
                unlink(meta_path);
                return false;
            }
        }
    }

    return true;
}

/** Copy the existing, non-expired payload path into the caller's bounded buffer. */
bool Cache_get_path(const Cache *cache, const char *key, char *path_out, size_t cap) {
    if (!cache || !key || !path_out || cap == 0) {
        return false;
    }

    if (!Cache_has(cache, key)) {
        return false;
    }

    char bin_path[512];
    char meta_path[512];
    compute_key_paths(cache, key, bin_path, meta_path);

    size_t len = strlen(bin_path);
    if (len >= cap) {
        return false;
    }

    strncpy(path_out, bin_path, cap - 1);
    path_out[cap - 1] = '\0';
    return true;
}

/** Allocate and return a NUL-terminated copy of a cached payload and its byte length. */
bool Cache_get_data(const Cache *cache, const char *key, void **data_out, size_t *size_out) {
    if (!cache || !key || !data_out || !size_out) {
        return false;
    }
    *data_out = nullptr;
    *size_out = 0;

    char bin_path[512];
    if (!Cache_get_path(cache, key, bin_path, sizeof(bin_path))) {
        return false;
    }

    FILE *f = fopen(bin_path, "rb");
    if (!f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        return false;
    }

    void *buf = malloc((size_t) sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buf, 1, (size_t) sz, f);
    fclose(f);

    ((char*) buf)[read_bytes] = '\0';
    *data_out = buf;
    *size_out = read_bytes;
    return true;
}

/** Atomically replace a key's payload, then write its timestamp, TTL, and size metadata. */
bool Cache_put_data(Cache *cache, const char *key, const void *data, size_t size, uint64_t ttl_sec) {
    if (!cache || !key || !data) {
        return false;
    }

    char bin_path[512];
    char meta_path[512];
    compute_key_paths(cache, key, bin_path, meta_path);

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp_%u", bin_path, (unsigned int) getpid());

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        return false;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        unlink(tmp_path);
        return false;
    }

    if (rename(tmp_path, bin_path) != 0) {
        unlink(tmp_path);
        return false;
    }

    CacheEntryMeta meta;
    Crypto_sha256Hex(key, strlen(key), meta.key_hash);
    meta.cached_at_ms = current_time_ms();
    meta.ttl_sec = ttl_sec;
    meta.content_size = size;

    return write_meta(meta_path, &meta);
}

/** Read a source file and store its bytes under a key with the requested TTL. */
bool Cache_put_file(Cache *cache, const char *key, const char *source_path, uint64_t ttl_sec) {
    if (!cache || !key || !source_path) {
        return false;
    }

    FILE *src = fopen(source_path, "rb");
    if (!src) {
        return false;
    }

    fseek(src, 0, SEEK_END);
    long sz = ftell(src);
    fseek(src, 0, SEEK_SET);

    if (sz < 0) {
        fclose(src);
        return false;
    }

    void *buf = malloc((size_t) sz);
    if (!buf && sz > 0) {
        fclose(src);
        return false;
    }

    size_t read_bytes = 0;
    if (sz > 0) {
        read_bytes = fread(buf, 1, (size_t) sz, src);
    }
    fclose(src);

    bool ok = Cache_put_data(cache, key, buf ? buf : "", read_bytes, ttl_sec);
    free(buf);
    return ok;
}

/** Unlink the payload and metadata files associated with a key. */
bool Cache_evict(Cache *cache, const char *key) {
    if (!cache || !key) {
        return false;
    }

    char bin_path[512];
    char meta_path[512];
    compute_key_paths(cache, key, bin_path, meta_path);

    unlink(bin_path);
    unlink(meta_path);
    return true;
}

/** Remove cache-directory entries except the preserved dictionary.ini index. */
bool Cache_clear(Cache *cache) {
    if (!cache) {
        return false;
    }

    DIR *d = opendir((*cache).dir_path);
    if (!d) {
        return false;
    }

    struct dirent *entry = nullptr;
    char filepath[1024];

    while ((entry = readdir(d)) != nullptr) {
        const char *name = (*entry).d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "dictionary.ini") == 0) {
            continue;
        }
        snprintf(filepath, sizeof(filepath), "%s/%s", (*cache).dir_path, name);
        unlink(filepath);
    }

    closedir(d);
    return true;
}

/** Return the cache directory path borrowed from the descriptor, or nullptr. */
const char *Cache_get_dir(const Cache *cache) {
    return cache ? (*cache).dir_path : nullptr;
}
