// Consolidated automated owner test suite for vex/re (io + nio).
// Usage: re_test [hot_file|findex|log|filewriter|file|logparser|transient_lifetime|mem|mem_exhaustion]

#include "vex.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "io/file/file.h"
#include "io/log/log.h"
#include "nio/mem.h"

static i32 g_failures = 0;
static char g_dir[256];

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Creates a fresh per-suite scratch directory under TMPDIR. */
static bool make_scratch_dir(const char *tag) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    snprintf(g_dir, sizeof(g_dir), "%s/vex_re_%s_%d", base, tag, getpid());
    File_delete(g_dir);
    return File_mkdirs(g_dir);
}

/** Joins the active scratch directory with a relative leaf path. */
static fn path_of(char *out, bytes cap, const char *leaf) {
    snprintf(out, cap, "%s/%s", g_dir, leaf);
}

/** Reads a file's byte length via stdio for on-disk verification. */
static i64 read_file_size(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    i64 n = ftell(f);
    fclose(f);
    return n;
}

/** Proves safe repeated lifecycle calls on the draft HotFileSys no-op. */
static fn suite_hot_file() {
    HotFileSys_pumpEvents();
    HotFileSys_shutdown();
    for (i32 i = 0; i < 2; ++i) {
        HotFileSys_init();
        HotFileSys_pumpEvents();
        HotFileSys_shutdown();
    }
}

/** Proves FIndex insert, update, remove, growth, arena ownership, and null safety. */
static fn suite_findex() {
    FIndex index = FIndex_create(4);
    assert(FIndex_live(&index));
    assert(FIndex_count(&index) == 0);

    u64 offset = 0;
    u64 length = 0;
    assert(!FIndex_find(&index, 1, &offset, &length));
    assert(FIndex_insert(&index, 1, 100, 10));
    assert(FIndex_insert(&index, 2, 200, 20));
    assert(FIndex_count(&index) == 2);
    assert(FIndex_find(&index, 1, &offset, &length) && offset == 100 && length == 10);
    assert(FIndex_find(&index, 2, &offset, &length) && offset == 200 && length == 20);
    assert(FIndex_insert(&index, 1, 111, 11));
    assert(FIndex_find(&index, 1, &offset, &length) && offset == 111 && length == 11);
    assert(FIndex_count(&index) == 2);

    FIndex_remove(&index, 1);
    assert(!FIndex_find(&index, 1, &offset, &length));
    assert(FIndex_count(&index) == 1);

    for (u64 i = 0; i < 200; i++)
        assert(FIndex_insert(&index, i + 1000, i, i + 1));
    for (u64 i = 0; i < 200; i++)
        assert(FIndex_find(&index, i + 1000, &offset, &length) && offset == i && length == i + 1);

    FIndex empty = FIndex_create(0);
    assert(!FIndex_live(&empty));
    assert(FIndex_insert(&empty, 7, 70, 7));
    assert(FIndex_live(&empty));
    assert(FIndex_find(&empty, 7, &offset, &length) && offset == 70 && length == 7);
    FIndex_shutdown(&empty);

    MemoryArena *arena = MemoryArena_create(64u * 1024u * 1024u);
    assert(arena);
    FIndex arenad = FIndex_createArena(2, arena);
    assert(FIndex_live(&arenad));
    assert(FIndex_insert(&arenad, 42, 420, 42));
    assert(FIndex_find(&arenad, 42, &offset, &length) && offset == 420 && length == 42);
    FIndex_shutdown(&arenad);
    MemoryArena_destroy(arena);

    FIndex_shutdown(&index);
    assert(!FIndex_live(&index));
    assert(!FIndex_insert(nullptr, 0, 0, 0));
    assert(!FIndex_find(nullptr, 0, nullptr, nullptr));
    FIndex_remove(nullptr, 0);
    FIndex_shutdown(nullptr);
    assert(FIndex_count(nullptr) == 0);
    assert(!FIndex_live(nullptr));
    Memory_freeAll();
}

/** Proves MPSC Log ring init, append accounting, drain on shutdown, and LogParser round trip. */
static fn suite_log() {
    CHECK(make_scratch_dir("log"));
    if (g_failures)
        return;

    char path[300];
    path_of(path, sizeof(path), "engine.log");
    CHECK(!Log_init(nullptr, path, 8));

    Log log;
    CHECK(!Log_init(&log, nullptr, 8));
    CHECK(Log_init(&log, path, 8));
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

    Log_setActive(&log, false);
    CHECK(!Log_isActive(&log));
    Log_append(&log, 9, 1, 1, 1, 1, 1);
    CHECK(Log_appended(&log) == 3);

    Log_setActive(&log, true);
    u64 before = Log_appended(&log) + Log_dropped(&log);
    enum { BURST = 2000 };
    for (i32 i = 0; i < BURST; i++)
        Log_append(&log, 4, i, 0, 0, 0, 0);
    u64 after = Log_appended(&log) + Log_dropped(&log);
    CHECK(after - before == BURST);

    Log dead;
    memset(&dead, 0, sizeof(dead));
    Log_append(&dead, 1, 0, 0, 0, 0, 0);
    Log_append(nullptr, 1, 0, 0, 0, 0, 0);

    Log_shutdown(&log);
    CHECK(!Log_isEnabled(&log));
    CHECK(Log_written(&log) == Log_appended(&log));
    Log_shutdown(&log);
    Log_shutdown(nullptr);

    CHECK(LogParser_isLogFile(path));
    i64 n = LogParser_count(path);
    CHECK(n >= 0);
    CHECK((u64) n == Log_written(&log));

    Log again;
    CHECK(Log_init(&again, path, 4));
    CHECK(Log_isEnabled(&again));
    Log_shutdown(&again);

    Log tiny;
    char path2[320];
    path_of(path2, sizeof(path2), "tiny.log");
    CHECK(Log_init(&tiny, path2, 0));
    Log_append(&tiny, 7, 1, 2, 3, 4, 5);
    CHECK(Log_appended(&tiny) == 1);
    Log_shutdown(&tiny);
    CHECK(LogParser_count(path2) == 1);
}

