// Relational Engine owner test for the migrated production io/cache.
//
// Proves the persistent SHA-256-keyed disk cache, isolated under a scratch
// VEX_HOME so it never touches the real state directory:
//   - open/close lifecycle; get_dir is the subsystem cache directory;
//   - miss (has/path/data) then put/get round trip of raw Bytes;
//   - get_data returns a caller-owned buffer (freed here) with exact content;
//   - a permanent entry and a TTL entry both present immediately;
//   - overwrite replaces content; evict removes; clear empties;
//   - put_file copies an existing file into the cache;
//   - nullptr arguments are safe refusals.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/cache.h"
#include "io/file.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static char g_home[256];

/* Exercises isolated cache lifecycle, disk round trips, file insertion, eviction, and null guards. */
int main(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    snprintf(g_home, sizeof(g_home), "%s/vexspoke_cache_%d", base, getpid());
    File_delete(g_home);
    CHECK(File_mkdirs(g_home));
    CHECK(setenv("VEX_HOME", g_home, 1) == 0);

    // Misses before anything is stored.
    Cache *c = nullptr;
    CHECK(Cache_open("unittest", &c));
    CHECK(c != nullptr);
    const char *dir = Cache_get_dir(c);
    CHECK(dir != nullptr && strstr(dir, "cache") != nullptr);
    CHECK(!Cache_has(c, "absent"));
    char path[512];
    CHECK(!Cache_get_path(c, "absent", path, sizeof(path)));
    void *data = nullptr;
    size_t size = 0;
    CHECK(!Cache_get_data(c, "absent", &data, &size));

    // put/get raw Bytes.
    CHECK(Cache_put_data(c, "greeting", "hello world", 11, 0));
    CHECK(Cache_has(c, "greeting"));
    CHECK(Cache_get_path(c, "greeting", path, sizeof(path)));
    CHECK(File_exists(path));
    CHECK(Cache_get_data(c, "greeting", &data, &size));
    CHECK(size == 11 && memcmp(data, "hello world", 11) == 0);
    free(data);

    // A TTL entry is present immediately.
    CHECK(Cache_put_data(c, "ttl", "x", 1, 3600));
    CHECK(Cache_has(c, "ttl"));

    // Overwrite replaces the content and size.
    CHECK(Cache_put_data(c, "greeting", "hi", 2, 0));
    CHECK(Cache_get_data(c, "greeting", &data, &size));
    CHECK(size == 2 && memcmp(data, "hi", 2) == 0);
    free(data);

    // put_file copies a source file.
    char src[300];
    snprintf(src, sizeof(src), "%s/source.bin", g_home);
    File *f = File_open(src, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_write(f, "payload-Bytes", 13) == 13);
    File_close(f);
    CHECK(Cache_put_file(c, "from-file", src, 0));
    CHECK(Cache_has(c, "from-file"));
    CHECK(Cache_get_data(c, "from-file", &data, &size));
    CHECK(size == 13 && memcmp(data, "payload-Bytes", 13) == 0);
    free(data);

    // Evict.
    CHECK(Cache_evict(c, "ttl"));
    CHECK(!Cache_has(c, "ttl"));
    CHECK(Cache_evict(c, "never-existed"));            // idempotent

    // Clear removes the rest.
    CHECK(Cache_clear(c));
    CHECK(!Cache_has(c, "greeting"));
    CHECK(!Cache_has(c, "from-file"));

    // Nullptr safety.
    CHECK(!Cache_open("x", nullptr));
    Cache_close(nullptr);
    CHECK(!Cache_has(nullptr, "k"));
    CHECK(!Cache_has(c, nullptr));
    CHECK(!Cache_get_path(c, "k", nullptr, 8));
    CHECK(!Cache_get_path(c, "k", path, 0));
    CHECK(!Cache_get_data(c, "k", nullptr, &size));
    CHECK(!Cache_get_data(c, "k", &data, nullptr));
    CHECK(!Cache_put_data(nullptr, "k", "v", 1, 0));
    CHECK(!Cache_put_data(c, nullptr, "v", 1, 0));
    CHECK(!Cache_put_data(c, "k", nullptr, 1, 0));
    CHECK(!Cache_put_file(c, "k", nullptr, 0));
    CHECK(!Cache_evict(c, nullptr));
    CHECK(!Cache_clear(nullptr));
    CHECK(Cache_get_dir(nullptr) == nullptr);

    Cache_close(c);

    if (g_failures == 0) {
        printf("cache_test: all assertions held\n");
        return 0;
    }
    printf("cache_test: %d FAILURES\n", g_failures);
    return 1;
}
