// Relational Engine owner test for the migrated production io/log.
//
// Proves the MPSC log ring + writer daemon:
//   - init rounds the ring up to a power of two, enables/activates, and writes
//     the magic header; nullptr args are refused;
//   - append counts each accepted record exactly once; deactivating makes
//     append a no-op; every attempt is accounted as appended XOR dropped, so
//     appended + dropped always equals the number of calls;
//   - shutdown joins the writer and drains every appended record, after which
//     written == appended; shutdown is idempotent and null-safe;
//   - re-init after shutdown works;
//   - the drained file parses back through LogParser.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/log.h"
#include "io/logparser.h"
#include "io/file.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

int main(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/vexspoke_log_%d", base, getpid());
    File_delete(dir);
    CHECK(File_mkdirs(dir));
    if (g_failures)
        return 1;
    char path[300];
    snprintf(path, sizeof(path), "%s/engine.log", dir);

    // Refusals before a valid open.
    CHECK(!Log_init(nullptr, path, 8));
    Log log;
    CHECK(!Log_init(&log, nullptr, 8));

    CHECK(Log_init(&log, path, 8));                    // 8 -> rounded to 8
    CHECK(Log_isEnabled(&log));
    CHECK(Log_isActive(&log));
    CHECK(strcmp(Log_path(&log), path) == 0);
    CHECK(Log_appended(&log) == 0);
    CHECK(Log_dropped(&log) == 0);
    CHECK(Log_written(&log) == 0);

    Log_append(&log, 1, 10, 20, 30, 40, 50);
    Log_appendKind(&log, 2);
    Log_append(&log, 3, -1, -2, -3, -4, -5);
    CHECK(Log_appended(&log) == 3);

    // Deactivation gates append.
    Log_setActive(&log, false);
    CHECK(!Log_isActive(&log));
    Log_append(&log, 9, 1, 1, 1, 1, 1);
    CHECK(Log_appended(&log) == 3);
    Log_setActive(&log, true);

    // Every attempt is appended XOR dropped.
    uint64_t before = Log_appended(&log) + Log_dropped(&log);
    enum { BURST = 2000 };
    for (int i = 0; i < BURST; i++)
        Log_append(&log, 4, i, 0, 0, 0, 0);
    uint64_t after = Log_appended(&log) + Log_dropped(&log);
    CHECK(after - before == BURST);

    // No-op on a disabled/inactive/zeroed log.
    Log dead;
    memset(&dead, 0, sizeof(dead));
    Log_append(&dead, 1, 0, 0, 0, 0, 0);               // enabled=false -> no-op
    Log_append(nullptr, 1, 0, 0, 0, 0, 0);

    Log_shutdown(&log);
    CHECK(!Log_isEnabled(&log));
    CHECK(Log_written(&log) == Log_appended(&log));    // fully drained
    Log_shutdown(&log);                                // idempotent
    Log_shutdown(nullptr);                             // null-safe

    // The written file is a valid ANTI log with the right record count.
    CHECK(LogParser_isLogFile(path));
    int64_t n = LogParser_count(path);
    CHECK(n >= 0);
    CHECK((uint64_t) n == Log_written(&log));

    // Re-init after shutdown works.
    Log again;
    CHECK(Log_init(&again, path, 4));
    CHECK(Log_isEnabled(&again));
    Log_shutdown(&again);

    // A fresh zero-slot request is clamped to one slot and still functions.
    Log tiny;
    char path2[320];
    snprintf(path2, sizeof(path2), "%s/tiny.log", dir);
    CHECK(Log_init(&tiny, path2, 0));
    Log_append(&tiny, 7, 1, 2, 3, 4, 5);
    CHECK(Log_appended(&tiny) == 1);
    Log_shutdown(&tiny);
    CHECK(LogParser_count(path2) == 1);

    if (g_failures == 0) {
        printf("log_test: all assertions held\n");
        return 0;
    }
    printf("log_test: %d FAILURES\n", g_failures);
    return 1;
}