/** Proves FileWriter parent creation, buffered and large writes, idempotent close, and error paths. */
static fn suite_filewriter() {
    CHECK(make_scratch_dir("fw"));
    if (g_failures)
        return;

    FileWriter w;
    memset(&w, 0, sizeof(w));
    FileWriter_close(&w);
    FileWriter_flush(&w);
    CHECK(FileWriter_bytesWritten(&w) == 0);

    char p[300];
    path_of(p, sizeof(p), "sub/dir/out.bin");
    CHECK(FileWriter_open(&w, p));
    CHECK(FileWriter_bytesWritten(&w) == 0);
    CHECK(File_exists(p));

    FileWriter_write(&w, (const u8*) "hello", 5);
    CHECK(FileWriter_bytesWritten(&w) == 5);
    FileWriter_write(&w, (const u8*) "", 0);
    CHECK(FileWriter_bytesWritten(&w) == 5);

    enum { BIG = 200000 };
    u8 *big = (u8*) malloc(BIG);
    CHECK(big != nullptr);
    for (i32 i = 0; i < BIG; i++)
        big[i] = (u8) (i * 31 + 7);
    FileWriter_write(&w, big, BIG);
    CHECK(FileWriter_bytesWritten(&w) == (u64) (5 + BIG));
    FileWriter_flush(&w);
    FileWriter_close(&w);
    FileWriter_close(&w);
    CHECK(read_file_size(p) == 5 + BIG);

    FILE *f = fopen(p, "rb");
    CHECK(f != nullptr);
    if (f) {
        u8 head[5];
        CHECK(fread(head, 1, 5, f) == 5);
        CHECK(memcmp(head, "hello", 5) == 0);
        CHECK(fseek(f, 5, SEEK_SET) == 0);
        u8 *back = (u8*) malloc(BIG);
        CHECK(back != nullptr);
        if (back) {
            CHECK(fread(back, 1, BIG, f) == (bytes) BIG);
            CHECK(memcmp(back, big, BIG) == 0);
            free(back);
        }
        fclose(f);
    }
    free(big);

    FileWriter_write(&w, (const u8*) "late", 4);
    CHECK(FileWriter_bytesWritten(&w) == (u64) (5 + BIG));

    FileWriter w2;
    memset(&w2, 0, sizeof(w2));
    CHECK(!FileWriter_open(&w2, nullptr));
    CHECK(!FileWriter_open(nullptr, "some/path"));

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
}

/** Checks directory/file operations, nested creation, idempotence, and null paths. */
static fn file_path_ops() {
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

/** Verifies file creation, write/read/seek/EOF, truncate, and deletion behavior. */
static fn file_write_read() {
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

/** Confirms append mode preserves existing bytes and begins writing at EOF. */
static fn file_append() {
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

/** Exercises invalid paths, missing files, bad lengths, null handles, and safe failures. */
static fn file_errors() {
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

/** Runs all File owner cases in a scratch directory. */
static fn suite_file() {
    CHECK(Memory_init(0));
    CHECK(make_scratch_dir("file"));
    if (g_failures)
        return;
    file_path_ops();
    file_write_read();
    file_append();
    file_errors();
}

/** Writes one 32-bit value in the ANTI-log big-endian byte order. */
static fn put_be32(FILE *f, u32 v) {
    u8 b[4] = { (u8) (v >> 24), (u8) (v >> 16), (u8) (v >> 8), (u8) v };
    fwrite(b, 1, 4, f);
}

/** Writes one 64-bit signed timestamp or value in big-endian byte order. */
static fn put_be64(FILE *f, i64 v) {
    u8 b[8];
    for (i32 i = 0; i < 8; i++)
        b[i] = (u8) ((u64) v >> (56 - 8 * i));
    fwrite(b, 1, 8, f);
}

/** Emits the ANTI-log magic, version, and big-endian record-size header. */
static fn put_header(FILE *f, u8 version, u32 record_size) {
    fwrite("ANTILOG", 1, 7, f);
    u8 v = version;
    fwrite(&v, 1, 1, f);
    put_be32(f, record_size);
}

typedef struct Collector {
    i32 count;
    i32 kinds[8];
    i64 v0[8];
    i64 ts[8];
} Collector;

;;GETTER
static inline i32 Collector_count(const Collector *c) { return (*c).count; }
;;SETTER
static inline fn Collector_setCount(Collector *c, i32 count) { (*c).count = count; }
;;SETTER
static inline fn Collector_setKindAt(Collector *c, i32 idx, i32 kind) { (*c).kinds[idx] = kind; }
;;SETTER
static inline fn Collector_setV0At(Collector *c, i32 idx, i64 v0) { (*c).v0[idx] = v0; }
;;SETTER
static inline fn Collector_setTsAt(Collector *c, i32 idx, i64 ts) { (*c).ts[idx] = ts; }

/** Saves parsed callback fields so record order and values can be asserted. */
static fn collect_record(any userdata, i32 kind, i64 ts,
                         i64 v0, i64 v1, i64 v2, i64 v3, i64 v4) {
    (discard) v1;
    (discard) v2;
    (discard) v3;
    (discard) v4;
    Collector *c = (Collector*) userdata;
    i32 count = Collector_count(c);
    if (count < 8) {
        Collector_setKindAt(c, count, kind);
        Collector_setTsAt(c, count, ts);
        Collector_setV0At(c, count, v0);
    }
    Collector_setCount(c, count + 1);
}

/** Verifies valid ANTI-log parsing, callback values, and partial trailing record handling. */
static fn logparser_valid() {
    char p[300];
    path_of(p, sizeof(p), "valid.log");
    FILE *f = fopen(p, "wb");
    CHECK(f != nullptr);
    put_header(f, 1, 52);
    put_be32(f, 1);
    put_be64(f, 111);
    put_be64(f, 10);
    put_be64(f, 11);
    put_be64(f, 12);
    put_be64(f, 13);
    put_be64(f, 14);
    put_be32(f, 2);
    put_be64(f, 222);
    put_be64(f, 20);
    put_be64(f, 21);
    put_be64(f, 22);
    put_be64(f, 23);
    put_be64(f, 24);
    fclose(f);

    CHECK(LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == 2);
    Collector c = { 0 };
    CHECK(LogParser_parse(p, collect_record, &c) == 2);
    CHECK(c.count == 2);
    CHECK(c.kinds[0] == 1 && c.ts[0] == 111 && c.v0[0] == 10);
    CHECK(c.kinds[1] == 2 && c.ts[1] == 222 && c.v0[1] == 20);

    f = fopen(p, "ab");
    CHECK(f != nullptr);
    fwrite("0123456789", 1, 10, f);
    fclose(f);
    CHECK(LogParser_count(p) == 2);
    Collector c2 = { 0 };
    CHECK(LogParser_parse(p, collect_record, &c2) == 2);
    CHECK(c2.count == 2);
}

/** Verifies malformed ANTI-log headers, truncated files, and null callbacks are refused. */
static fn logparser_malformed() {
    char p[300];
    path_of(p, sizeof(p), "badmagic.log");
    FILE *f = fopen(p, "wb");
    CHECK(f != nullptr);
    fwrite("NOTALOG", 1, 7, f);
    u8 v = 1;
    fwrite(&v, 1, 1, f);
    put_be32(f, 52);
    put_be32(f, 1);
    put_be64(f, 1);
    put_be64(f, 1);
    put_be64(f, 1);
    put_be64(f, 1);
    put_be64(f, 1);
    put_be64(f, 1);
    fclose(f);

    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);
    Collector c = { 0 };
    CHECK(LogParser_parse(p, collect_record, &c) == -1);

    path_of(p, sizeof(p), "badver.log");
    f = fopen(p, "wb");
    CHECK(f != nullptr);
    put_header(f, 2, 52);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);

    path_of(p, sizeof(p), "badrec.log");
    f = fopen(p, "wb");
    CHECK(f != nullptr);
    put_header(f, 1, 40);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));

    path_of(p, sizeof(p), "tiny.log");
    f = fopen(p, "wb");
    CHECK(f != nullptr);
    fwrite("ANTI", 1, 4, f);
    fclose(f);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);

    path_of(p, sizeof(p), "absent.log");
    remove(p);
    CHECK(!LogParser_isLogFile(p));
    CHECK(LogParser_count(p) == -1);
    CHECK(LogParser_parse(p, collect_record, &c) == -1);
    CHECK(LogParser_parse(nullptr, nullptr, nullptr) == -1);
}

