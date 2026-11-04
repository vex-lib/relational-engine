#include <assert.h>
// Relational Engine owner test for the migrated production clipboard seam.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/clipboard.h"
#include "io/cache.h"
#include "c23/free.h"
#include "test_support.h"

/* Exercises the host clipboard round trip; execution is gated by explicit mutation permission. */
static void test_clipboard(void) {
    printf("Testing clipboard bridge...\n");
    const char *test_str = "VexGraph Ecosystem Clipboard Payload";

    assert(Clipboard_setText(test_str));
    assert(Clipboard_hasText());

    char out_buf[128];
    size_t len = Clipboard_getText(out_buf, sizeof(out_buf));
    assert(len > 0);
    assert(strcmp(out_buf, test_str) == 0);

    Clipboard_clear();
}

/* Runs cache put/get, path lookup, eviction, and clearing under a temporary VEX_HOME. */
static void test_cache(void) {
    printf("Testing persistent cache engine...\n");

    char tmpdir[512];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vex_cache_test_%d", (int) getpid());
    setenv("VEX_HOME", tmpdir, 1);

    Cache *cache = nullptr;
    assert(Cache_open("unit_test_cache", &cache));
    assert(cache != nullptr);

    const char *key = "https://raw.githubusercontent.com/vexgraph-ecosystem/formula/core.json";
    const char *payload = "{\"name\": \"vexgraph\", \"version\": \"2.0.0\"}";

    // Store data in cache
    assert(Cache_put_data(cache, key, payload, strlen(payload), 3600));

    // Has key
    assert(Cache_has(cache, key));
    assert(!Cache_has(cache, "https://nonexistent.org/bogus"));

    // Get path
    char path[512];
    assert(Cache_get_path(cache, key, path, sizeof(path)));
    assert(strlen(path) > 0);

    // Get data
    void *data = nullptr;
    size_t size = 0;
    assert(Cache_get_data(cache, key, &data, &size));
    assert(size == strlen(payload));
    assert(memcmp(data, payload, size) == 0);
    free(data);

    // Evict
    assert(Cache_evict(cache, key));
    assert(!Cache_has(cache, key));

    // Clear
    assert(Cache_clear(cache));
    Cache_close(cache);
}

static int custom_dtor_calls = 0;
/* Counts calls to verify the registered destructor callback is dispatched. */
static void custom_destructor(void *ptr) {
    (void) ptr;
    custom_dtor_calls++;
}

/* Verifies C23 destructor registration and callback lookup/invocation. */
static void test_destructor_dispatch(void) {
    printf("Testing c23 destructor dispatch...\n");
    uint32_t my_type = 9999;
    Destructor_register(my_type, custom_destructor);
    DestructorFn fn = Destructor_lookup(my_type);
    assert(fn == custom_destructor);

    fn(nullptr);
    assert(custom_dtor_calls == 1);
}

int main(void) {
    const char *permission = getenv("RE_TEST_SYSTEM_CLIPBOARD");
    if (!permission || strcmp(permission, "1") != 0) {
        fputs("SKIP: system clipboard mutation requires RE_TEST_SYSTEM_CLIPBOARD=1\n", stderr);
        return B_TEST_SKIP;
    }
    printf("[Clipboard & Cache Test] Starting test suite...\n");
    test_clipboard();
    test_cache();
    test_destructor_dispatch();
    printf("[Clipboard & Cache Test] ALL TESTS PASSED\n");
    return 0;
}
