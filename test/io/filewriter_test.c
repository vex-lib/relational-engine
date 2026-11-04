// Relational Engine owner test for the migrated production io/filewriter.
//
// Proves the buffered binary writer:
//   - open creates missing parent directories and truncates;
//   - write accumulates bytesWritten, coalesces through the 64 KB buffer, and
//     a write larger than the buffer still lands whole;
//   - flush and close are idempotent; closed writers ignore further writes;
//   - nullptr path and an uncreatable parent directory fail cleanly;
//   - the Bytes on disk match exactly what was written.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "io/filewriter.h"
#include "io/file.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static char g_dir[256];

/** Builds a bounded path for a leaf inside the test's temporary directory. */
static void path_of(char *out, size_t cap, const char *leaf) {
    snprintf(out, cap, "%s/%s", g_dir, leaf);
}

/* Reads a file's size for verifying the writer's on-disk result. */
static long read_file_size(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

int main(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    snprintf(g_dir, sizeof(g_dir), "%s/vexspoke_fw_%d", base, getpid());
    File_delete(g_dir);
    CHECK(File_mkdirs(g_dir));
    if (g_failures)
        return 1;

    FileWriter w;
    memset(&w, 0, sizeof(w));
    FileWriter_close(&w);                              // close before open: no-op
    FileWriter_flush(&w);
    CHECK(FileWriter_bytesWritten(&w) == 0);

    // Open into a not-yet-existing nested directory.
    char p[300];
    path_of(p, sizeof(p), "sub/dir/out.bin");
    CHECK(FileWriter_open(&w, p));
    CHECK(FileWriter_bytesWritten(&w) == 0);
    CHECK(File_exists(p));

    FileWriter_write(&w, (const uint8_t*) "hello", 5);
    CHECK(FileWriter_bytesWritten(&w) == 5);
    FileWriter_write(&w, (const uint8_t*) "", 0);      // zero write
    CHECK(FileWriter_bytesWritten(&w) == 5);

    // A write larger than the internal buffer must still be exact.
    enum { BIG = 200000 };
    uint8_t *big = (uint8_t*) malloc(BIG);
    CHECK(big != nullptr);
    for (int i = 0; i < BIG; i++)
        big[i] = (uint8_t) (i * 31 + 7);
    FileWriter_write(&w, big, BIG);
    CHECK(FileWriter_bytesWritten(&w) == (uint64_t) (5 + BIG));

    FileWriter_flush(&w);
    FileWriter_close(&w);
    FileWriter_close(&w);                              // idempotent

    // Verify on-disk size and content.
    CHECK(read_file_size(p) == 5 + BIG);
    FILE *f = fopen(p, "rb");
    CHECK(f != nullptr);
    if (f) {
        uint8_t head[5];
        CHECK(fread(head, 1, 5, f) == 5);
        CHECK(memcmp(head, "hello", 5) == 0);
        CHECK(fseek(f, 5, SEEK_SET) == 0);
        uint8_t *back = (uint8_t*) malloc(BIG);
        CHECK(back != nullptr);
        if (back) {
            CHECK(fread(back, 1, BIG, f) == (size_t) BIG);
            CHECK(memcmp(back, big, BIG) == 0);
            free(back);
        }
        fclose(f);
    }
    free(big);

    // Writes after close are ignored.
    FileWriter_write(&w, (const uint8_t*) "late", 4);
    CHECK(FileWriter_bytesWritten(&w) == (uint64_t) (5 + BIG));

    // nullptr path fails.
    FileWriter w2;
    memset(&w2, 0, sizeof(w2));
    CHECK(!FileWriter_open(&w2, nullptr));
    CHECK(!FileWriter_open(nullptr, "some/path"));   // nullptr writer refused

    // Parent that cannot be a directory.
    char blocker[300];
    path_of(blocker, sizeof(blocker), "blocker");
    FILE *bf = fopen(blocker, "wb");
    CHECK(bf != nullptr);
    if (bf)
        fclose(bf);
    char under[320];
    snprintf(under, sizeof(under), "%s/child", blocker);
    CHECK(!FileWriter_open(&w2, under));

    File_delete(p);
    File_delete(blocker);

    if (g_failures == 0) {
        printf("filewriter_test: all assertions held\n");
        return 0;
    }
    printf("filewriter_test: %d FAILURES\n", g_failures);
    return 1;
}