/** Verifies timestamp formatting, kind-name lookup, custom labels, and truncation reporting. */
static fn logparser_format_and_kinds() {
    char out[256];
    i32 n = LogParser_formatRecord(out, sizeof(out), 1,
                                   1012345000LL, 1000000000LL, nullptr,
                                   1, 2, 3, 4, 5);
    CHECK(n > 0);
    CHECK(strstr(out, "12.345 ms") != nullptr);
    CHECK(strstr(out, "produce") != nullptr);
    CHECK(strstr(out, "1  2  3  4  5") != nullptr);

    n = LogParser_formatRecord(out, sizeof(out), 2, 5, 0, "custom", 0, 0, 0, 0, 0);
    CHECK(n > 0);
    CHECK(strstr(out, "0.000 ms") != nullptr);
    CHECK(strstr(out, "custom") != nullptr);

    char small[8];
    CHECK(LogParser_formatRecord(small, sizeof(small), 1, 1, 0, "x", 0, 0, 0, 0, 0) == -1);

    CHECK(strcmp(LogParser_kindName(1), "produce") == 0);
    CHECK(strcmp(LogParser_kindName(2), "present") == 0);
    CHECK(strcmp(LogParser_kindName(3), "drop") == 0);
    CHECK(strcmp(LogParser_kindName(10), "keyDown") == 0);
    CHECK(strcmp(LogParser_kindName(33), "touchCancel") == 0);
    CHECK(strcmp(LogParser_kindName(999), "kind#999") == 0);
    CHECK(strcmp(LogParser_kindName(-3), "kind#-3") == 0);
}

/** Runs all LogParser owner cases in an isolated scratch directory. */
static fn suite_logparser() {
    CHECK(make_scratch_dir("lp"));
    if (g_failures)
        return;
    logparser_valid();
    logparser_malformed();
    logparser_format_and_kinds();
}

typedef struct TestLabel {
    char *text;
    bool ownsText;
} TestLabel;

;;GETTER
static inline char *TestLabel_text(const TestLabel *lbl) { return (*lbl).text; }
;;SETTER
static inline fn TestLabel_setTextField(TestLabel *lbl, char *text) { (*lbl).text = text; }
;;GETTER
static inline bool TestLabel_ownsText(const TestLabel *lbl) { return (*lbl).ownsText; }
;;SETTER
static inline fn TestLabel_setOwnsText(TestLabel *lbl, bool owns) { (*lbl).ownsText = owns; }

/** Allocates and initializes a permanent fallback label object. */
static TestLabel *TestLabel_0() {
    TestLabel *lbl = (TestLabel*) Memory_alloc(100, Re_arena(), sizeof(TestLabel));
    if (!lbl)
        return nullptr;
    TestLabel_setTextField(lbl, nullptr);
    TestLabel_setOwnsText(lbl, false);
    return lbl;
}

/** Replaces label text with an owned permanent copy, releasing prior owned text. */
static fn TestLabel_setText(TestLabel *lbl, const char *text) {
    if (!lbl)
        return;
    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl))
        Memory_free(TestLabel_text(lbl));
    if (text) {
        bytes len = strlen(text) + 1;
        char *copy = (char*) Memory_alloc(101, Re_arena(), len);
        TestLabel_setTextField(lbl, copy);
        if (copy)
            memcpy(copy, text, len);
        TestLabel_setOwnsText(lbl, true);
    } else {
        TestLabel_setTextField(lbl, nullptr);
        TestLabel_setOwnsText(lbl, false);
    }
}

/** Replaces label text with a borrowed pointer and aborts on transient escape under DEBUG_BORROW_CHECK. */
static fn TestLabel_setTextBorrowed(TestLabel *lbl, const char *text) {
    if (!lbl)
        return;
#if defined(DEBUG_BORROW_CHECK)
    if (Transient_contains(text) && !Transient_contains(lbl)) {
        fprintf(stderr, "[LIFETIME ESCAPE] TestLabel_setTextBorrowed: escape abort\n");
        abort();
    }
#endif
    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl))
        Memory_free(TestLabel_text(lbl));
    TestLabel_setTextField(lbl, (char*) text);
    TestLabel_setOwnsText(lbl, false);
}

/** Releases owned text and the fallback label allocation. */
static fn TestLabel_free(TestLabel *lbl) {
    if (!lbl)
        return;
    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl))
        Memory_free(TestLabel_text(lbl));
    Memory_free(lbl);
}

/** Verifies transient lifetime classification, auto-promotion, poisoning, and escape death test. */
static fn suite_transient_lifetime() {
    assert(Memory_init(0));
    const char *lit = "literal_constant_string";
    assert(Memory_getLifetime(lit) == MEMORY_LIFETIME_UNKNOWN);

    char stackBuf[64];
    strcpy(stackBuf, "stack_buffer_array");
    assert(Memory_getLifetime(stackBuf) == MEMORY_LIFETIME_UNKNOWN);

    any perm = Memory_alloc(1, Re_arena(), 128);
    assert(perm != nullptr);
    assert(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);

    any trans = Transient_alloc(2, 128);
    assert(trans != nullptr);
    assert(Memory_getLifetime(trans) == MEMORY_LIFETIME_TRANSIENT);

    Memory_free((any) lit);
    Memory_free((any) stackBuf);
    Memory_free(trans);

    char *transStr = (char*) Transient_alloc(3, 32);
    strcpy(transStr, "transient_string_123");
    TestLabel *lbl = TestLabel_0();
    assert(lbl != nullptr);
    TestLabel_setText(lbl, transStr);
    assert(TestLabel_ownsText(lbl) == true);
    assert(Memory_getLifetime(TestLabel_text(lbl)) == MEMORY_LIFETIME_PERMANENT);

    Transient_reset();
    assert(strcmp(TestLabel_text(lbl), "transient_string_123") == 0);

    const char *staticStr = "constant_borrowed_literal";
    TestLabel_setTextBorrowed(lbl, staticStr);
    assert(TestLabel_ownsText(lbl) == false);
    assert(TestLabel_text(lbl) == staticStr);

    TestLabel_setText(lbl, "next_owned_value");
    assert(TestLabel_ownsText(lbl) == true);
    assert(strcmp(TestLabel_text(lbl), "next_owned_value") == 0);
    TestLabel_free(lbl);

    bytes allocBytes = 65536;
    u8 *p = (u8*) Transient_alloc(4, allocBytes);
    assert(p != nullptr);
    memset(p, 0x55, allocBytes);
    assert((*p) == 0x55);
    Transient_reset();
#if defined(DEBUG_BORROW_CHECK)
    assert((*p) == 0xDD);
    assert(*(p + allocBytes - 1) == 0xDD);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);
        char *escapeStr = (char*) Transient_alloc(5, 32);
        strcpy(escapeStr, "escaped_transient");
        TestLabel *escapeLbl = TestLabel_0();
        TestLabel_setTextBorrowed(escapeLbl, escapeStr);
        exit(0);
    }
    i32 status = 0;
    waitpid(pid, &status, 0);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGABRT);
