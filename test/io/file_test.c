// Relational Engine owner test for the migrated production io/file.
//
// Proves the stdio-backed File wrapper end to end in a scratch directory:
//   - isDirectory / exists / mkdirs (nested, idempotent) / delete;
//   - open for write+create+truncate, write, flush, close, size/pos tracking;
//   - reopen for read, exact read, EOF behavior, seek, partial reads;
//   - append mode preserves and positions at the end;
//   - write+truncate on an existing file resets it;
//   - error paths: nullptr path, missing read, negative read/write lengths,
//     null destination, bad seek; nullptr close.

#include "vex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "io/file/file.h"
#include "nio/mem.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static char g_dir[256];

/* Joins a scratch-root path and leaf into the caller's bounded output buffer. */
static fn path_of(char *out, bytes cap, const char *leaf) {
    snprintf(out, cap, "%s/%s", g_dir, leaf);
}

/* Checks directory/file operations, nested creation, idempotence, and null paths. */
static fn test_path_ops() {
    char nested[300];
    path_of(nested, sizeof(nested), "a/b.sh/c");
    CHECK(!File_isDirectory(nested));
    CHECK(File_mkdirs(nested));
    CHECK(File_exists(nested));
    CHECK(File_isDirectory(nested));
    CHECK(File_mkdirs(nested));
    CHECK(File_isDirectory(g_dir));

    CHECK(!File_exists("/no/such/vexspoke/path"));
    CHECK(!File_isDirectory("/no/such/vexspoke/path"));
    CHECK(!File_exists(nullptr));
    CHECK(!File_isDirectory(nullptr));
    CHECK(!File_mkdirs(nullptr));
    CHECK(!File_delete(nullptr));
}

/* Verifies file creation, write/read/seek/EOF, truncate, and deletion behavior. */
static fn test_write_read() {
    char p[300];
    path_of(p, sizeof(p), "data.bin");

    CHECK(!File_exists(p));
    File *f = File_open(p, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_mode(f) == (FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE));
    CHECK(File_name(f) != nullptr && strcmp(File_name(f), p) == 0);
    CHECK(File_handle(f) != nullptr);
    CHECK(File_size(f) == 0);
    CHECK(File_pos(f) == 0);

    CHECK(File_write(f, "hello", 5) == 5);
    CHECK(File_size(f) == 5);
    CHECK(File_pos(f) == 5);
    CHECK(File_flush(f));
    CHECK(File_close(f));
    CHECK(File_exists(p));

    f = File_open(p, FILE_MODE_READ);
    CHECK(f != nullptr);
    CHECK(File_size(f) == 5);
    char buf[16] = { 0 };
    CHECK(File_read(f, buf, sizeof(buf)) == 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    CHECK(File_pos(f) == 5);
    CHECK(File_eof(f));
    CHECK(File_read(f, buf, sizeof(buf)) == 0);
    CHECK(File_seek(f, 1));
    CHECK(File_read(f, buf, 4) == 4);
    CHECK(memcmp(buf, "ello", 4) == 0);
    CHECK(!File_seek(f, -1));
    CHECK(File_close(f));

    f = File_open(p, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_size(f) == 0);
    CHECK(File_write(f, "xy", 2) == 2);
    File_close(f);

    f = File_open(p, FILE_MODE_READ);
    CHECK(f != nullptr);
    CHECK(File_size(f) == 2);
    File_close(f);

    CHECK(File_delete(p));
    CHECK(!File_exists(p));
    CHECK(!File_delete(p));
}

/* Confirms append mode preserves existing bytes and begins writing at EOF. */
static fn test_append() {
    char p[300];
    path_of(p, sizeof(p), "append.bin");

    File *f = File_open(p, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_write(f, "abc", 3) == 3);
    File_close(f);

    f = File_open(p, FILE_MODE_WRITE | FILE_MODE_APPEND | FILE_MODE_CREATE);
    CHECK(f != nullptr);
    CHECK(File_pos(f) == 3);
    CHECK(File_write(f, "de", 2) == 2);
    CHECK(File_size(f) == 5);
    File_close(f);

    f = File_open(p, FILE_MODE_READ);
    CHECK(f != nullptr);
    char buf[8] = { 0 };
    CHECK(File_read(f, buf, sizeof(buf)) == 5);
    CHECK(memcmp(buf, "abcde", 5) == 0);
    File_close(f);
    File_delete(p);
}

/* Exercises invalid paths, missing files, bad lengths, null handles, and safe failures. */
static fn test_errors() {
    CHECK(File_open(nullptr, FILE_MODE_READ) == nullptr);
    char p[300];
    path_of(p, sizeof(p), "missing.bin");
    File_delete(p);
    CHECK(File_open(p, FILE_MODE_READ) == nullptr);

    char q[300];
    path_of(q, sizeof(q), "err.bin");
    File *f = File_open(q, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_write(f, "abc", 3) == 3);
    CHECK(File_write(f, nullptr, 3) == -1);
    CHECK(File_write(f, "x", -1) == -1);
    CHECK(File_write(f, "x", 0) == 0);
    CHECK(File_read(f, nullptr, 1) == -1);
    char tmp[4];
    CHECK(File_read(nullptr, tmp, 1) == -1);
    CHECK(!File_seek(nullptr, 0));
    CHECK(!File_seek(f, -1));
    CHECK(!File_flush(nullptr));
    CHECK(File_close(nullptr) == false);
    File_close(f);
    File_delete(q);
}

/* Initializes memory and a scratch directory, then runs the File owner cases. */
int main() {
    CHECK(Memory_init(0));

    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    snprintf(g_dir, sizeof(g_dir), "%s/vexspoke_file_%d", base, getpid());
    File_delete(g_dir);
    CHECK(File_mkdirs(g_dir));
    if (g_failures) {
        printf("file_test: could not create scratch dir\n");
        return 1;
    }

    test_path_ops();
    test_write_read();
    test_append();
    test_errors();

    if (g_failures == 0) {
        printf("file_test: all assertions held\n");
        return 0;
    }
    printf("file_test: %d FAILURES\n", g_failures);
    return 1;
}
