// Relational Engine owner test for the migrated production io/logparser.
//
// Proves the binary ANTI-log reader against hand-built files:
//   - a valid 12-byte header ("ANTILOG", version 1, record size 52);
//   - count is (size - header) / 52; parse streams every record with the exact
//     big-endian kind/timestamp/values;
//   - a truncated trailing record is ignored, not read past;
//   - malformed magic, wrong version, short file, and a missing file are all
//     refused (-1 / false);
//   - formatRecord renders "<ms>.<mmm> ms  <name>  v0..v4", honors a null name
//     via the kind table, and reports truncation with -1;
//   - kind names map exactly, with a "kind#N" fallback.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static char g_dir[256];

/* Writes one 32-bit value in the log format's big-endian byte order. */
static void put_be32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}

/* Writes one signed timestamp/value as its 64-bit big-endian representation. */
static void put_be64(FILE *f, int64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++)
        b[i] = (uint8_t)((uint64_t)v >> (56 - 8 * i));
    fwrite(b, 1, 8, f);
}

/* Emits the ANTI-log magic, version, and big-endian record-size header. */
static void put_header(FILE *f, uint8_t version, uint32_t record_size) {
    fwrite("ANTILOG", 1, 7, f);
    uint8_t v = version;
    fwrite(&v, 1, 1, f);
    put_be32(f, record_size);
}

typedef struct Collector {
    int count;
    int kinds[8];
    int64_t v0[8];
    int64_t ts[8];
} Collector;

/* Saves selected callback fields so parsing order and values can be asserted. */
static void collect(void *userdata, int kind, int64_t ts,
                    int64_t v0, int64_t v1, int64_t v2, int64_t v3, int64_t v4) {
    (void) v1; (void) v2; (void) v3; (void) v4;
    Collector *c = (Collector*) userdata;
    if ((*c).count < 8) {
        (*c).kinds[(*c).count] = kind;
        (*c).ts[(*c).count] = ts;
        (*c).v0[(*c).count] = v0;
    }
    (*c).count++;
}

/* Verifies valid records, callback values, counts, and safe handling of a partial trailing record. */
static void test_valid(void) {
    char p[300];
    snprintf(p, sizeof(p), "%s/valid.log", g_dir);
    FILE *f = fopen(p, "wb");
    CHECK(f != nullptr);
    put_header(f, 1, 52);
    put_be32(f, 1);          // kind produce
    put_be64(f, 111);
    put_be64(f, 10); put_be64(f, 11); put_be64(f, 12); put_be64(f, 13); put_be64(f, 14);
    put_be32(f, 2);          // kind present
    put_be64(f, 222);
    put_be64(f, 20); put_be64(f, 21); put_be64(f, 22); put_be64(f, 23); put_be64(f, 24);
    fclose(f);

    CHECK(LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == 2);

    Collector c = { 0 };
    CHECK(LogParser_parse(p, collect, &c) == 2);
    CHECK(c.count == 2);
    CHECK(c.kinds[0] == 1 && c.ts[0] == 111 && c.v0[0] == 10);
    CHECK(c.kinds[1] == 2 && c.ts[1] == 222 && c.v0[1] == 20);

    // A truncated trailing record is ignored by count and parse.
    f = fopen(p, "ab");
    CHECK(f != nullptr);
    fwrite("0123456789", 1, 10, f);
    fclose(f);
    CHECK(LogParser_count(p) == 2);
    Collector c2 = { 0 };
    CHECK(LogParser_parse(p, collect, &c2) == 2);
    CHECK(c2.count == 2);
}

/* Verifies malformed headers, missing/short files, and null parse callbacks are rejected. */
static void test_malformed(void) {
    char p[300];

    // Bad magic.
    snprintf(p, sizeof(p), "%s/badmagic.log", g_dir);
    FILE *f = fopen(p, "wb");
    CHECK(f != nullptr);
    fwrite("NOTALOG", 1, 7, f);
    uint8_t v = 1;
    fwrite(&v, 1, 1, f);
    put_be32(f, 52);
    put_be32(f, 1);
    put_be64(f, 1);
    put_be64(f, 1); put_be64(f, 1); put_be64(f, 1); put_be64(f, 1); put_be64(f, 1);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);
    Collector c = { 0 };
    CHECK(LogParser_parse(p, collect, &c) == -1);

    // Wrong version.
    snprintf(p, sizeof(p), "%s/badver.log", g_dir);
    f = fopen(p, "wb");
    CHECK(f != nullptr);
    put_header(f, 2, 52);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);

    // Wrong record size.
    snprintf(p, sizeof(p), "%s/badrec.log", g_dir);
    f = fopen(p, "wb");
    CHECK(f != nullptr);
    put_header(f, 1, 40);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));

    // Too short to hold a header.
    snprintf(p, sizeof(p), "%s/tiny.log", g_dir);
    f = fopen(p, "wb");
    CHECK(f != nullptr);
    fwrite("ANTI", 1, 4, f);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);

    // Missing file.
    snprintf(p, sizeof(p), "%s/absent.log", g_dir);
    remove(p);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);
    CHECK(LogParser_parse(p, collect, &c) == -1);

    // Null handler.
    CHECK(LogParser_parse(nullptr, nullptr, nullptr) == -1);
}

/* Checks timestamp formatting, kind-name fallback, supplied labels, and truncation reporting. */
static void test_format(void) {
    char out[256];
    int n = LogParser_formatRecord(out, sizeof(out), 1,
                                   1012345000LL, 1000000000LL, nullptr,
                                   1, 2, 3, 4, 5);
    CHECK(n > 0);
    CHECK(strstr(out, "12.345 ms") != nullptr);
    CHECK(strstr(out, "produce") != nullptr);
    CHECK(strstr(out, "1  2  3  4  5") != nullptr);

    // base_ts <= 0 -> 0.000 ms.
    n = LogParser_formatRecord(out, sizeof(out), 2, 5, 0, "custom", 0, 0, 0, 0, 0);
    CHECK(n > 0);
    CHECK(strstr(out, "0.000 ms") != nullptr);
    CHECK(strstr(out, "custom") != nullptr);

    // Truncation reports -1.
    char small[8];
    CHECK(LogParser_formatRecord(small, sizeof(small), 1, 1, 0, "x", 0, 0, 0, 0, 0) == -1);
}

/* Checks known event names and deterministic fallback names for unknown kinds. */
static void test_kind_names(void) {
    CHECK(strcmp(LogParser_kindName(1), "produce") == 0);
    CHECK(strcmp(LogParser_kindName(2), "present") == 0);
    CHECK(strcmp(LogParser_kindName(3), "drop") == 0);
    CHECK(strcmp(LogParser_kindName(10), "keyDown") == 0);
    CHECK(strcmp(LogParser_kindName(33), "touchCancel") == 0);
    CHECK(strcmp(LogParser_kindName(999), "kind#999") == 0);
    CHECK(strcmp(LogParser_kindName(-3), "kind#-3") == 0);
}

/* Creates an isolated temporary directory, runs parser contract cases, and returns their aggregate status. */
int main(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    snprintf(g_dir, sizeof(g_dir), "%s/vexspoke_lp_%d", base, getpid());
    File_delete(g_dir);
    CHECK(File_mkdirs(g_dir));
    if (g_failures)
        return 1;

    test_valid();
    test_malformed();
    test_format();
    test_kind_names();

    if (g_failures == 0) {
        printf("logparser_test: all assertions held\n");
        return 0;
    }
    printf("logparser_test: %d FAILURES\n", g_failures);
    return 1;
}