#endif
    Memory_free(perm);
    Memory_freeAll();
}

enum {
    TEST_TYPE_A = 1u,
    TEST_TYPE_B = 2u,
    TEST_TYPE_C = 3u,
};

static _Alignas(16) u8 g_foreign[64];

typedef struct Over32 {
    _Alignas(32) u8 bytes[32];
} Over32;

/** Verifies Memory/MemoryArena alignment, overflow refusal, foreign pointers, realloc, and transient generation. */
static fn suite_mem() {
    CHECK(Memory_init(0));
    CHECK(Memory_length(nullptr) == 0);
    CHECK(Memory_type(nullptr) == 0);
    Memory_free(nullptr);
    CHECK(Memory_realloc(nullptr, 0) == nullptr);
    CHECK(Memory_alloc(TEST_TYPE_A, nullptr, 16) == nullptr);

    any p = Memory_alloc(TEST_TYPE_A, Re_arena(), 16);
    CHECK(p != nullptr);
    CHECK(((pointer) p & 31) == 0);
    CHECK(Memory_length(p) == 16);
    CHECK(Memory_type(p) == TEST_TYPE_A);
    memset(p, 0xA5, 16);

    any p_next = Memory_alloc(TEST_TYPE_B, Re_arena(), 16);
    CHECK(p_next != nullptr);
    CHECK(((pointer) p_next & 31) == 0);
    CHECK(_Alignof(Over32) == 32);
    CHECK(sizeof(Over32) == 32);

    any over_slab = Memory_alloc(TEST_TYPE_B, Re_arena(), sizeof(Over32));
    CHECK(over_slab != nullptr);
    CHECK(((pointer) over_slab & 31) == 0);

    for (i32 i = 0; i < 3; i++) {
        any over_bump = Memory_alloc(TEST_TYPE_B, Re_arena(), 5000u + (bytes) i * 16u);
        CHECK(over_bump != nullptr);
        CHECK(((pointer) over_bump & 31) == 0);
    }

    any zero = Memory_alloc(TEST_TYPE_C, Re_arena(), 0);
    CHECK(zero != nullptr);
    CHECK(Memory_length(zero) == 0);
    CHECK(Memory_alloc(TEST_TYPE_A, Re_arena(), (bytes) UINT32_MAX + 1u) == nullptr);
    CHECK(Memory_alloc(TEST_TYPE_A, Re_arena(), (bytes) -1) == nullptr);

    CHECK(Memory_length(g_foreign) == 0);
    CHECK(Memory_type(g_foreign) == 0);
    Memory_free(g_foreign);
    CHECK(Memory_similar(p, g_foreign) == false);
    CHECK(Memory_similar(g_foreign, p) == false);
    CHECK(Memory_similar(p, nullptr) == false);

    any p2 = Memory_alloc(TEST_TYPE_A, Re_arena(), 64);
    CHECK(p2 != nullptr);
    CHECK(Memory_similar(p, p2));
    any other = Memory_alloc(TEST_TYPE_B, Re_arena(), 16);
    CHECK(Memory_similar(p, other) == false);
    CHECK(Memory_similar(p, p) == true);

    any havoc = Memory_realloc(p, (bytes) -1);
    CHECK(havoc == nullptr);
    CHECK(Memory_length(p) == 16);
    for (bytes i = 0; i < 16; i++)
        CHECK(((u8*) p)[i] == 0xA5);

    any grown = Memory_realloc(p, 128);
    CHECK(grown != nullptr);
    CHECK(grown != p);
    CHECK(Memory_length(grown) == 128);
    CHECK(Memory_type(grown) == TEST_TYPE_A);
    for (bytes i = 0; i < 16; i++)
        CHECK(((u8*) grown)[i] == 0xA5);
    p = grown;
    (discard) p;
    Memory_freeAll();

    for (i32 i = 0; i < 3; i++)
        CHECK(Memory_alloc(0xABCDu, Re_arena(), 8) != nullptr);
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 3);
    CHECK(Memory_findAll(TEST_TYPE_A, nullptr, 0) == 0);
    any found[8] = { 0 };
    CHECK(Memory_findAll(0xABCDu, found, 1) == 3);
    CHECK(found[0] != nullptr);
    Memory_free(found[0]);
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);
    CHECK(Memory_length(found[0]) == 0);
    Memory_free(found[0]);
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);
    Memory_freeAll();
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 0);

    MemoryArena *b = MemoryArena_create(64u * 1024u * 1024u);
    CHECK(b != nullptr);
    CHECK(MemoryArena_capacity(b) == 64u * 1024u * 1024u);
    any bp = MemoryArena_alloc(b, 0xB0B0u, 32);
    CHECK(bp != nullptr);
    CHECK(Memory_type(bp) == 0xB0B0u);
    CHECK(MemoryArena_activeBytes(b) >= 32);
    CHECK(Memory_findAll(0xB0B0u, nullptr, 0) == 0);
    CHECK(MemoryArena_findAll(b, 0xB0B0u, nullptr, 0) == 1);
    CHECK(MemoryArena_alloc(nullptr, 0, 8) == nullptr);
    CHECK(MemoryArena_realloc(b, bp, 96) != nullptr);
    MemoryArena_freeAll(b);
    CHECK(MemoryArena_activeBytes(b) == 0);
    MemoryArena_destroy(b);
    MemoryArena_destroy(Memory_defaultArena());
    CHECK(Memory_alloc(TEST_TYPE_A, Re_arena(), 8) != nullptr);

    any perm = Memory_alloc(TEST_TYPE_A, Re_arena(), 8);
    CHECK(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);
    CHECK(Memory_getLifetime(nullptr) == MEMORY_LIFETIME_UNKNOWN);
    CHECK(Memory_getLifetime(g_foreign) == MEMORY_LIFETIME_UNKNOWN);

    pid_t tpid = fork();
    if (tpid == 0) {
        g_failures = 0;
        CHECK(!Memory_initTransient(SIZE_MAX));
        CHECK(!Memory_initTransient(SIZE_MAX - 1u));
        CHECK(Memory_initTransient(4096));
        any tp = Transient_alloc(TEST_TYPE_A, 16);
        CHECK(tp != nullptr);
        CHECK(((pointer) tp & 31) == 0);
        CHECK(Transient_contains(tp));
        CHECK(Memory_length(tp) == 16);
        CHECK(Memory_getLifetime(tp) == MEMORY_LIFETIME_TRANSIENT);
        Memory_free(tp);
        CHECK(Transient_contains(tp));
        CHECK(Transient_alloc(TEST_TYPE_A, 4096u + 64u) == nullptr);
        u32 gen = Transient_getGeneration();
        Transient_reset();
        CHECK(Transient_getGeneration() == gen + 1);
        CHECK(!Transient_contains(tp));
        u8 *before = Transient_alloc(TEST_TYPE_A, 16);
        CHECK(before != nullptr);
        memset(before, 0xA5, 16);
        gen = Transient_getGeneration();
        CHECK(Transient_alloc(TEST_TYPE_A, SIZE_MAX) == nullptr);
        CHECK(Transient_alloc(TEST_TYPE_A, SIZE_MAX - 15u) == nullptr);
        CHECK(Transient_alloc(TEST_TYPE_A, (bytes) UINT32_MAX + 1u) == nullptr);
        CHECK(Transient_alloc(TEST_TYPE_A, UINT32_MAX) == nullptr);
        CHECK(Memory_length(before) == 16 && before[0] == 0xA5);
        CHECK(Transient_getGeneration() == gen);
        u8 *after = Transient_alloc(TEST_TYPE_A, 16);
        CHECK(after == before + MEMORY_HEADER_SIZE + 32);
        Transient_reset();
        _exit(g_failures == 0 ? 0 : 1);
    }
    CHECK(tpid > 0);
    i32 tstatus = 0;
    CHECK(waitpid(tpid, &tstatus, 0) == tpid);
    CHECK(WIFEXITED(tstatus) && WEXITSTATUS(tstatus) == 0);

    pid_t pid = fork();
    if (pid == 0) {
        Memory_free((any) (pointer) 0x1000);
        volatile bytes len = Memory_length((any) (pointer) 0x1000);
        _exit(len == 0 ? 0 : 1);
    }
    CHECK(pid > 0);
    i32 status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    Memory_freeAll();
}

