// Relational Engine owner test for the migrated VexHome cache factory and layout.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "io/vexhome.h"
#include "io/file.h"

static int g_failures = 0;
#define CHECK(cond) do{ if(!(cond)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond); g_failures++; } }while(0)

/* Creates a per-process temporary root and points VEX_HOME at it for the test. */
static void setup_temp_vexhome(char *tmpdir, size_t tmpdir_sz) {
    const char *tmp_base = getenv("TMPDIR");
    if (!tmp_base || *tmp_base == '\0')
        tmp_base = "/tmp";
    snprintf(tmpdir, tmpdir_sz, "%s/vexhome_test_%d", tmp_base, getpid());
    File_delete(tmpdir);
    CHECK(File_mkdirs(tmpdir));
    setenv("VEX_HOME", tmpdir, 1);
}

/* Removes the temporary VEX_HOME override and deletes its test directory. */
static void teardown_temp_vexhome(const char *tmpdir) {
    unsetenv("VEX_HOME");
    File_delete(tmpdir);
}

/* Verifies cache/index creation, preservation of an edited index, and root-cache fallbacks. */
static void testVexHomeCache(void) {
    char tmpdir[FILE_PATH_MAX];
    setup_temp_vexhome(tmpdir, sizeof(tmpdir));

    // Test 1: cacheEnsure creates cache dir and dictionary.ini
    bool ok = VexHome_cacheEnsure("testsub");
    CHECK(ok);

    const char *cache_dir = VexHome_cache("testsub");
    CHECK(cache_dir != nullptr);
    CHECK(File_exists(cache_dir));
    CHECK(File_isDirectory(cache_dir));

    const char *index_path = VexHome_cacheIndex("testsub");
    CHECK(index_path != nullptr);
    CHECK(File_exists(index_path));

    // Verify dictionary.ini content
    File *f = File_open(index_path, FILE_MODE_READ);
    CHECK(f != nullptr);
    char buf[512];
    int64_t n = File_read(f, buf, sizeof(buf) - 1);
    CHECK(n > 0);
    buf[n] = '\0';
    File_close(f);

    CHECK(strstr(buf, "# vexgraph cache index") != nullptr);
    CHECK(strstr(buf, "subsystem = testsub") != nullptr);
    CHECK(strstr(buf, "version = 1") != nullptr);
    CHECK(strstr(buf, "created = ") != nullptr);

    // Test 2: Second cacheEnsure does not overwrite edited dictionary.ini
    // Edit the dictionary.ini
    f = File_open(index_path, FILE_MODE_WRITE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    const char *edited = "# edited by test\n[cache]\nsubsystem = testsub\ncreated = 2024-01-01\nversion = 99\n";
    int64_t w = File_write(f, edited, (int64_t)strlen(edited));
    CHECK(w == (int64_t)strlen(edited));
    File_close(f);

    // Call cacheEnsure again - should NOT overwrite
    ok = VexHome_cacheEnsure("testsub");
    CHECK(ok);

    f = File_open(index_path, FILE_MODE_READ);
    CHECK(f != nullptr);
    n = File_read(f, buf, sizeof(buf) - 1);
    CHECK(n > 0);
    buf[n] = '\0';
    File_close(f);

    CHECK(strstr(buf, "# edited by test") != nullptr);
    CHECK(strstr(buf, "version = 99") != nullptr);
    CHECK(strstr(buf, "created = 2024-01-01") != nullptr);

    // Test 3: NULL/empty subsystem falls back to root cache dir
    const char *root_cache = VexHome_cache(nullptr);
    CHECK(root_cache != nullptr);
    CHECK(strstr(root_cache, "/cache") != nullptr);
    CHECK(strstr(root_cache, "/testsub") == nullptr);

    const char *empty_cache = VexHome_cache("");
    CHECK(empty_cache != nullptr);
    CHECK(strcmp(root_cache, empty_cache) == 0);

    const char *root_index = VexHome_cacheIndex(nullptr);
    CHECK(root_index != nullptr);
    CHECK(strstr(root_index, "/cache/dictionary.ini") != nullptr);

    // Test 4: VexHome_ensure creates cache dir too
    char tmpdir2[FILE_PATH_MAX];
    setup_temp_vexhome(tmpdir2, sizeof(tmpdir2));

    ok = VexHome_ensure();
    CHECK(ok);

    const char *cache_after_ensure = VexHome_cache(nullptr);
    CHECK(File_exists(cache_after_ensure));
    CHECK(File_isDirectory(cache_after_ensure));

    teardown_temp_vexhome(tmpdir2);

    // Test 5: VEX_HOME precedence — direct unit check
    char tmpdir3[FILE_PATH_MAX];
    setup_temp_vexhome(tmpdir3, sizeof(tmpdir3));

    const char *root = VexHome_root();
    CHECK(root != nullptr);
    CHECK(strcmp(root, tmpdir3) == 0);

    teardown_temp_vexhome(tmpdir3);

    // Cleanup
    teardown_temp_vexhome(tmpdir);
}

/* Verifies each VexHome path remains rooted beneath the configured temporary directory. */
static void testVexHomePathsPrefix(void) {
    char tmpdir[FILE_PATH_MAX];
    setup_temp_vexhome(tmpdir, sizeof(tmpdir));

    const char *root = VexHome_root();
    CHECK(root != nullptr);
    CHECK(strncmp(root, tmpdir, strlen(tmpdir)) == 0);

    const char *projects = VexHome_projects();
    CHECK(projects != nullptr);
    CHECK(strncmp(projects, tmpdir, strlen(tmpdir)) == 0);
    CHECK(strstr(projects, "/projects") != nullptr);

    const char *logs = VexHome_logs();
    CHECK(logs != nullptr);
    CHECK(strncmp(logs, tmpdir, strlen(tmpdir)) == 0);
    CHECK(strstr(logs, "/logs") != nullptr);

    const char *fonts = VexHome_fonts();
    CHECK(fonts != nullptr);
    CHECK(strncmp(fonts, tmpdir, strlen(tmpdir)) == 0);
    CHECK(strstr(fonts, "/fonts") != nullptr);

    const char *placeholder = VexHome_placeholder();
    CHECK(placeholder != nullptr);
    CHECK(strncmp(placeholder, tmpdir, strlen(tmpdir)) == 0);
    CHECK(strstr(placeholder, "/placeholder") != nullptr);

    const char *cache = VexHome_cache("subsys");
    CHECK(cache != nullptr);
    CHECK(strncmp(cache, tmpdir, strlen(tmpdir)) == 0);
    CHECK(strstr(cache, "/cache/subsys") != nullptr);

    const char *cache_index = VexHome_cacheIndex("subsys");
    CHECK(cache_index != nullptr);
    CHECK(strncmp(cache_index, tmpdir, strlen(tmpdir)) == 0);
    CHECK(strstr(cache_index, "/cache/subsys/dictionary.ini") != nullptr);

    teardown_temp_vexhome(tmpdir);
}

/* Runs the VexHome cache and path-layout owner cases and reports aggregate failure status. */
int main(void) {
    testVexHomeCache();
    testVexHomePathsPrefix();
    if (g_failures == 0)
        printf("vexhome_test: all checks passed\n");
    else
        printf("vexhome_test: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