enum {
    MIB = 1024 * 1024,
    SLAB_BYTES = 30 * MIB,
    BUMP_BYTES = 4096,
    SMALL_PAYLOAD = 64 - MEMORY_HEADER_SIZE,
    SMALL_SLOTS = 32768,
    SPILL_SLOTS = BUMP_BYTES / 64,
    MODEL_SLOTS = 64,
    MODEL_STEPS = 10000,
    WORKERS = 4,
    WORKER_STEPS = 4000
};

static const u64 EX_TYPE_A = UINT64_C(0x1200000000000041);
static const u64 EX_TYPE_B = UINT64_C(0x1300000000000041);
static bytes g_heapCalls;
static i32 g_failAfter = -1;

/** Injects a failure at a chosen allocation stage; otherwise delegates to libc. */
static bool failHeap() {
    g_heapCalls++;
    if (g_failAfter < 0)
        return false;
    if (g_failAfter == 0) {
        g_failAfter = -1;
        return true;
    }
    g_failAfter--;
    return false;
}

/** Fault-injecting malloc seam for mem.o when compiled with MEM_TEST_FAULTS. */
any MemTest_malloc(bytes size) { return failHeap() ? nullptr : malloc(size); }

/** Fault-injecting calloc seam for mem.o when compiled with MEM_TEST_FAULTS. */
any MemTest_calloc(bytes count, bytes elementBytes) { return failHeap() ? nullptr : calloc(count, elementBytes); }

/** Fault-injecting realloc seam for mem.o when compiled with MEM_TEST_FAULTS. */
any MemTest_realloc(any ptr, bytes size) { return failHeap() ? nullptr : realloc(ptr, size); }

/** Asserts every byte in a range matches the expected value. */
static fn checkBytes(const u8 *ptr, bytes length, u8 value) {
    for (bytes i = 0; i < length; i++)
        assert(ptr[i] == value);
}

/** Rejects partial arena construction and registry growth while preserving live owners. */
static fn ex_construction() {
#if defined(MEM_TEST_FAULTS)
    for (i32 stage = 0; stage < 2; stage++) {
        g_failAfter = stage;
        assert(MemoryArena_create(SLAB_BYTES + BUMP_BYTES) == nullptr);
    }
#endif
    g_failAfter = -1;
    assert(MemoryArena_create(1) == nullptr);

    MemoryArena *owners[8];
    for (bytes i = 0; i < 7; i++) {
        owners[i] = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
        assert(owners[i] != nullptr);
    }
#if defined(MEM_TEST_FAULTS)
    g_failAfter = 2;
    assert(MemoryArena_create(SLAB_BYTES + BUMP_BYTES) == nullptr);
    g_failAfter = -1;
#endif
    owners[7] = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(owners[7] != nullptr);

    for (bytes i = 0; i < 8; i++) {
        u8 *p = MemoryArena_alloc(owners[i], EX_TYPE_A, 32);
        assert(p != nullptr && Memory_type(p) == EX_TYPE_A);
        MemoryArena_destroy(owners[i]);
    }
}

/** Exhausts slab and spill storage, verifies no heap fallback, preserves bytes, and reclaims safely. */
static fn ex_exhaustion() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(a != nullptr);
    assert(MemoryArena_exhaustionCount(a) == 0);

    u8 *blocks[SMALL_SLOTS + SPILL_SLOTS];
    bytes calls = g_heapCalls;
    for (bytes i = 0; i < SMALL_SLOTS + SPILL_SLOTS; i++) {
        blocks[i] = MemoryArena_alloc(a, EX_TYPE_A, SMALL_PAYLOAD);
        assert(blocks[i] != nullptr);
        assert(((pointer) blocks[i] & 15u) == 0);
        memset(blocks[i], (u8) i, SMALL_PAYLOAD);
    }
    assert(MemoryArena_activeBytes(a) == SMALL_SLOTS * 64u + BUMP_BYTES);

    u64 rejects = MemoryArena_exhaustionCount(a);
    for (bytes i = 0; i < 32; i++)
        assert(MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == rejects + 32);
    assert(g_heapCalls == calls);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == SMALL_SLOTS + SPILL_SLOTS);

    any found[3] = { nullptr, (any) blocks[0], (any) blocks[1] };
    assert(MemoryArena_findAll(a, EX_TYPE_A, found, 1) == SMALL_SLOTS + SPILL_SLOTS);
    assert(found[1] == blocks[0] && found[2] == blocks[1]);

    any grown = MemoryArena_realloc(a, blocks[0], 128);
    assert(grown != nullptr);
    Memory_free(grown);
    blocks[0] = nullptr;

    for (bytes i = 1; i < SMALL_SLOTS + SPILL_SLOTS; i++)
        checkBytes(blocks[i], SMALL_PAYLOAD, (u8) i);

    u8 *spill = blocks[SMALL_SLOTS];
    MemoryArena_free(a, spill);
    MemoryArena_free(a, spill);
    assert(Memory_length(spill) == 0);

    u8 *reused = MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD);
    assert(reused != nullptr && reused != spill);

    u8 *reclaimed = MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD);
    assert(reclaimed == spill);

    u64 beforeReject = MemoryArena_exhaustionCount(a);
    assert(MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == beforeReject + 1);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == SMALL_SLOTS + SPILL_SLOTS - 2);

    MemoryArena_freeAll(a);
    assert(MemoryArena_activeBytes(a) == 0);
    assert(MemoryArena_exhaustionCount(a) == 0);
    assert(Memory_type(blocks[SMALL_SLOTS + 1]) == 0);
    assert(Memory_length(blocks[SMALL_SLOTS + 1]) == 0);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_alloc(a, EX_TYPE_A, SMALL_PAYLOAD) != nullptr);
    MemoryArena_destroy(a);
}

/** Exercises exact bump capacity, failed realloc, shrink, and cross-arena owner transfer. */
static fn ex_boundaries() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    MemoryArena *b = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(a && b);

    u8 *p = MemoryArena_alloc(a, EX_TYPE_A, BUMP_BYTES - MEMORY_HEADER_SIZE);
    assert(p != nullptr);
    MemoryArena_free(a, p);
    p = MemoryArena_alloc(a, EX_TYPE_A, BUMP_BYTES + 1);
    assert(p == nullptr);
    MemoryArena_destroy(a);

    a = MemoryArena_create(SLAB_BYTES + BUMP_BYTES + MEMORY_HEADER_SIZE);
    assert(a);
    p = MemoryArena_alloc(a, EX_TYPE_A, BUMP_BYTES);
    assert(p && Memory_length(p) == BUMP_BYTES);
    memset(p, 0xA5, BUMP_BYTES);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 1);
    assert(MemoryArena_realloc(a, p, BUMP_BYTES) == nullptr);
    checkBytes(p, BUMP_BYTES, 0xA5);
    assert(Memory_type(p) == EX_TYPE_A && Memory_length(p) == BUMP_BYTES);

    u8 *q = MemoryArena_realloc(b, p, 31);
    assert(q && Memory_type(q) == EX_TYPE_A && Memory_length(q) == 31);
    checkBytes(q, 31, 0xA5);
    assert(Memory_type(p) == 0);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_findAll(b, EX_TYPE_A, nullptr, 0) == 1);

    MemoryArena_free(a, q);
    checkBytes(q, 31, 0xA5);
    assert(Memory_length(q) == 31);
    MemoryArena_destroy(a);
    MemoryArena_destroy(b);
}

/** Exhausts every physical slab size class independently, then verifies free and reuse. */
static fn ex_allClasses() {
    const bytes payloads[] = {
        64 - MEMORY_HEADER_SIZE, 128 - MEMORY_HEADER_SIZE,
        256 - MEMORY_HEADER_SIZE, 512 - MEMORY_HEADER_SIZE,
        1024 - MEMORY_HEADER_SIZE, 2048 - MEMORY_HEADER_SIZE,
        4096 - MEMORY_HEADER_SIZE
    };
    const bytes capacities[] = { 32768, 32768, 16384, 8192, 4096, 2048, 2048 };

    MemoryArena *a = MemoryArena_create(SLAB_BYTES);
    assert(a);

    for (bytes c = 0; c < sizeof(payloads) / sizeof(payloads[0]); c++) {
        bytes calls = g_heapCalls;
        u8 *last = nullptr;
        for (bytes i = 0; i < capacities[c]; i++) {
            last = MemoryArena_alloc(a, EX_TYPE_A, payloads[c]);
            assert(last && Memory_length(last) == payloads[c]);
            memset(last, 0x5A, payloads[c]);
        }
        assert(MemoryArena_alloc(a, EX_TYPE_A, payloads[c]) == nullptr);
        assert(MemoryArena_realloc(a, last, SIZE_MAX) == nullptr);
        checkBytes(last, payloads[c], 0x5A);

        Memory_free(last);
        u8 *replacement = MemoryArena_alloc(a, EX_TYPE_B, payloads[c]);
        assert(replacement == last && Memory_type(replacement) == EX_TYPE_B);
        assert(MemoryArena_alloc(a, EX_TYPE_A, payloads[c]) == nullptr);
        assert(g_heapCalls == calls);

        MemoryArena_freeAll(a);
        assert(Memory_length(last) == 0);
        assert(MemoryArena_activeBytes(a) == 0);
    }
    MemoryArena_destroy(a);
}

/** Rejects misaligned pointers and tampered headers while preserving unrelated blocks. */
static fn ex_hostile() {
    MemoryArena *a = MemoryArena_create(SLAB_BYTES);
    assert(a);
    u8 *p = MemoryArena_alloc(a, EX_TYPE_A, 48);
    u8 *other = MemoryArena_alloc(a, EX_TYPE_B, 48);
    assert(p && other);
    memset(p, 0, 48);
    memset(other, 0xC3, 48);
    assert(!Memory_similar(p, other));
    assert(MemoryArena_alloc(a, EX_TYPE_A, SIZE_MAX) == nullptr);
    assert(MemoryArena_alloc(a, EX_TYPE_A, (bytes) UINT32_MAX + 1u) == nullptr);
    assert(MemoryArena_alloc(a, EX_TYPE_A, UINT32_MAX) == nullptr);

    for (bytes offset = 1; offset < 48; offset++) {
        assert(Memory_type(p + offset) == 0);
        assert(Memory_length(p + offset) == 0);
        assert(Memory_realloc(p + offset, 32) == nullptr);
        assert(MemoryArena_realloc(a, p + offset, 32) == nullptr);
        Memory_free(p + offset);
        MemoryArena_free(a, p + offset);
    }

    MemoryHeader *header = (MemoryHeader*) (p - MEMORY_HEADER_SIZE);
    MemoryHeader saved = *header;
    MemoryHeader tampered = saved;
    tampered.hash ^= 2u;
    *header = tampered;
    assert(Memory_type(p) == 0 && Memory_length(p) == 0);
    assert(MemoryArena_realloc(a, p, 32) == nullptr);
    Memory_free(p);
    *header = saved;

    tampered = saved;
    tampered.typeId ^= UINT64_C(0x100000000);
    *header = tampered;
    assert(Memory_type(p) == 0);
    *header = saved;

    tampered = saved;
    tampered.length++;
    *header = tampered;
    assert(Memory_length(p) == 0);
    *header = saved;

    assert(Memory_type(p) == EX_TYPE_A && Memory_length(p) == 48);
    checkBytes(other, 48, 0xC3);
    MemoryArena_destroy(a);
}

/** Runs seeded mixed-size alloc/realloc/free histories asserting bytes, type, and length. */
static fn ex_model() {
    const bytes sizes[] = {
        0, 1, 15, 16, 47, 48, 49, 111, 112, 113,
        239, 240, 241, 495, 496, 497, 1007, 1008, 1009,
        2031, 2032, 2033, 4079, 4080, 4081, 8192
    };
    u8 *ptrs[MODEL_SLOTS] = { 0 };
    bytes lengths[MODEL_SLOTS] = { 0 };
    u32 random = UINT32_C(0xA110CA7E);

    MemoryArena *a = MemoryArena_create(64u * MIB);
    assert(a);

    for (bytes step = 0; step < MODEL_STEPS; step++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;

        bytes slot = random % MODEL_SLOTS;
        bytes length = sizes[(random >> 8) % (sizeof(sizes) / sizeof(sizes[0]))];
        u8 value = (u8) slot;

        if (ptrs[slot]) {
            checkBytes(ptrs[slot], lengths[slot], value);
            assert(Memory_type(ptrs[slot]) == EX_TYPE_A);
            assert(Memory_length(ptrs[slot]) == lengths[slot]);
        }

        if (random & 1u) {
            u8 *next = ptrs[slot] ? MemoryArena_realloc(a, ptrs[slot], length)
                                  : MemoryArena_alloc(a, EX_TYPE_A, length);
            assert(next);
            checkBytes(next, lengths[slot] < length ? lengths[slot] : length, value);
            ptrs[slot] = next;
            lengths[slot] = length;
        } else {
            MemoryArena_free(a, ptrs[slot]);
            ptrs[slot] = MemoryArena_alloc(a, EX_TYPE_A, length);
            assert(ptrs[slot]);
            lengths[slot] = length;
        }
        memset(ptrs[slot], value, length);
    }
    MemoryArena_destroy(a);
}

static MemoryArena *g_threadArena;
static atomic_uint g_ready;
static atomic_bool g_start;

/** Executes disjoint concurrent alloc/free cycles after a synchronized barrier. */
static any ex_worker(any context) {
    bytes identity = (bytes) (pointer) context;
    atomic_fetch_add_explicit(&g_ready, 1, memory_order_release);
    while (!atomic_load_explicit(&g_start, memory_order_acquire)) {
    }
    for (bytes i = 0; i < WORKER_STEPS; i++) {
        u8 *p = MemoryArena_alloc(g_threadArena, EX_TYPE_A, 49 + identity);
        assert(p);
        memset(p, (i32) identity, 49 + identity);
        checkBytes(p, 49 + identity, (u8) identity);
        Memory_free(p);
    }
    return nullptr;
}

/** Proves concurrent disjoint alloc/free across worker threads with zero leaks. */
static fn ex_concurrent() {
    atomic_store_explicit(&g_ready, 0, memory_order_relaxed);
    atomic_store_explicit(&g_start, false, memory_order_relaxed);
    g_threadArena = MemoryArena_create(SLAB_BYTES + BUMP_BYTES);
    assert(g_threadArena);

    pthread_t threads[WORKERS];
    for (bytes i = 0; i < WORKERS; i++)
        assert(pthread_create(&threads[i], nullptr, ex_worker, (any) (pointer) i) == 0);
    while (atomic_load_explicit(&g_ready, memory_order_acquire) != WORKERS) {
    }
    atomic_store_explicit(&g_start, true, memory_order_release);

    for (bytes i = 0; i < WORKERS; i++)
        assert(pthread_join(threads[i], nullptr) == 0);
    assert(MemoryArena_findAll(g_threadArena, EX_TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_activeBytes(g_threadArena) == 0);
    MemoryArena_destroy(g_threadArena);
}

/** Proves large-block coalescing, splitting, and tail retraction without freeAll or heap calls. */
static fn ex_coalesce() {
    enum { LARGE_REGION = 32 * 1024 };
    MemoryArena *a = MemoryArena_create(SLAB_BYTES + LARGE_REGION);
    assert(a != nullptr);

    bytes calls = g_heapCalls;
    u8 *aBlock = MemoryArena_alloc(a, EX_TYPE_A, 8192);
    u8 *bBlock = MemoryArena_alloc(a, EX_TYPE_A, 8192);
    u8 *cBlock = MemoryArena_alloc(a, EX_TYPE_A, 8192);
    assert(aBlock && bBlock && cBlock);
    memset(aBlock, 0x11, 8192);
    memset(bBlock, 0x22, 8192);
    memset(cBlock, 0x33, 8192);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 3);

    MemoryArena_free(a, aBlock);
    MemoryArena_free(a, bBlock);
    assert(Memory_length(aBlock) == 0 && Memory_length(bBlock) == 0);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 1);
    checkBytes(cBlock, 8192, 0x33);

    u8 *merged = MemoryArena_alloc(a, EX_TYPE_B, 16384);
    assert(merged == aBlock);
    assert(Memory_type(merged) == EX_TYPE_B && Memory_length(merged) == 16384);
    memset(merged, 0x44, 16384);
    checkBytes(cBlock, 8192, 0x33);
    assert(g_heapCalls == calls);

    MemoryArena_free(a, merged);
    MemoryArena_free(a, cBlock);
    assert(MemoryArena_activeBytes(a) == 0);
    assert(MemoryArena_findAll(a, 0, nullptr, 0) == 0);

    u8 *full = MemoryArena_alloc(a, EX_TYPE_A, LARGE_REGION - MEMORY_HEADER_SIZE);
    assert(full != nullptr);
    assert(g_heapCalls == calls);
    MemoryArena_free(a, full);
    assert(MemoryArena_activeBytes(a) == 0);
    MemoryArena_destroy(a);
}

/** Runs all adversarial Memory exhaustion, fault-injection, hostile, and concurrency scenarios. */
static fn suite_mem_exhaustion() {
    assert(Memory_init(0));
    ex_construction();
    ex_exhaustion();
    ex_coalesce();
    ex_boundaries();
    ex_allClasses();
    ex_hostile();
    ex_model();
    ex_concurrent();
}

typedef struct WatchRec {
    i32 count;
    char lastRel[128];
    FileEventType lastType;
    i64 lastEpoch;
    i64 lastSize;
} WatchRec;

;;GETTER
static inline i32 WatchRec_count(const WatchRec *r) { return (*r).count; }
;;SETTER
static inline fn WatchRec_setCount(WatchRec *r, i32 v) { (*r).count = v; }
;;GETTER
static inline char *WatchRec_lastRelMut(WatchRec *r) { return (*r).lastRel; }
;;SETTER
static inline fn WatchRec_setLastType(WatchRec *r, FileEventType t) { (*r).lastType = t; }
;;SETTER
static inline fn WatchRec_setLastEpoch(WatchRec *r, i64 s) { (*r).lastEpoch = s; }
;;SETTER
static inline fn WatchRec_setLastSize(WatchRec *r, i64 s) { (*r).lastSize = s; }
;;GETTER
static inline const char *WatchEv_rel(const FileWatchEvent *e) { return (*e).relativePath; }
;;GETTER
static inline FileEventType WatchEv_type(const FileWatchEvent *e) { return (*e).type; }
;;GETTER
static inline i64 WatchEv_epoch(const FileWatchEvent *e) { return (*e).epochSeconds; }
;;GETTER
static inline i64 WatchEv_size(const FileWatchEvent *e) { return (*e).fileSize; }

/** Records rich FileWatchEvent deliveries for suite_file_watch assertions. */
static fn onWatchRec(const FileWatchEvent *ev, any userdata) {
    WatchRec *r = (WatchRec*) userdata;
    WatchRec_setCount(r, WatchRec_count(r) + 1);
    const char *rel = WatchEv_rel(ev);
    if (rel)
        snprintf(WatchRec_lastRelMut(r), 128, "%s", rel);
    WatchRec_setLastType(r, WatchEv_type(ev));
    WatchRec_setLastEpoch(r, WatchEv_epoch(ev));
    WatchRec_setLastSize(r, WatchEv_size(ev));
}

/** Proves FileWatchProps defaults, filtering, debounce coalescing, metadata, and null safety. */
static fn suite_file_watch() {
    CHECK(Memory_init(0));
    CHECK(make_scratch_dir("watch"));
    if (g_failures)
        return;

    FileWatchProps defs = FileWatchProps_defaults();
    CHECK(defs.latencyMs == 100u && defs.debounceMs == 50u);
    CHECK(defs.recursive && !defs.emitDirs && defs.ignoreHidden);

    const char *ignores[] = { "*.swp", "build/" };
    const char *includes[] = { ".c", ".h" };
    FileWatchProps props = defs;
    props.latencyMs = 25u;
    props.debounceMs = 500u;
    props.ignorePatterns = ignores;
    props.ignoreCount = 2;
    props.includeExtensions = includes;
    props.includeCount = 2;

    WatchRec rec = { 0 };
    FileEventWatcher *w = FileEventWatcher_createWithProps(g_dir, FILE_EVENT_ALL, &props, onWatchRec, &rec);
    CHECK(w != nullptr);
    CHECK(strcmp(FileEventWatcher_getPath(w), g_dir) == 0);
    CHECK(FileEventWatcher_getMask(w) == FILE_EVENT_ALL);
    CHECK(!FileEventWatcher_isRunning(w));
    CHECK(FileEventWatcher_pendingCount(w) == 0);

    FileWatchProps got = FileEventWatcher_getProps(w);
    CHECK(got.latencyMs == 25u && got.debounceMs == 500u);
    CHECK(got.ignoreCount == 2 && got.includeCount == 2);

    char pOk[320];
    char pHidden[320];
    char pSwp[320];
    char pTxt[320];
    char pSub[320];
    path_of(pOk, sizeof(pOk), "src/main.c");
    path_of(pHidden, sizeof(pHidden), ".git/config.c");
    path_of(pSwp, sizeof(pSwp), "main.c.swp");
    path_of(pTxt, sizeof(pTxt), "notes.txt");
    path_of(pSub, sizeof(pSub), "sub/deep.c");

    CHECK(FileEventWatcher_matchesPath(w, pOk, false));
    CHECK(!FileEventWatcher_matchesPath(w, pOk, true));
    CHECK(!FileEventWatcher_matchesPath(w, pHidden, false));
    CHECK(!FileEventWatcher_matchesPath(w, pSwp, false));
    CHECK(!FileEventWatcher_matchesPath(w, pTxt, false));

    char realFile[320];
    path_of(realFile, sizeof(realFile), "hello.c");
    FILE *fp = fopen(realFile, "wb");
    CHECK(fp != nullptr);
    if (fp) {
        fwrite("int x = 1;\n", 1, 11, fp);
        fclose(fp);
    }

    CHECK(FileEventWatcher_pushSynthetic(w, realFile, FILE_EVENT_CREATED, false));
    CHECK(FileEventWatcher_pushSynthetic(w, realFile, FILE_EVENT_MODIFIED, false));
    CHECK(FileEventWatcher_pendingCount(w) == 1);
    CHECK(!FileEventWatcher_pushSynthetic(w, pHidden, FILE_EVENT_MODIFIED, false));
    CHECK(FileEventWatcher_pendingCount(w) == 1);

    FileEventWatcher_pump(w);
    CHECK(FileEventWatcher_pendingCount(w) == 0);
    CHECK(rec.count == 1);
    CHECK(strcmp(rec.lastRel, "hello.c") == 0);
    CHECK(rec.lastType == (FILE_EVENT_CREATED | FILE_EVENT_MODIFIED));
    CHECK(rec.lastEpoch > 1700000000LL);
    CHECK(rec.lastSize == 11);

    props.recursive = false;
    CHECK(FileEventWatcher_setProps(w, &props));
    CHECK(!FileEventWatcher_matchesPath(w, pSub, false));
    CHECK(FileEventWatcher_matchesPath(w, realFile, false));

    CHECK(FileEventWatcher_getPath(nullptr) == nullptr);
    CHECK(FileEventWatcher_getMask(nullptr) == FILE_EVENT_NONE);
    CHECK(!FileEventWatcher_isRunning(nullptr));
    CHECK(FileEventWatcher_pendingCount(nullptr) == 0);
    CHECK(!FileEventWatcher_matchesPath(nullptr, realFile, false));
    CHECK(!FileEventWatcher_setProps(nullptr, &props));

    FileEventWatcher_destroy(w);
    FileEventWatcher_destroy(nullptr);
    File_delete(realFile);
    Memory_freeAll();
}

typedef struct SuiteEntry {
    const char *name;
    fn (*run)();
} SuiteEntry;

/** Runs all automated vex/re suites or a single named suite passed as argv[1]. */
i32 main(i32 argc, char **argv) {
    const SuiteEntry suites[] = {
        { "hot_file", suite_hot_file },
        { "findex", suite_findex },
        { "log", suite_log },
        { "filewriter", suite_filewriter },
        { "file", suite_file },
        { "logparser", suite_logparser },
        { "mem", suite_mem },
        { "transient_lifetime", suite_transient_lifetime },
        { "mem_exhaustion", suite_mem_exhaustion },
        { "file_watch", suite_file_watch },
    };
    const char *filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && strcmp(filter, suites[i].name) != 0)
            continue;
        suites[i].run();
        ran++;
    }

    if (filter && ran == 0) {
        fprintf(stderr, "re_test: unknown suite '%s'\n", filter);
        return 2;
    }
    if (g_failures != 0) {
        fprintf(stderr, "re_test: %d FAILURES\n", g_failures);
        return 1;
    }
    puts("re_test: PASS");
    return 0;
}
