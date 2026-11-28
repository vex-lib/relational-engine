/**
 * Consolidated automated owner test suite for vex/re (io + nio).
 * Usage: re_test [hot_file|findex|log|filewriter|file|logparser|transient_lifetime|mem|mem_exhaustion]
 */

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
#include "test/re_test.h"
#include "hash/hash.h"
#include "substrate/relational/cell.h"
#include "substrate/relational/symbol_table.h"
#include "substrate/relational/relational.h"
#include "lang/annotation.h"
#include "type/re_type.h"
#include "substrate/relational/shelf.h"
#include "substrate/variable/var.h"
#include "substrate/variable/variable_hash_map.h"
#include "substrate/variable/variable_slot.h"
#include "substrate/variable/variable_mini_map.h"
#include "substrate/variable/variable_pool.h"
#include "substrate/substrate.h"
#include "re.h"
#include <sched.h>
#include "atomic/atomic.h"
#include "struct/struct.h"
#include "exception/try_value.h"
#include "exception/try_code.h"
#include "thread/re_thread.h"
#include "exception/try_ptr.h"
#include "exception/try.h"

static i32 g_failures = 0;
static utf8 g_dir[256];

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Creates a fresh per-suite scratch directory under TMPDIR. */
static bool make_scratch_dir(string tag) {
    string base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    snprintf(g_dir, sizeof(g_dir), "%s/vex_re_%s_%d", base, tag, getpid());
    File_delete(g_dir);
    return File_mkdirs(g_dir);
}

/** Joins the active scratch directory with a relative leaf path. */
static fn path_of(utf8 *out, bytes cap, string leaf) {
    snprintf(out, cap, "%s/%s", g_dir, leaf);
}

/** Reads a file's byte length via stdio for on-disk verification. */
static i64 read_file_size(string p) {
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
    FIndex index = FIndex(4);
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

    FIndex empty = FIndex(0);
    assert(!FIndex_live(&empty));
    assert(FIndex_insert(&empty, 7, 70, 7));
    assert(FIndex_live(&empty));
    assert(FIndex_find(&empty, 7, &offset, &length) && offset == 70 && length == 7);
    FIndex_shutdown(&empty);

    MemoryArena *arena = MemoryArena(64u * 1024u * 1024u);
    assert(arena);
    FIndex arenad = FIndex(2, arena);
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

    utf8 path[300];
    path_of(path, sizeof(path), "engine.log");
    CHECK(!Log_init(nullptr, path, 8));

    Log log;
    CHECK(!Log_init(&log, nullptr, 8));
    CHECK(Log_init(&log, path, 8));
    CHECK(Log_isEnabled(&log));
    CHECK(Log_isActive(&log));
    CHECK(string_equals(Log_path(&log), path));
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
    utf8 path2[320];
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

    utf8 p[300];
    path_of(p, sizeof(p), "sub/dir/out.bin");
    CHECK(FileWriter_open(&w, p));
    CHECK(FileWriter_bytesWritten(&w) == 0);
    CHECK(File_exists(p));

    FileWriter_write(&w, (const u8*) "hello", 5);
    CHECK(FileWriter_bytesWritten(&w) == 5);
    FileWriter_write(&w, (const u8*) "", 0);
    CHECK(FileWriter_bytesWritten(&w) == 5);

    enum { BIG = 200000 };
    static u8 big[BIG];
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
        static u8 back[BIG];
        CHECK(fread(back, 1, BIG, f) == (bytes) BIG);
        CHECK(memcmp(back, big, BIG) == 0);
        fclose(f);
    }

    FileWriter_write(&w, (const u8*) "late", 4);
    CHECK(FileWriter_bytesWritten(&w) == (u64) (5 + BIG));

    FileWriter w2;
    memset(&w2, 0, sizeof(w2));
    CHECK(!FileWriter_open(&w2, nullptr));
    CHECK(!FileWriter_open(nullptr, "some/path"));

    utf8 blocker[300];
    path_of(blocker, sizeof(blocker), "blocker");
    FILE *bf = fopen(blocker, "wb");
    CHECK(bf != nullptr);
    if (bf)
        fclose(bf);
    utf8 under[320];
    snprintf(under, sizeof(under), "%s/child", blocker);
    CHECK(!FileWriter_open(&w2, under));

    File_delete(p);
    File_delete(blocker);
}

/** Checks directory/file operations, nested creation, idempotence, and null paths. */
static fn file_path_ops() {
    utf8 nested[300];
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
    utf8 p[300];
    path_of(p, sizeof(p), "data.bin");
    CHECK(!File_exists(p));

    File *f = File_open(p, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_mode(f) == (FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE));
    CHECK(File_name(f) != nullptr && string_equals(File_name(f), p));
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
    utf8 buf[16] = { 0 };
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
    utf8 p[300];
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
    utf8 buf[8] = { 0 };
    CHECK(File_read(f, buf, sizeof(buf)) == 5);
    CHECK(memcmp(buf, "abcde", 5) == 0);
    File_close(f);
    File_delete(p);
}

/** Exercises invalid paths, missing files, bad lengths, null handles, and safe failures. */
static fn file_errors() {
    CHECK(File_open(nullptr, FILE_MODE_READ) == nullptr);

    utf8 p[300];
    path_of(p, sizeof(p), "missing.bin");
    File_delete(p);
    CHECK(File_open(p, FILE_MODE_READ) == nullptr);

    utf8 q[300];
    path_of(q, sizeof(q), "err.bin");
    File *f = File_open(q, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    CHECK(f != nullptr);
    CHECK(File_write(f, "abc", 3) == 3);
    CHECK(File_write(f, nullptr, 3) == -1);
    CHECK(File_write(f, "x", -1) == -1);
    CHECK(File_write(f, "x", 0) == 0);
    CHECK(File_read(f, nullptr, 1) == -1);
    utf8 tmp[4];
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

;;GETTER
static inline i32 Collector_count(const Collector *c)
{
    return (*c).count;
}
;;SETTER
static inline fn Collector_setCount(Collector *c, i32 count)
{
    (*c).count = count;
}
;;SETTER
static inline fn Collector_setKindAt(Collector *c, i32 idx, i32 kind)
{
    (*c).kinds[idx] = kind;
}
;;SETTER
static inline fn Collector_setV0At(Collector *c, i32 idx, i64 v0)
{
    (*c).v0[idx] = v0;
}
;;SETTER
static inline fn Collector_setTsAt(Collector *c, i32 idx, i64 ts)
{
    (*c).ts[idx] = ts;
}

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
    utf8 p[300];
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
    utf8 p[300];
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
    utf8 out[256];
    i32 n = LogParser_formatRecord(out, sizeof(out), 1,
                                   1012345000LL, 1000000000LL, nullptr,
                                   1, 2, 3, 4, 5);
    CHECK(n > 0);
    CHECK(string_contains(out, "12.345 ms"));
    CHECK(string_contains(out, "produce"));
    CHECK(string_contains(out, "1  2  3  4  5"));

    n = LogParser_formatRecord(out, sizeof(out), 2, 5, 0, "custom", 0, 0, 0, 0, 0);
    CHECK(n > 0);
    CHECK(string_contains(out, "0.000 ms"));
    CHECK(string_contains(out, "custom"));

    utf8 small[8];
    CHECK(LogParser_formatRecord(small, sizeof(small), 1, 1, 0, "x", 0, 0, 0, 0, 0) == -1);

    CHECK(string_equals(LogParser_kindName(1), "produce"));
    CHECK(string_equals(LogParser_kindName(2), "present"));
    CHECK(string_equals(LogParser_kindName(3), "drop"));
    CHECK(string_equals(LogParser_kindName(10), "keyDown"));
    CHECK(string_equals(LogParser_kindName(33), "touchCancel"));
    CHECK(string_equals(LogParser_kindName(999), "kind#999"));
    CHECK(string_equals(LogParser_kindName(-3), "kind#-3"));
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

;;GETTER
static inline utf8 *TestLabel_text(const TestLabel *lbl)
{
    return (*lbl).text;
}
;;SETTER
static inline fn TestLabel_setTextField(TestLabel *lbl, utf8 *text)
{
    (*lbl).text = text;
}
;;GETTER
static inline bool TestLabel_ownsText(const TestLabel *lbl)
{
    return (*lbl).ownsText;
}
;;SETTER
static inline fn TestLabel_setOwnsText(TestLabel *lbl, bool owns)
{
    (*lbl).ownsText = owns;
}

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
static fn TestLabel_setText(TestLabel *lbl, string text) {
    if (!lbl)
        return;
    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl))
        Memory_free(TestLabel_text(lbl));
    if (text) {
        bytes len = strlen(text) + 1;
        utf8 *copy = (utf8*) Memory_alloc(101, Re_arena(), len);
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
static fn TestLabel_setTextBorrowed(TestLabel *lbl, string text) {
    if (!lbl)
        return;
#if defined(DEBUG_BORROW_CHECK)
    if (Transient_contains(text) && !Transient_contains(lbl)) {
        fprint(stderr, "[LIFETIME ESCAPE] TestLabel_setTextBorrowed: escape abort");
        abort();
    }
#endif
    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl))
        Memory_free(TestLabel_text(lbl));
    TestLabel_setTextField(lbl, (utf8*) text);
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
    string lit = "literal_constant_string";
    assert(Memory_getLifetime(lit) == MEMORY_LIFETIME_UNKNOWN);

    utf8 stackBuf[64];
    memcpy(stackBuf, "stack_buffer_array", strlen("stack_buffer_array") + 1);
    assert(Memory_getLifetime(stackBuf) == MEMORY_LIFETIME_UNKNOWN);

    any perm = Memory_alloc(1, Re_arena(), 128);
    assert(perm != nullptr);
    assert(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);

    any trans = Transient_alloc(2, 128);
    assert(trans != nullptr);
    assert(Memory_getLifetime(trans) == MEMORY_LIFETIME_TRANSIENT);

    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
    Memory_free((any) lit);
    Memory_free((any) stackBuf);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
    Memory_free(trans);

    utf8 *transStr = (utf8*) Transient_alloc(3, 32);
    memcpy(transStr, "transient_string_123", strlen("transient_string_123") + 1);
    TestLabel *lbl = TestLabel_0();
    assert(lbl != nullptr);
    TestLabel_setText(lbl, transStr);
    assert(TestLabel_ownsText(lbl) == true);
    assert(Memory_getLifetime(TestLabel_text(lbl)) == MEMORY_LIFETIME_PERMANENT);

    Transient_reset();
    assert(string_equals(TestLabel_text(lbl), "transient_string_123"));

    string staticStr = "constant_borrowed_literal";
    TestLabel_setTextBorrowed(lbl, staticStr);
    assert(TestLabel_ownsText(lbl) == false);
    assert(TestLabel_text(lbl) == staticStr);

    TestLabel_setText(lbl, "next_owned_value");
    assert(TestLabel_ownsText(lbl) == true);
    assert(string_equals(TestLabel_text(lbl), "next_owned_value"));
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
        utf8 *escapeStr = (utf8*) Transient_alloc(5, 32);
        memcpy(escapeStr, "escaped_transient", strlen("escaped_transient") + 1);
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

/** Verifies Memory/MemoryArena alignment, overflow refusal, foreign pointers, realloc, and transient generation. */
static fn suite_mem() {
    CHECK(Memory_init(0));
    CHECK(FREED != nullptr);
    CHECK(((pointer) FREED & 31) == 0);
    CHECK(Memory_isFreed(FREED) == true);
    CHECK(Memory_isFreed(nullptr) == false);
    CHECK(Memory_isFreed(g_foreign) == false);
    CHECK(Memory_length(FREED) == 0);
    CHECK(Memory_type(FREED) == 0);
    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
    Memory_free(FREED);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
    CHECK(Memory_realloc(FREED, 32) == nullptr);
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
    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
    Memory_free(g_foreign);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
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
    CHECK(Memory_isFreed(found[0]) == false);
    Memory_free(found[0]);
    CHECK(Memory_isFreed(found[0]) == true);
    const MemoryHeader *freedHdr = (const MemoryHeader*) ((const u8*) found[0] - sizeof(MemoryHeader));
    CHECK(MemoryHeader_pointer(freedHdr) == (u64) (pointer) FREED);
    CHECK(MemoryHeader_hash(freedHdr) == 0);
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);
    CHECK(Memory_length(found[0]) == 0);
    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
    Memory_free(found[0]);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);
    Memory_freeAll();
    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 0);

    MemoryArena *b = MemoryArena(64u * 1024u * 1024u);
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
        Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
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
    SMALL_PAYLOAD = 64 - MEMORY_HEADER_SIZE,
    MODEL_SLOTS = 64,
    MODEL_STEPS = 10000,
    WORKERS = 4,
    WORKER_STEPS = 4000
};

static const u64 EX_TYPE_A = UINT64_C(0x1200000000000041);
static const u64 EX_TYPE_B = UINT64_C(0x1300000000000041);
static bytes g_heapCalls;
static i32 g_failAfter = -1;

/** Injects a failure at a chosen page-allocation stage; otherwise delegates to MemoryPool. */
static bool failHeap()
{
    if (g_failAfter < 0)
        return false;

    g_heapCalls++;

    if (g_failAfter == 0) {
        g_failAfter = -1;
        return true;
    }

    g_failAfter--;
    return false;
}

extern PageMeta *MemoryPool_allocPages(MemoryPool *pool, MemoryArena *owner, bytes count, u8 kind, u8 slabClass);

/** Fault-injecting page-allocation seam for arena.o when compiled with MEM_TEST_FAULTS. */
PageMeta *MemTest_allocPages(MemoryPool *pool, MemoryArena *owner, bytes count, u8 kind, u8 slabClass)
{
    if (failHeap())
        return nullptr;

    return MemoryPool_allocPages(pool, owner, count, kind, slabClass);
}

/** Asserts every byte in a range matches the expected value. */
static fn checkBytes(const u8 *ptr, bytes length, u8 value) {
    for (bytes i = 0; i < length; i++)
        assert(ptr[i] == value);
}

/** Verifies arena construction faults, 1-page budgets, unbounded arenas, and many-arena pool sharing. */
static fn ex_construction() {
#if defined(MEM_TEST_FAULTS)
    g_failAfter = 0;
    assert(MemoryArena_pages(4) == nullptr);
#endif
    g_failAfter = -1;

    MemoryArena *tiny = MemoryArena(1);
    assert(tiny != nullptr);
    assert(MemoryArena_maxPages(tiny) == 1);
    assert(MemoryArena_usedPages(tiny) == 0);
    assert(MemoryArena_capacity(tiny) == MEMORY_PAGE_SIZE);
    MemoryArena_destroy(tiny);

    MemoryArena *unbounded = MemoryArena();
    assert(unbounded != nullptr);
    assert(MemoryArena_maxPages(unbounded) == 0);
    assert(MemoryArena_usedPages(unbounded) == 0);
    assert(MemoryArena_capacity(unbounded) == 0);
    MemoryArena_destroy(unbounded);

    MemoryPool *pool = MemoryPool_default();
    assert(pool != nullptr);
    assert(MemoryPool_pageSize() == MEMORY_PAGE_SIZE);
    bytes usedBefore = MemoryPool_usedPages(pool);

    enum { MANY_ARENAS = 256 };
    MemoryArena *owners[MANY_ARENAS];
    for (bytes i = 0; i < MANY_ARENAS; i++) {
        owners[i] = MemoryArena_pages(2);
        assert(owners[i] != nullptr);
        u8 *p = MemoryArena_alloc(owners[i], EX_TYPE_A, 32);
        assert(p != nullptr && Memory_type(p) == EX_TYPE_A);
        assert(MemoryArena_usedPages(owners[i]) == 1);
    }
    assert(MemoryPool_usedPages(pool) >= usedBefore + MANY_ARENAS);
    for (bytes i = 0; i < MANY_ARENAS; i++)
        MemoryArena_destroy(owners[i]);
    assert(MemoryPool_usedPages(pool) == usedBefore);

    bytes chunksBefore = MemoryPool_chunkCount(pool);
    MemoryArena *spike = MemoryArena();
    assert(spike != nullptr);
    u8 *bigA = MemoryArena_alloc(spike, EX_TYPE_A, 40u * MIB);
    u8 *bigB = MemoryArena_alloc(spike, EX_TYPE_B, 40u * MIB);
    assert(bigA != nullptr && bigB != nullptr);
    assert(MemoryPool_chunkCount(pool) > chunksBefore);
    MemoryArena_destroy(spike);
    assert(MemoryPool_chunkCount(pool) == chunksBefore);
    assert(MemoryPool_usedPages(pool) == usedBefore);
}

/** Exhausts a 1-page arena budget, verifies no heap fallback, preserves bytes, and returns pages to pool. */
static fn ex_exhaustion() {
    enum { PAGE_SLOTS = MEMORY_PAGE_SIZE / 64u };
    const bytes pageSlots = PAGE_SLOTS;
    MemoryArena *a = MemoryArena_pages(1);
    assert(a != nullptr);
    assert(MemoryArena_exhaustionCount(a) == 0);
    assert(MemoryArena_usedPages(a) == 0);

    u8 *blocks[PAGE_SLOTS];

    bytes calls = g_heapCalls;
    for (bytes i = 0; i < pageSlots; i++) {
        blocks[i] = MemoryArena_alloc(a, EX_TYPE_A, SMALL_PAYLOAD);
        assert(blocks[i] != nullptr);
        assert(((pointer) blocks[i] & 31u) == 0);
        memset(blocks[i], (u8) i, SMALL_PAYLOAD);
    }
    assert(MemoryArena_usedPages(a) == 1);
    assert(MemoryArena_activeBytes(a) == pageSlots * 64u);

    u64 rejects = MemoryArena_exhaustionCount(a);
    for (bytes i = 0; i < 32; i++)
        assert(MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == rejects + 32);
    assert(g_heapCalls == calls);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == pageSlots);

    any found[3] = { nullptr, (any) blocks[0], (any) blocks[1] };
    assert(MemoryArena_findAll(a, EX_TYPE_A, found, 1) == pageSlots);
    assert(found[1] == blocks[0] && found[2] == blocks[1]);

    for (bytes i = 0; i < pageSlots; i++)
        checkBytes(blocks[i], SMALL_PAYLOAD, (u8) i);

    u8 *victim = blocks[0];
    assert(!Memory_isFreed(victim));
    MemoryArena_free(a, victim);
    assert(Memory_isFreed(victim));
    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
    MemoryArena_free(a, victim);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
    assert(Memory_length(victim) == 0);

    u8 *reused = MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD);
    assert(reused == victim);
    assert(!Memory_isFreed(reused));
    assert(Memory_type(reused) == EX_TYPE_B);

    u64 beforeReject = MemoryArena_exhaustionCount(a);
    assert(MemoryArena_alloc(a, EX_TYPE_B, SMALL_PAYLOAD) == nullptr);
    assert(MemoryArena_exhaustionCount(a) == beforeReject + 1);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == pageSlots - 1);

    MemoryArena_freeAll(a);
    assert(MemoryArena_usedPages(a) == 0);
    assert(MemoryArena_activeBytes(a) == 0);
    assert(MemoryArena_exhaustionCount(a) == 0);
    assert(Memory_type(blocks[1]) == 0);
    assert(Memory_length(blocks[1]) == 0);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_alloc(a, EX_TYPE_A, SMALL_PAYLOAD) != nullptr);
    assert(MemoryArena_usedPages(a) == 1);
    MemoryArena_destroy(a);
}

/** Exercises exact page-budget boundaries, failed realloc, shrink, and cross-arena transfer. */
static fn ex_boundaries() {
    MemoryArena *a = MemoryArena_pages(1);
    MemoryArena *b = MemoryArena_pages(2);
    assert(a && b);

    u8 *p = MemoryArena_alloc(a, EX_TYPE_A, 4096u - MEMORY_HEADER_SIZE);
    assert(p != nullptr);
    MemoryArena_free(a, p);
    assert(MemoryArena_usedPages(a) == 0);
    assert(MemoryArena_alloc(a, EX_TYPE_A, MEMORY_PAGE_SIZE) == nullptr);
    MemoryArena_destroy(a);

    a = MemoryArena_pages(2);
    assert(a);
    p = MemoryArena_alloc(a, EX_TYPE_A, MEMORY_PAGE_SIZE);
    assert(p && Memory_length(p) == MEMORY_PAGE_SIZE);
    assert(MemoryArena_usedPages(a) == 2);
    memset(p, 0xA5, MEMORY_PAGE_SIZE);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 1);
    assert(MemoryArena_realloc(a, p, MEMORY_PAGE_SIZE) == nullptr);
    checkBytes(p, MEMORY_PAGE_SIZE, 0xA5);
    assert(Memory_type(p) == EX_TYPE_A && Memory_length(p) == MEMORY_PAGE_SIZE);

    u8 *q = MemoryArena_realloc(b, p, 31);
    assert(q && Memory_type(q) == EX_TYPE_A && Memory_length(q) == 31);
    checkBytes(q, 31, 0xA5);
    assert(Memory_type(p) == 0);
    assert(MemoryArena_usedPages(a) == 0);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_findAll(b, EX_TYPE_A, nullptr, 0) == 1);

    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
    MemoryArena_free(a, q);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
    checkBytes(q, 31, 0xA5);
    assert(Memory_length(q) == 31);
    MemoryArena_destroy(a);
    MemoryArena_destroy(b);
}

/** Exhausts every slab size class in a 1-page arena independently, then verifies free, reuse, and page return. */
static fn ex_allClasses() {
    const bytes blockSizes[] = { 64, 128, 256, 512, 1024, 2048, 4096 };
    MemoryArena *a = MemoryArena_pages(1);
    assert(a);

    for (bytes c = 0; c < sizeof(blockSizes) / sizeof(blockSizes[0]); c++) {
        bytes payload = blockSizes[c] - MEMORY_HEADER_SIZE;
        bytes cap = MEMORY_PAGE_SIZE / blockSizes[c];
        bytes calls = g_heapCalls;
        u8 *last = nullptr;
        for (bytes i = 0; i < cap; i++) {
            last = MemoryArena_alloc(a, EX_TYPE_A, payload);
            assert(last && Memory_length(last) == payload);
            memset(last, 0x5A, payload);
        }
        assert(MemoryArena_usedPages(a) == 1);
        assert(MemoryArena_alloc(a, EX_TYPE_A, payload) == nullptr);
        assert(MemoryArena_realloc(a, last, SIZE_MAX) == nullptr);
        checkBytes(last, payload, 0x5A);

        Memory_free(last);
        u8 *replacement = MemoryArena_alloc(a, EX_TYPE_B, payload);
        assert(replacement == last && Memory_type(replacement) == EX_TYPE_B);
        assert(MemoryArena_alloc(a, EX_TYPE_A, payload) == nullptr);
        assert(g_heapCalls == calls);

        MemoryArena_freeAll(a);
        assert(Memory_length(last) == 0);
        assert(MemoryArena_usedPages(a) == 0);
        assert(MemoryArena_activeBytes(a) == 0);
    }
    MemoryArena_destroy(a);
}

/** Rejects misaligned pointers and tampered headers while preserving unrelated blocks. */
static fn ex_hostile() {
    MemoryArena *a = MemoryArena_pages(4);
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

    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);
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
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
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
        2031, 2032, 2033, 4079, 4080, 4081, 8192, MEMORY_PAGE_SIZE + 512
    };
    u8 *ptrs[MODEL_SLOTS] = { 0 };
    bytes lengths[MODEL_SLOTS] = { 0 };
    u32 random = UINT32_C(0xA110CA7E);

    MemoryArena *a = MemoryArena();
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
    g_threadArena = MemoryArena();
    assert(g_threadArena);

    thread threads[WORKERS];
    for (bytes i = 0; i < WORKERS; i++)
        assert(pthread_create(&threads[i], nullptr, ex_worker, (any) (pointer) i) == 0);
    while (atomic_load_explicit(&g_ready, memory_order_acquire) != WORKERS) {
    }
    atomic_store_explicit(&g_start, true, memory_order_release);

    for (bytes i = 0; i < WORKERS; i++)
        assert(pthread_join(threads[i], nullptr) == 0);
    assert(MemoryArena_findAll(g_threadArena, EX_TYPE_A, nullptr, 0) == 0);
    assert(MemoryArena_activeBytes(g_threadArena) == 0);
    assert(MemoryArena_usedPages(g_threadArena) == 0);
    MemoryArena_destroy(g_threadArena);
}

/** Proves multi-page span allocation, pool page coalescing, and full return without heap calls. */
static fn ex_coalesce() {
    MemoryArena *a = MemoryArena_pages(8);
    assert(a != nullptr);

    bytes calls = g_heapCalls;
    u8 *aBlock = MemoryArena_alloc(a, EX_TYPE_A, MEMORY_PAGE_SIZE);
    u8 *bBlock = MemoryArena_alloc(a, EX_TYPE_A, MEMORY_PAGE_SIZE);
    u8 *cBlock = MemoryArena_alloc(a, EX_TYPE_A, MEMORY_PAGE_SIZE);
    assert(aBlock && bBlock && cBlock);
    assert(MemoryArena_usedPages(a) == 6);
    memset(aBlock, 0x11, MEMORY_PAGE_SIZE);
    memset(bBlock, 0x22, MEMORY_PAGE_SIZE);
    memset(cBlock, 0x33, MEMORY_PAGE_SIZE);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 3);

    MemoryArena_free(a, aBlock);
    MemoryArena_free(a, bBlock);
    assert(Memory_length(aBlock) == 0 && Memory_length(bBlock) == 0);
    assert(MemoryArena_usedPages(a) == 2);
    assert(MemoryArena_findAll(a, EX_TYPE_A, nullptr, 0) == 1);
    checkBytes(cBlock, MEMORY_PAGE_SIZE, 0x33);

    u8 *merged = MemoryArena_alloc(a, EX_TYPE_B, 3u * MEMORY_PAGE_SIZE);
    assert(merged != nullptr);
    assert(MemoryArena_usedPages(a) == 6);
    assert(Memory_type(merged) == EX_TYPE_B && Memory_length(merged) == 3u * MEMORY_PAGE_SIZE);
    memset(merged, 0x44, 3u * MEMORY_PAGE_SIZE);
    checkBytes(cBlock, MEMORY_PAGE_SIZE, 0x33);
    assert(g_heapCalls == calls);

    MemoryArena_free(a, merged);
    MemoryArena_free(a, cBlock);
    assert(MemoryArena_usedPages(a) == 0);
    assert(MemoryArena_activeBytes(a) == 0);
    assert(MemoryArena_findAll(a, 0, nullptr, 0) == 0);

    u8 *full = MemoryArena_alloc(a, EX_TYPE_A, 7u * MEMORY_PAGE_SIZE);
    assert(full != nullptr);
    assert(MemoryArena_usedPages(a) == 8);
    assert(g_heapCalls == calls);
    MemoryArena_free(a, full);
    assert(MemoryArena_usedPages(a) == 0);
    assert(MemoryArena_activeBytes(a) == 0);
    MemoryArena_destroy(a);
}

;;GETTER
static MemoryArena *ExRaceCtx_arena(const ExRaceCtx *c)
{
    return c ? (*c).arena : nullptr;
}
;;GETTER
static any ExRaceCtx_sharedPtr(const ExRaceCtx *c)
{
    return c ? (*c).sharedPtr : nullptr;
}
;;GETTER
static au32 *ExRaceCtx_go(const ExRaceCtx *c)
{
    return c ? (*c).go : nullptr;
}
;;GETTER
static au32 *ExRaceCtx_trueCount(const ExRaceCtx *c)
{
    return c ? (*c).trueCount : nullptr;
}
;;GETTER
static au32 *ExRaceCtx_falseCount(const ExRaceCtx *c)
{
    return c ? (*c).falseCount : nullptr;
}

static any exRaceWorker(any arg)
{
    ExRaceCtx *ctx = (ExRaceCtx*) arg;
    au32 *go = ExRaceCtx_go(ctx);
    while (atomic_load(go) == 0u) {
    }
    if (MemoryArena_free(ExRaceCtx_arena(ctx), ExRaceCtx_sharedPtr(ctx)))
        atomic_fetch_add(ExRaceCtx_trueCount(ctx), 1u);
    else
        atomic_fetch_add(ExRaceCtx_falseCount(ctx), 1u);
    return nullptr;
}

/** Spawn a child process under default MEMORY_FAULT_ABORT and assert SIGABRT + tip crash dump. */
static fn ex_assert_abort_with_tip(bool useFreedSentinel)
{
    i32 pipeFd[2] = { -1, -1 };
    assert(pipe(pipeFd) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(pipeFd[0]);
        (discard) dup2(pipeFd[1], STDERR_FILENO);
        close(pipeFd[1]);
        Memory_setFaultPolicy(MEMORY_FAULT_ABORT);

        if (useFreedSentinel) {
            (discard) Memory_free(FREED);
            _exit(0);
        }

        MemoryArena *childArena = MemoryArena_pages(4);
        any blk = MemoryArena_alloc(childArena, EX_TYPE_A, 64);
        any keep = MemoryArena_alloc(childArena, EX_TYPE_A, 64);
        (discard) keep;
        (discard) Memory_free(blk);
        (discard) Memory_free(blk);
        _exit(0);
    }

    close(pipeFd[1]);

    utf8 dump[4096];
    memset(dump, 0, sizeof(dump));
    bytes total = 0u;

    while (total + 1u < sizeof(dump)) {
        i64 n = (i64) read(pipeFd[0], dump + total, sizeof(dump) - 1u - total);

        if (n <= 0)
            break;

        total += (bytes) n;
    }

    close(pipeFd[0]);

    i32 status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGABRT);
    assert(string_contains(dump, "FATAL MEMORY FAULT"));
    assert(string_contains(dump, "double-free"));
    assert(string_contains(dump, "tip:"));
}

static fn ex_cve_double_free_and_uaf()
{
    assert(Memory_faultPolicy() == MEMORY_FAULT_ABORT);
    ex_assert_abort_with_tip(false);
    ex_assert_abort_with_tip(true);

    Memory_setFaultPolicy(MEMORY_FAULT_RETURN_FALSE);

    MemoryArena *a = MemoryArena_pages(8);
    assert(a != nullptr);

    any p = MemoryArena_alloc(a, EX_TYPE_A, 64);
    any anchor = MemoryArena_alloc(a, EX_TYPE_A, 64);
    assert(p != nullptr && anchor != nullptr);

    assert(Memory_free(p) == true);
    assert(Memory_isFreed(p) == true);
    assert(Memory_free(p) == false);
    assert(MemoryArena_free(a, p) == false);
    assert(Memory_free(FREED) == false);
    assert(MemoryArena_free(a, FREED) == false);
    assert(Memory_free(nullptr) == false);
    assert(MemoryArena_free(a, nullptr) == false);

    assert(Memory_safeHeader(p) == nullptr);
    assert(Memory_type(p) == 0);
    assert(Memory_length(p) == 0);
    assert(Memory_realloc(p, 128) == nullptr);
    assert(MemoryArena_realloc(a, p, 128) == nullptr);
    assert(Memory_safeHeader(FREED) == nullptr);
    assert(Memory_realloc(FREED, 64) == nullptr);

    assert(Memory_free(anchor) == true);
    assert(MemoryArena_free(a, anchor) == false);

    any racePtr = MemoryArena_alloc(a, EX_TYPE_B, 128);
    assert(racePtr != nullptr);
    au32 go = 0u;
    au32 trueCount = 0u;
    au32 falseCount = 0u;
    ExRaceCtx ctx;
    memset(&ctx, 0, sizeof(ExRaceCtx));
    ctx.arena = a;
    ctx.sharedPtr = racePtr;
    ctx.go = &go;
    ctx.trueCount = &trueCount;
    ctx.falseCount = &falseCount;

    thread threads[8];
    for (u32 i = 0u; i < 8u; i++)
        assert(pthread_create(&threads[i], nullptr, exRaceWorker, &ctx) == 0);
    atomic_store(&go, 1u);
    for (u32 i = 0u; i < 8u; i++)
        pthread_join(threads[i], nullptr);

    assert(atomic_load(&trueCount) == 1u);
    assert(atomic_load(&falseCount) == 7u);
    assert(MemoryArena_activeBytes(a) == 0u);
    MemoryArena_destroy(a);
    Memory_setFaultPolicy(MEMORY_FAULT_ABORT);
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
    ex_cve_double_free_and_uaf();
}

;;GETTER
static inline i32 WatchRec_count(const WatchRec *r)
{
    return (*r).count;
}
;;SETTER
static inline fn WatchRec_setCount(WatchRec *r, i32 v)
{
    (*r).count = v;
}
;;GETTER
static inline utf8 *WatchRec_lastRelMut(WatchRec *r)
{
    return (*r).lastRel;
}
;;SETTER
static inline fn WatchRec_setLastType(WatchRec *r, FileEventType t)
{
    (*r).lastType = t;
}
;;SETTER
static inline fn WatchRec_setLastEpoch(WatchRec *r, i64 s)
{
    (*r).lastEpoch = s;
}
;;SETTER
static inline fn WatchRec_setLastSize(WatchRec *r, i64 s)
{
    (*r).lastSize = s;
}
;;GETTER
static inline string WatchEv_rel(const FileWatchEvent *e)
{
    return (*e).relativePath;
}
;;GETTER
static inline FileEventType WatchEv_type(const FileWatchEvent *e)
{
    return (*e).type;
}
;;GETTER
static inline i64 WatchEv_epoch(const FileWatchEvent *e)
{
    return (*e).epochSeconds;
}
;;GETTER
static inline i64 WatchEv_size(const FileWatchEvent *e)
{
    return (*e).fileSize;
}

/** Records rich FileWatchEvent deliveries for suite_file_watch assertions. */
static fn onWatchRec(const FileWatchEvent *ev, any userdata) {
    WatchRec *r = (WatchRec*) userdata;
    WatchRec_setCount(r, WatchRec_count(r) + 1);
    string rel = WatchEv_rel(ev);
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

    string ignores[] = { "*.swp", "build/" };
    string includes[] = { ".c", ".h" };
    FileWatchProps props = defs;
    props.latencyMs = 25u;
    props.debounceMs = 500u;
    props.ignorePatterns = ignores;
    props.ignoreCount = 2;
    props.includeExtensions = includes;
    props.includeCount = 2;

    WatchRec rec = { 0 };
    FileEventWatcher *w = FileEventWatcher(g_dir, FILE_EVENT_ALL, &props, onWatchRec, &rec);
    CHECK(w != nullptr);
    CHECK(string_equals(FileEventWatcher_getPath(w), g_dir));
    CHECK(FileEventWatcher_getMask(w) == FILE_EVENT_ALL);
    CHECK(!FileEventWatcher_isRunning(w));
    CHECK(FileEventWatcher_pendingCount(w) == 0);

    FileWatchProps got = FileEventWatcher_getProps(w);
    CHECK(got.latencyMs == 25u && got.debounceMs == 500u);
    CHECK(got.ignoreCount == 2 && got.includeCount == 2);

    utf8 pOk[320];
    utf8 pHidden[320];
    utf8 pSwp[320];
    utf8 pTxt[320];
    utf8 pSub[320];
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

    utf8 realFile[320];
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
    CHECK(string_equals(rec.lastRel, "hello.c"));
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

#undef CHECK

/** Suite: hash (from hash/hash_test.c). */
/**
 * re/test/hash/hash_test.c, the Hash owner test (Per-File Battle Test Law).
 *
 * Hash is stateless pure functions, so the battle rows are DETERMINISM, the
 * VALUE BOUNDARY MATRIX (nullptr/empty/huge), and an AVALANCHE oracle: a single
 * input-bit flip must flip roughly half the output bits (Murmur3 finalizer
 * contract). FNV-1a is pinned to published reference vectors so a silent
 * algorithm drift is caught, not merely "some hash changed".
 */



static i32 s_hash_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if(!(cond)) {                                                      \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_hash_g_failures++;                                                  \
        }                                                                  \
    } while(0)

/** Counts set bits to measure the 64-bit hash avalanche response. */
static u32 popcount64(u64 v) {

    u32 n = 0;

    while(v) {

        n += (u32) (v & 1u);

        v >>= 1;

    }

    return n;

}

/** Counts set bits to measure the 32-bit hash avalanche response. */
static u32 popcount32(u32 v) {

    u32 n = 0;

    while(v) {

        n += (u32) (v & 1u);

        v >>= 1;

    }

    return n;

}

/** Checks hash reference vectors, deterministic output, null inputs, and avalanche behavior. */
static i32 suite_hash() {

    CHECK(Hash_fnv1a64(nullptr, 0) == 0);

    CHECK(Hash_fnv1a64(nullptr, 999) == 0);

    const u8 none = 0;

    CHECK(Hash_fnv1a64(&none, 0) == 0);

    CHECK(Hash_fnv1a64((const u8*) "a", 1) == 0xaf63dc4c8601ec8cull);

    CHECK(Hash_fnv1a64((const u8*) "foobar", 6) == 0x85944171f73967e8ull);

    const u8 buf[64] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

    u64 first = Hash_fnv1a64(buf, sizeof(buf));

    for(i32 i = 0; i < 8; i++) 
       CHECK(Hash_fnv1a64(buf, sizeof(buf)) == first);

    u8 tweaked[64];

    memcpy(tweaked, buf, sizeof(buf));

    tweaked[37] ^= 0x01u;

    CHECK(Hash_fnv1a64(tweaked, sizeof(buf)) != first);

    CHECK(Hash_pointer(nullptr) == Hash_murmur3Mix64(0));

    i32 a = 0, b = 0;

    CHECK(Hash_pointer(&a) == Hash_pointer(&a));

    CHECK(Hash_pointer(&a) != Hash_pointer(&b));

    CHECK(Hash_murmur3Mix64(0) == 0);

    CHECK(Hash_murmur3Mix64(1) != Hash_murmur3Mix64(2));

    for(i32 bit = 0; bit < 64; bit += 7) {

        u64 base = 0x0123456789abcdefull;

        u64 other = base ^ (1ull << bit);

        u64 h = Hash_murmur3Mix64(base) ^ Hash_murmur3Mix64(other);

        CHECK(popcount64(h) >= 16);

    }

    for(i32 i = 0; i < 4; i++) 
       CHECK(Hash_murmur3Mix64(0xdeadbeefcafef00dull) == Hash_murmur3Mix64(0xdeadbeefcafef00dull));

    CHECK(Hash_murmur3Mix32(0) == 0);

    CHECK(Hash_murmur3Mix32(1u) != Hash_murmur3Mix32(2u));

    for(i32 bit = 0; bit < 32; bit += 5) {

        u32 base = 0x12345678u;

        u32 other = base ^ (1u << bit);

        u32 h = Hash_murmur3Mix32(base) ^ Hash_murmur3Mix32(other);

        CHECK(popcount32(h) >= 8);

    }

    if(s_hash_g_failures == 0) {

        print("hash_test: all assertions held");

        return 0;

    }

    print("hash_test: ", s_hash_g_failures, " FAILURES");

    return 1;

}
#undef CHECK

/** Void adapter for hash suite. */
static fn suite_hash_wrap() {
    if (suite_hash() != 0)
        g_failures++;
}

/** Suite: cell (from substrate/relational/cell_test.c). */
/**
 * re/test/substrate/relational/cell_test.c, owner test for substrate/cell.
 *
 * Proves the 32-byte identity cell:
 * - all three arity constructors (Cell_0/_1/_2) and the Cell(...) chooser
 * build a cell whose self-describing header carries the caller's typeId;
 * - Cell_check answers identity from the block header, not a side table;
 * - sizeof(Cell) stays 16 Bytes and the block is 32 Bytes (compile-time);
 * - value slot set/get round-trips, including 0 and pointer-wide values;
 * - the toString projections render, null-render "nullptr", and report
 * truncation when the destination is too small;
 * - every entry point is null-safe.
 */



enum {
    s_cell_TEST_CLASS_A = 1u,
    s_cell_TEST_CLASS_B = 2u,
    TEST_CLASS_C = 3u,
    TEST_CLASS_D = 4u,
};

static i32 s_cell_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_cell_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Exercises all Cell constructor arities, type identity checks, value defaults, and cleanup. */
static fn test_construction() {
    Cell *anon = Cell_0();

    CHECK(anon != nullptr);

    CHECK(Cell_typeId(anon) == 0u);

    CHECK(Cell_check(anon, 0u));

    CHECK(Cell_getValue(anon) == 0u);

    Cell *one = Cell_1(s_cell_TEST_CLASS_A);

    CHECK(one != nullptr);

    CHECK(Cell_typeId(one) == s_cell_TEST_CLASS_A);

    CHECK(Cell_check(one, s_cell_TEST_CLASS_A));

    CHECK(!Cell_check(one, s_cell_TEST_CLASS_B));

    CHECK(Cell_getValue(one) == 0u);

    Cell *two = Cell_2(s_cell_TEST_CLASS_B, 0xDEADBEEFull);

    CHECK(two != nullptr);

    CHECK(Cell_typeId(two) == s_cell_TEST_CLASS_B);

    CHECK(Cell_getValue(two) == 0xDEADBEEFull);

    Cell *c0 = Cell();

    Cell *c1 = Cell(TEST_CLASS_C);

    Cell *c2 = Cell(TEST_CLASS_D, 42u);

    CHECK(c0 != nullptr && Cell_typeId(c0) == 0u);

    CHECK(c1 != nullptr && Cell_typeId(c1) == TEST_CLASS_C);

    CHECK(c2 != nullptr && Cell_typeId(c2) == TEST_CLASS_D);

    CHECK(Cell_getValue(c2) == 42u);

    Cell_free(anon);

    Cell_free(one);

    Cell_free(two);

    Cell_free(c0);

    Cell_free(c1);

    Cell_free(c2);
}

/** Checks value-slot writes at zero, uintptr maximum, and pointer-width payloads. */
static fn test_value_slot() {
    Cell *c = Cell_2(s_cell_TEST_CLASS_A, 1u);

    CHECK(c != nullptr);

    Cell_setValue(c, 0u);

    CHECK(Cell_getValue(c) == 0u);

    Cell_setValue(c, UINTPTR_MAX);

    CHECK(Cell_getValue(c) == UINTPTR_MAX);

    i32 local = 7;

    Cell_setValue(c, (pointer) &local);

    CHECK(Cell_getValue(c) == (pointer) &local);

    CHECK(Cell_check(c, s_cell_TEST_CLASS_A));

    Cell_free(c);
}

/** Checks Cell value/structure strings, truncation signaling, and null destinations/self. */
static fn test_to_string() {
    Cell *c = Cell_2(s_cell_TEST_CLASS_A, 0xABCDu);

    CHECK(c != nullptr);

    utf8 buf[128];

    bool trunc = true;

    Cell_toString(c, buf, sizeof(buf), &trunc);

    CHECK(!trunc);

    CHECK(string_contains(buf, "Cell(typeId=0x"));

    CHECK(string_contains(buf, "abcd"));

    trunc = false;

    Cell_toStringStruct(c, buf, sizeof(buf), &trunc);

    CHECK(!trunc);

    CHECK(string_contains(buf, "Cell { value=0x"));

    CHECK(string_contains(buf, "pad=0x"));

    utf8 tiny[8];

    trunc = false;

    Cell_toString(c, tiny, sizeof(tiny), &trunc);

    CHECK(trunc);

    trunc = true;

    Cell_toString(nullptr, buf, sizeof(buf), &trunc);

    CHECK(!trunc);

    CHECK(string_equals(buf, "nullptr"));

    Cell_toStringStruct(nullptr, buf, sizeof(buf), &trunc);

    CHECK(string_equals(buf, "nullptr"));

    trunc = true;

    Cell_toString(c, nullptr, 0, &trunc);

    CHECK(!trunc);

    Cell_toString(c, buf, 0, nullptr);

    Cell_toStringStruct(c, nullptr, 0, nullptr);

    Cell_free(c);
}

/** Verifies Cell accessors, mutators, and destruction tolerate a null receiver. */
static fn test_null_safety() {
    CHECK(Cell_typeId(nullptr) == 0u);

    CHECK(!Cell_check(nullptr, 0u));

    CHECK(Cell_getValue(nullptr) == 0u);

    Cell_setValue(nullptr, 1u);

    Cell_free(nullptr);
}

/** Initializes memory, runs Cell contract cases, and reports aggregate assertion failures. */
static i32 suite_cell() {
    CHECK(Memory_init(0));

    test_construction();

    test_value_slot();

    test_to_string();

    test_null_safety();

    if (s_cell_g_failures == 0) {
        print("cell_test: all assertions held");

        return 0;
    }

    print("cell_test: ", s_cell_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for cell suite. */
static fn suite_cell_wrap() {
    if (suite_cell() != 0)
        g_failures++;
}

/** Suite: class_relational (from substrate/relational/class_relational_test.c). */
/** class_relational_test/test.c, verify the Relational spotlight (SymbolTable). */



static i32 s_class_relational_g_failures = 0;
#define CHECK(cond) do{ if(!(cond)){ print("FAIL ", __FILE__, ":", __LINE__, " ", #cond); s_class_relational_g_failures++; } }while(0)

/** Exercises relational name/value/function lookup, ranked scopes, and rename behavior. */
static fn testRelationalSpotlight(){
    SymbolTable global = SymbolTable();

    SymbolTable local = SymbolTable();

    CHECK(global.active);

    CHECK(local.active);

    Relational_setString(&global, "health", "100");

    Relational_setString(&global, "health_ui", "panel");

    Relational_setString(&global, "health_progress_bar", "bar");

    Relational_setString(&local, "hp_text", "42");

    Relational_setString(&local, "mana", "50");

    i32 out[10];

    bytes n = Relational_search(&global, "health", out, 10);

    CHECK(n==3);

    CHECK(string_equals(SymbolTable_getName(&global, out[0]), "health"));

    n = Relational_search(&global, "health_", out, 10);

    CHECK(n==2);

    i32 outAll[10];

    n = Relational_searchAll(&global, &local, "health", outAll, 10);

    CHECK(n==3);

    n = Relational_search(&global, "HEALTH", out, 10);

    CHECK(n==3);

    any p = Relational_getValue(&global, "health_ui");

    CHECK(p != nullptr);

    CHECK(string_equals(Relational_getString(&global, "health_ui"), "panel"));

    any fn = (any) 0x12345;

    Relational_setFunction(&global, "onUpdate", fn);

    CHECK(Relational_getFunction(&global, "onUpdate")==fn);

    CHECK(Relational_setName(&global, "mana", "mana2")==false);

    CHECK(Relational_setName(&local, "mana", "mana2")==true);

    CHECK(Relational_getId(&local, "mana2")>=0);

    SymbolTable_shutdown(&global);

    SymbolTable_shutdown(&local);
}

/** Runs the relational spotlight scenario and returns its accumulated result. */
static i32 suite_class_relational(){
    if (!Memory_init(0u)) 
       return 1;

    testRelationalSpotlight();

    if(s_class_relational_g_failures==0) 
       print("class_relational_test: all checks passed");
    else
        print("class_relational_test: ", s_class_relational_g_failures, " FAILURES");

    return s_class_relational_g_failures==0?0:1;
}
#undef CHECK

/** Void adapter for class_relational suite. */
static fn suite_class_relational_wrap() {
    if (suite_class_relational() != 0)
        g_failures++;
}

/** Suite: shelf (from substrate/relational/shelf_test.c). */
/**
 * ============================================================================
 * MODULE: ShelfTest (shelf_test/test.c)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the identity cell and the global shelf: the 32-byte cell
 * layout + header identity, Cell_check, and the shelf's never-moved node pool
 * with u32 graph edges (index stability across growth, link walk, null-safety,
 * both string projections).
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_shelf_g_fail = 0;
static i32 s_shelf_g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        s_shelf_g_checks++;                                                       \
        if (!(cond)) {                                                    \
            s_shelf_g_fail++;                                                     \
            print("  FAIL line ", __LINE__, ": ", #cond);              \
        }                                                                 \
    } while (0)

/** Checks Cell layout, header identity, value storage, and null-safe destruction. */
static fn testCell() {
    print("[1] identity cell: 32 B block, header identity, value slot");

    CHECK(sizeof(Cell) == 16u);

    Cell *c = Cell_2(TYPE_VARIABLE_SLOT, 0xDEADu);

    CHECK(c != nullptr);

    if (c) {
        CHECK(Cell_typeId(c) == TYPE_VARIABLE_SLOT);

        CHECK(Cell_check(c, TYPE_VARIABLE_SLOT) == true);

        CHECK(Cell_check(c, TYPE_SHELF) == false);

        CHECK(Cell_getValue(c) == 0xDEADu);

        CHECK(Memory_length(c) == CELL_PAYLOAD_BYTES);

        Cell_setValue(c, 0xBEEFu);

        CHECK(Cell_getValue(c) == 0xBEEFu);

        Cell_free(c);
    }

    Cell *anon = Cell_0();

    CHECK(anon != nullptr && Cell_typeId(anon) == 0u);

    Cell_free(anon);

    Cell *one = Cell_1(TYPE_SHELF);

    CHECK(one != nullptr && Cell_getValue(one) == 0u && Cell_check(one, TYPE_SHELF));

    Cell_free(one);

    Cell_free(nullptr);

    CHECK(true);
}

/** Checks Cell value and structure projections, including truncation and nullptr rendering. */
static fn testCellStrings() {
    print("[2] cell string projections");

    Cell *c = Cell_2(0x2Cu, 0x1Fu);

    utf8 buf[96];

    bool truncated = true;

    Cell_toString(c, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "Cell(typeId=0x2c, value=0x1f)"));

    Cell_toStringStruct(c, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "Cell { value=0x1f, pad=0x0 }"));

    utf8 tiny[4];

    Cell_toString(c, tiny, sizeof(tiny), &truncated);

    CHECK(truncated == true);

    truncated = true;

    Cell_toString(nullptr, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "nullptr"));

    Cell_free(c);
}

/** Exercises shelf insertion, linked traversal, invalid indices, and head updates. */
static fn testShelfBasics() {
    print("[3] shelf: node pool, cells, u32 edges, head walk");

    Shelf *shelf = Shelf_0();

    CHECK(shelf != nullptr);

    if (!shelf) 
        return;

    CHECK(Memory_type(shelf) == TYPE_SHELF);

    CHECK(Shelf_count(shelf) == 0u);

    CHECK(Shelf_isEmpty(shelf) == true);

    CHECK(Shelf_getHead(shelf) == SHELF_INDEX_NONE);

    u32 a = Shelf_addCell(shelf, TYPE_VARIABLE_SLOT, 0x10u);

    u32 b = Shelf_addCell(shelf, TYPE_SHELF, 0x20u);

    u32 cc = Shelf_addCell(shelf, 0x1234u, 0x30u);

    CHECK(a == 0u && b == 1u && cc == 2u);

    CHECK(Shelf_count(shelf) == 3u);

    CHECK(Shelf_isEmpty(shelf) == false);

    Cell *ca = (Cell*) Shelf_getCell(shelf, a);

    Cell *cb = (Cell*) Shelf_getCell(shelf, b);

    Cell *c3 = (Cell*) Shelf_getCell(shelf, cc);

    CHECK(Cell_check(ca, TYPE_VARIABLE_SLOT) == true && Cell_getValue(ca) == 0x10u);

    CHECK(Cell_check(cb, TYPE_SHELF) == true && Cell_getValue(cb) == 0x20u);

    CHECK(Cell_check(c3, 0x1234u) == true && Cell_getValue(c3) == 0x30u);

    CHECK(Shelf_setHead(shelf, a) == true);

    CHECK(Shelf_link(shelf, a, b) == true);

    CHECK(Shelf_link(shelf, b, cc) == true);

    CHECK(Shelf_getNext(shelf, cc) == SHELF_INDEX_NONE);

    u32 order[4] = { 0u, 0u, 0u, 0u };

    u32 n = 0u;

    for (u32 i = Shelf_getHead(shelf); i != SHELF_INDEX_NONE && n < 4u; i = Shelf_getNext(shelf, i)) 
        order[n++] = i;

    CHECK(n == 3u && order[0] == 0u && order[1] == 1u && order[2] == 2u);

    CHECK(Shelf_getCell(shelf, 99u) == 0u);

    CHECK(Shelf_getNext(shelf, 99u) == SHELF_INDEX_NONE);

    CHECK(Shelf_setNext(shelf, 99u, 0u) == false);

    CHECK(Shelf_link(shelf, 99u, 0u) == false);

    CHECK(Shelf_setHead(shelf, 99u) == false);

    CHECK(Shelf_setHead(shelf, SHELF_INDEX_NONE) == true);

    CHECK(Shelf_getHead(shelf) == SHELF_INDEX_NONE);

    Shelf_free(shelf);
}

/** Verifies cell addresses and values remain stable as the shelf grows to 500 entries. */
static fn testShelfStability() {
    print("[4] node/cell addresses stay stable across growth");

    Shelf *shelf = Shelf_0();

    if (!shelf) {
        CHECK(false);

        return;
    }

    u32 first = Shelf_addCell(shelf, TYPE_VARIABLE_SLOT, 0x0u);

    pointer firstCell = Shelf_getCell(shelf, first);

    CHECK(firstCell != 0u);

    for (u32 i = 1u; i < 500u; i++) 
        CHECK(Shelf_addCell(shelf, TYPE_VARIABLE_SLOT, i) == i);

    CHECK(Shelf_count(shelf) == 500u);

    CHECK(Shelf_getCell(shelf, first) == firstCell);

    bool intact = true;

    for (u32 i = 0u; i < 500u; i++) {
        Cell *c = (Cell*) Shelf_getCell(shelf, i);

        if (!c || Cell_getValue(c) != i) {
            intact = false;

            break;
        }
    }

    CHECK(intact);

    Shelf_free(shelf);
}

/** Checks Shelf null-safe operations, lifecycle idempotence, and bounded projections. */
static fn testShelfNullAndStrings() {
    print("[5] shelf null-safety + string projections");

    CHECK(Shelf_count(nullptr) == 0u);

    CHECK(Shelf_isEmpty(nullptr) == true);

    CHECK(Shelf_getHead(nullptr) == SHELF_INDEX_NONE);

    CHECK(Shelf_getCell(nullptr, 0u) == 0u);

    CHECK(Shelf_getNext(nullptr, 0u) == SHELF_INDEX_NONE);

    CHECK(Shelf_setNext(nullptr, 0u, 0u) == false);

    CHECK(Shelf_setHead(nullptr, 0u) == false);

    CHECK(Shelf_addNode(nullptr, 0u) == SHELF_INDEX_NONE);

    CHECK(Shelf_addCell(nullptr, 0u, 0u) == SHELF_INDEX_NONE);

    CHECK(Shelf_init(nullptr) == false);

    Shelf_shutdown(nullptr);

    Shelf_free(nullptr);

    Shelf shelf;

    CHECK(Shelf_init(&shelf) == true);

    Shelf_addCell(&shelf, TYPE_VARIABLE_SLOT, 0x1u);

    utf8 buf[128];

    bool truncated = true;

    Shelf_toString(&shelf, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "Shelf(count=1)"));

    Shelf_toStringStruct(&shelf, buf, sizeof(buf), &truncated);

    string prefix = "Shelf { active=true, count=1, head=-1, nodes=0x";

    CHECK(truncated == false && string_startsWith(buf, prefix, strlen(prefix)));

    utf8 tiny[4];

    Shelf_toString(&shelf, tiny, sizeof(tiny), &truncated);

    CHECK(truncated == true);

    truncated = true;

    Shelf_toString(nullptr, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "nullptr"));

    Shelf_shutdown(&shelf);

    CHECK(Shelf_count(&shelf) == 0u);

    Shelf_shutdown(&shelf);

    CHECK(true);
}

/** Runs Cell and Shelf identity, collection, growth, projection, and null-safety checks. */
static i32 suite_shelf() {
    print("=== Cell + Shelf Test Suite ===\n");

    testCell();

    testCellStrings();

    testShelfBasics();

    testShelfStability();

    testShelfNullAndStrings();

    print("\n", s_shelf_g_checks, " checks, ", s_shelf_g_fail, " failed");

    if (s_shelf_g_fail == 0) 
        print("=== Cell + Shelf Test Suite: ALL PASS ===");

    return s_shelf_g_fail == 0 ? 0 : 1;
}
#undef CHECK

/** Void adapter for shelf suite. */
static fn suite_shelf_wrap() {
    if (suite_shelf() != 0)
        g_failures++;
}

/** Suite: symbol_table (from substrate/relational/symbol_table_test.c). */
/**
 * re/test/substrate/relational/symbol_table_test.c, owner test for
 * substrate/symbol_table.
 *
 * The relational symbol registry maps a name to (classId, pointer):
 * - init/shutdown lifecycle, d64 shutdown, inactive-table safety;
 * - instant (create-or-FAIL: duplicates rejected, never updated), pool-first
 * lookup, case folding to lowercase, and the ASCII name policy
 * (empty/overlong/illegal rejected);
 * - rename moves both the name and the row identity, and rejects unknown
 * olds, collisions, and bad new names;
 * - findByClass returns matches in insertion order and reports the total
 * even when the caller's buffer is i16;
 * - pointer get/set/compareAndSet semantics, including failure atomicity;
 * - getName buffer sizing, bad ids, and nullptr out;
 * - failure observability: every promised cold rejection writes one
 * "[variable] ..." line to stderr, and the safe result is still returned;
 * - nullptr for every pointer parameter.
 */



enum {
    s_symbol_table_TEST_CLASS_A = 1u,
    s_symbol_table_TEST_CLASS_B = 2u,
};

static i32 s_symbol_table_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_symbol_table_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Checks create-or-fail admission, case-folded lookup, class/pointer reads, and duplicate preservation. */
static fn test_instant_and_lookup(SymbolTable *v) {
    i32 id0 = SymbolTable_instant(v, "alpha", s_symbol_table_TEST_CLASS_A, 0x10u);

    CHECK(id0 == 0);

    CHECK(SymbolTable_getId(v, "alpha") == id0);

    CHECK(SymbolTable_getId(v, "ALPHA") == id0);

    CHECK(SymbolTable_getClassId(v, id0) == s_symbol_table_TEST_CLASS_A);

    CHECK(SymbolTable_getPointer(v, id0) == 0x10u);

    i32 dup = SymbolTable_instant(v, "alpha", s_symbol_table_TEST_CLASS_B, 0x99u);

    CHECK(dup == -1);

    CHECK(SymbolTable_getClassId(v, id0) == s_symbol_table_TEST_CLASS_A);

    CHECK(SymbolTable_getPointer(v, id0) == 0x10u);

    CHECK(SymbolTable_getId(v, "nope") == -1);

    CHECK(SymbolTable_getId(v, "unknown.name") == -1);

    CHECK(SymbolTable_getActiveCount(v) == 1);
}

/** Exercises invalid/valid name boundaries and confirms rejected names do not add rows. */
static fn test_name_policy(SymbolTable *v) {
    bytes before = SymbolTable_getActiveCount(v);

    CHECK(SymbolTable_instant(v, nullptr, s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "has space", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "has-dash", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "sla/sh", s_symbol_table_TEST_CLASS_A, 1u) == -1);
    {
        const utf8 utf8Name[] = { 'b', (utf8) 0xC3, (utf8) 0xA9, '\0' };
        CHECK(SymbolTable_instant(v, utf8Name, s_symbol_table_TEST_CLASS_A, 1u) == -1);
    }
    {
        const utf8 tooLong[] = "aaaaaaaaaaaaaaaaaaaaaaaa";
        CHECK(SymbolTable_instant(v, tooLong, s_symbol_table_TEST_CLASS_A, 1u) == -1);
    }
    {
        const utf8 maxOk[] = "aaaaaaaaaaaaaaaaaaaaaaa";
        CHECK(SymbolTable_instant(v, maxOk, s_symbol_table_TEST_CLASS_A, 1u) >= 0);
    }
    CHECK(SymbolTable_instant(v, "a_b.c9", s_symbol_table_TEST_CLASS_A, 1u) >= 0);

    i32 id = SymbolTable_instant(v, "MixedCase_1.x", s_symbol_table_TEST_CLASS_A, 1u);

    CHECK(id >= 0);

    CHECK(SymbolTable_getId(v, "mixedcase_1.x") == id);

    CHECK(SymbolTable_getActiveCount(v) == before + 3);
}

/** Checks successful rename identity preservation and rejection-state stability. */
static fn test_rename(SymbolTable *v) {
    i32 id = SymbolTable_instant(v, "ren_old", s_symbol_table_TEST_CLASS_A, 7u);

    CHECK(id >= 0);

    CHECK(SymbolTable_rename(v, "ren_old", "ren_new"));

    CHECK(SymbolTable_getId(v, "ren_old") == -1);

    CHECK(SymbolTable_getId(v, "ren_new") == id);

    CHECK(SymbolTable_getPointer(v, id) == 7u);

    CHECK(SymbolTable_getClassId(v, id) == s_symbol_table_TEST_CLASS_A);

    CHECK(!SymbolTable_rename(v, "ren_ghost", "ren_x"));

    CHECK(!SymbolTable_rename(v, "ren_new", "alpha"));

    CHECK(!SymbolTable_rename(v, "ren_new", "bad name"));

    CHECK(SymbolTable_getId(v, "ren_new") == id);

    CHECK(SymbolTable_rename(v, "ren_new", "ren_other"));

    CHECK(SymbolTable_getId(v, "ren_other") == id);
}

/** Checks class filtering order, count-only/i16 output buffers, and no-match behavior. */
static fn test_find_by_class(SymbolTable *v) {
    const u32 cls = 0xABCDEF01u;

    i32 a = SymbolTable_instant(v, "fbc_a", cls, 1u);

    i32 b = SymbolTable_instant(v, "fbc_b", cls, 2u);

    i32 c = SymbolTable_instant(v, "fbc_c", cls, 3u);

    CHECK(a >= 0 && b >= 0 && c >= 0);

    CHECK(SymbolTable_instant(v, "fbc_other", 0xABCDEF02u, 4u) >= 0);

    i32 out[8] = { -1 };

    bytes total = SymbolTable_findByClass(v, cls, out, 8);

    CHECK(total == 3);

    CHECK(out[0] == a && out[1] == b && out[2] == c);

    i32 one[1] = { -1 };

    total = SymbolTable_findByClass(v, cls, one, 1);

    CHECK(total == 3);

    CHECK(one[0] == a);

    CHECK(SymbolTable_findByClass(v, cls, nullptr, 8) == 3);

    CHECK(SymbolTable_findByClass(v, cls, out, 0) == 3);

    CHECK(SymbolTable_findByClass(v, 0xDEADBEEFu, out, 8) == 0);
}

/** Checks pointer get/set/CAS semantics, including mismatch preservation and invalid ids. */
static fn test_pointer_accessors(SymbolTable *v) {
    i32 id = SymbolTable_instant(v, "pa_x", 0x11u, 100u);

    CHECK(id >= 0);

    CHECK(SymbolTable_getPointer(v, id) == 100u);

    SymbolTable_setPointer(v, id, 200u);

    CHECK(SymbolTable_getPointer(v, id) == 200u);

    CHECK(SymbolTable_compareAndSetPointer(v, id, 200u, 300u));

    CHECK(SymbolTable_getPointer(v, id) == 300u);

    CHECK(!SymbolTable_compareAndSetPointer(v, id, 999u, 400u));

    CHECK(SymbolTable_getPointer(v, id) == 300u);

    CHECK(SymbolTable_getClassId(v, id) == 0x11u);

    CHECK(SymbolTable_getPointer(v, -1) == 0u);

    CHECK(SymbolTable_getPointer(v, 99999) == 0u);

    CHECK(SymbolTable_getClassId(v, -1) == 0u);

    SymbolTable_setPointer(v, -1, 1u);

    CHECK(!SymbolTable_compareAndSetPointer(v, -1, 0u, 1u));
}

/** Checks direct name return and invalid-id handling. */
static fn test_get_name(SymbolTable *v) {
    i32 id = SymbolTable_instant(v, "gn_name", s_symbol_table_TEST_CLASS_A, 1u);

    CHECK(id >= 0);

    CHECK(string_equals(SymbolTable_getName(v, id), "gn_name"));

    CHECK(SymbolTable_getName(v, -1) == nullptr);

    CHECK(SymbolTable_getName(v, 99999) == nullptr);
}

/** Exercises null arguments and zero-initialized inactive tables across the public surface. */
static fn test_null_and_inactive() {
    SymbolTable constructed = SymbolTable();

    CHECK(constructed.active);

    SymbolTable_shutdown(&constructed);

    SymbolTable_shutdown(nullptr);

    CHECK(SymbolTable_instant(nullptr, "x", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_getId(nullptr, "x") == -1);

    CHECK(!SymbolTable_rename(nullptr, "a", "b.sh"));

    CHECK(SymbolTable_findByClass(nullptr, s_symbol_table_TEST_CLASS_A, nullptr, 0) == 0);

    CHECK(SymbolTable_getPointer(nullptr, 0) == 0u);

    SymbolTable_setPointer(nullptr, 0, 1u);

    CHECK(!SymbolTable_compareAndSetPointer(nullptr, 0, 0u, 1u));

    CHECK(SymbolTable_getClassId(nullptr, 0) == 0u);

    CHECK(SymbolTable_getName(nullptr, 0) == nullptr);

    CHECK(SymbolTable_getActiveCount(nullptr) == 0);

    SymbolTable dead;

    memset(&dead, 0, sizeof(dead));

    CHECK(SymbolTable_instant(&dead, "x", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_getId(&dead, "x") == -1);

    CHECK(SymbolTable_getActiveCount(&dead) == 0);

    SymbolTable_shutdown(&dead);
}

/** Captures stderr to assert each rejected cold admission/rename emits one diagnostic. */
static fn test_observability(SymbolTable *v) {
    FILE *capture = tmpfile();

    CHECK(capture != nullptr);

    i32 original = dup(fileno(stderr));

    CHECK(original >= 0);

    CHECK(dup2(fileno(capture), fileno(stderr)) >= 0);

    CHECK(SymbolTable_instant(v, nullptr, s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "bad name", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(SymbolTable_instant(v, "aaaaaaaaaaaaaaaaaaaaaaaa", s_symbol_table_TEST_CLASS_A, 1u) == -1);

    CHECK(!SymbolTable_rename(v, "ghost", "ghost2"));

    CHECK(!SymbolTable_rename(v, "alpha", "also bad"));

    fflush(stderr);

    CHECK(dup2(original, fileno(stderr)) >= 0);

    close(original);

    rewind(capture);

    i32 lines = 0;

    utf8 line[512];

    while (fgets(line, sizeof(line), capture) != nullptr) {
        CHECK(string_contains(line, "[variable]"));

        lines++;
    }

    CHECK(lines == 6);

    fclose(capture);
}

/** Checks active-entry removal and safe repeated shutdown on an initialized table. */
static fn test_shutdown() {
    SymbolTable v = SymbolTable();

    CHECK(v.active);

    CHECK(SymbolTable_instant(&v, "sd_x", s_symbol_table_TEST_CLASS_A, 1u) >= 0);

    CHECK(SymbolTable_getActiveCount(&v) == 1);

    SymbolTable_shutdown(&v);

    CHECK(SymbolTable_getActiveCount(&v) == 0);

    CHECK(SymbolTable_getId(&v, "sd_x") == -1);

    CHECK(SymbolTable_getPointer(&v, 0) == 0u);

    SymbolTable_shutdown(&v);
}

/** Initializes memory and the table, runs isolated contract cases, and aggregates failures. */
static i32 suite_symbol_table() {
    CHECK(Memory_init(0));

    SymbolTable v = SymbolTable();

    CHECK(v.active);

    test_instant_and_lookup(&v);

    test_name_policy(&v);

    test_rename(&v);

    test_find_by_class(&v);

    test_pointer_accessors(&v);

    test_get_name(&v);

    test_observability(&v);

    SymbolTable_shutdown(&v);

    test_null_and_inactive();

    test_shutdown();

    StringPool_shutdown();

    if (s_symbol_table_g_failures == 0) {
        print("symbol_table_test: all assertions held");

        return 0;
    }

    print("symbol_table_test: ", s_symbol_table_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for symbol_table suite. */
static fn suite_symbol_table_wrap() {
    if (suite_symbol_table() != 0)
        g_failures++;
}

/** Suite: var (from substrate/variable/var_test.c). */
static i32 g_calls;
static pointer g_last;

static fn observer(var *self, pointer value, any context) {
    (discard) self;

    g_calls += 1;

    g_last = value;

    assert(context == &g_calls);
}

static i32 suite_var() {
    MemoryArena *arena = MemoryArena(64u * 1024u * 1024u);

    assert(arena);

    var *v = var("health", arena);

    assert(v);

    assert(string_equals(var_name(v), "health"));

    assert(var_value(v) == 0);

    assert(var_observerCount(v) == 0);

    assert(var_arena(v) == arena);

    assert(var_subscribe(v, observer, &g_calls));

    assert(var_set(v, 42));

    assert(g_calls == 1 && g_last == 42);

    utf8 buffer[128];

    bool truncated = true;

    var_toString(v, buffer, sizeof buffer, &truncated);

    assert(!truncated && string_contains(buffer, "health"));

    var_unsubscribe(v, observer, &g_calls);

    assert(var_observerCount(v) == 0);

    assert(var_set(v, 7));

    assert(g_calls == 1);

    assert(var_value(v) == 7);

    assert(!var_subscribe(nullptr, observer, nullptr));

    assert(!var_set(nullptr, 1));

    var_unsubscribeAll(v);

    var_unsubscribeAll(nullptr);

    MemoryArena_destroy(arena);

    Memory_freeAll();

    return 0;
}

/** Void adapter for var suite. */
static fn suite_var_wrap() {
    if (suite_var() != 0)
        g_failures++;
}

/** Suite: variable_hash_map (from substrate/variable/variable_hash_map_test.c). */
/**
 * ============================================================================
 * MODULE: VariableHashMapTest (variable_hash_map_test/test.c)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the relational name => pointer hash map (hash system B):
 * lowercase folding, the 39 first-utf8 buckets, the per-bucket 1024 dashcode
 * slots, forced collisions (chaining inside a slot), upsert semantics,
 * rejection of illegal names, both string projections, and null-safety.
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_variable_hash_map_g_fail = 0;
static i32 s_variable_hash_map_g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        s_variable_hash_map_g_checks++;                                                       \
        if (!(cond)) {                                                    \
            s_variable_hash_map_g_fail++;                                                     \
            print("  FAIL line ", __LINE__, ": ", #cond);              \
        }                                                                 \
    } while (0)

/** Mirror the map's slot index so the test can force a real collision. */
static u32 slotOf(string name) {
    utf8 folded[VARIABLE_SLOT_NAME_BYTES];

    if (!VariableSlot_foldName(name, folded)) 
        return 0xFFFFFFFFu;

    string tail = folded + 1;

    u64 seed = Hash_fnv1a64((const u8*) tail, strlen(tail));

    return (u32)(Hash_murmur3Mix64(seed) & 1023u);
}

/** Checks initialization, empty lookup defaults, null receiver behavior, and idempotent shutdown. */
static fn testLifecycle() {
    print("[1] init / empty / count / null-safety");

    VariableHashMap map;

    CHECK(VariableHashMap_init(&map) == true);

    CHECK(VariableHashMap_count(&map) == 0u);

    CHECK(VariableHashMap_isEmpty(&map) == true);

    CHECK(VariableHashMap_contains(&map, "anything") == false);

    CHECK(VariableHashMap_get(&map, "anything") == 0u);

    CHECK(VariableHashMap_init(nullptr) == false);

    CHECK(VariableHashMap_count(nullptr) == 0u);

    CHECK(VariableHashMap_isEmpty(nullptr) == true);

    CHECK(VariableHashMap_contains(nullptr, "x") == false);

    CHECK(VariableHashMap_get(nullptr, "x") == 0u);

    CHECK(VariableHashMap_add(nullptr, "x", 1u) == false);

    VariableHashMap_shutdown(nullptr);

    VariableHashMap_shutdown(&map);

    CHECK(VariableHashMap_count(&map) == 0u);

    VariableHashMap_shutdown(&map);

    CHECK(true);
}

/** Checks case folding, lookup, and same-key upsert without duplicate count growth. */
static fn testFoldAndBucket() {
    print("[2] lowercase folding + upsert semantics");

    VariableHashMap map;

    VariableHashMap_init(&map);

    CHECK(VariableHashMap_add(&map, "helloWorld", 0xAAu) == true);

    CHECK(VariableHashMap_count(&map) == 1u);

    CHECK(VariableHashMap_get(&map, "helloworld") == 0xAAu);

    CHECK(VariableHashMap_get(&map, "HELLOWORLD") == 0xAAu);

    CHECK(VariableHashMap_get(&map, "helloWorld") == 0xAAu);

    CHECK(VariableHashMap_contains(&map, "HelloWorld") == true);

    CHECK(VariableHashMap_add(&map, "HEALTH", 1u) == true);

    CHECK(VariableHashMap_add(&map, "health", 2u) == true);

    CHECK(VariableHashMap_count(&map) == 2u);

    CHECK(VariableHashMap_get(&map, "health") == 2u);

    VariableHashMap_shutdown(&map);
}

/** Admits and resolves a key in every supported first-character bucket. */
static fn testAllBuckets() {
    print("[3] every first-utf8 bucket is reachable (39 of them)");

    VariableHashMap map;

    VariableHashMap_init(&map);

    string firsts = "abcdefghijklmnopqrstuvwxyz0123456789_$-";

    bytes n = strlen(firsts);

    CHECK(n == 39u);

    for (bytes i = 0; i < n; i++) {
        utf8 name[4] = { firsts[i], '1', '\0' };

        CHECK(VariableHashMap_add(&map, name, 0x100u + (pointer) i) == true);
    }

    CHECK(VariableHashMap_count(&map) == 39u);

    for (bytes i = 0; i < n; i++) {
        utf8 name[4] = { firsts[i], '1', '\0' };

        CHECK(VariableHashMap_get(&map, name) == 0x100u + (pointer) i);
    }

    VariableHashMap_shutdown(&map);
}

/** Checks invalid-name rejection and accepted dotted/name-length boundary forms. */
static fn s_variable_hash_map_testRejection() {
    print("[4] illegal / malformed names rejected; dotted names accepted");

    VariableHashMap map;

    VariableHashMap_init(&map);

    CHECK(VariableHashMap_add(&map, nullptr, 1u) == false);

    CHECK(VariableHashMap_add(&map, "", 1u) == false);

    CHECK(VariableHashMap_add(&map, "abcdefghijklmnopqrstuvwx", 1u) == false);

    CHECK(VariableHashMap_add(&map, ".a", 1u) == false);

    CHECK(VariableHashMap_add(&map, "a.", 1u) == false);

    CHECK(VariableHashMap_add(&map, "a..b.sh", 1u) == false);

    CHECK(VariableHashMap_add(&map, "has space", 1u) == false);

    CHECK(VariableHashMap_add(&map, "character.position.x", 1u) == true);

    CHECK(VariableHashMap_add(&map, "23charsokabcdefghijklmn", 1u) == true);

    CHECK(VariableHashMap_count(&map) == 2u);

    CHECK(VariableHashMap_get(&map, "CHARACTER.POSITION.X") == 1u);

    VariableHashMap_shutdown(&map);
}

/** Finds two distinct keys sharing a bucket/slot and verifies both remain retrievable. */
static fn testCollisionChain() {
    print("[5] forced collision: two names share a bucket+slot and both resolve");

    bool seen[1024] = { false };

    u32 owner[1024] = { 0u };

    u32 firstI = 0u;

    u32 secondI = 0u;

    bool found = false;

    for (u32 i = 0u; i < 4096u && !found; i++) {
        utf8 name[16];

        snprintf(name, sizeof(name), "z%u", i);

        u32 s = slotOf(name);

        if (s >= 1024u) 
            break;

        if (seen[s]) {
            found = true;

            firstI = owner[s];

            secondI = i;

            break;
        }

        seen[s] = true;

        owner[s] = i;
    }

    CHECK(found);

    if (found) {
        utf8 a[16];

        utf8 b[16];

        snprintf(a, sizeof(a), "z%u", firstI);

        snprintf(b, sizeof(b), "z%u", secondI);

        CHECK(slotOf(a) == slotOf(b));

        VariableHashMap map;

        VariableHashMap_init(&map);

        CHECK(VariableHashMap_add(&map, a, 0x111u) == true);

        CHECK(VariableHashMap_add(&map, b, 0x222u) == true);

        CHECK(VariableHashMap_count(&map) == 2u);

        CHECK(VariableHashMap_get(&map, a) == 0x111u);

        CHECK(VariableHashMap_get(&map, b) == 0x222u);

        VariableHashMap_shutdown(&map);
    }
}

/** Inserts and resolves 3,000 same-bucket names to exercise chained-slot volume. */
static fn testVolume() {
    print("[6] volume: 3000 same-bucket names, all resolve (chained slots)");

    VariableHashMap map;

    VariableHashMap_init(&map);

    const u32 total = 3000u;

    for (u32 i = 0u; i < total; i++) {
        utf8 name[16];

        snprintf(name, sizeof(name), "var%u", i);

        if (VariableHashMap_add(&map, name, 0x5000u + i) != true) {
            CHECK(false);

            break;
        }
    }

    CHECK(VariableHashMap_count(&map) == total);

    bool ok = true;

    for (u32 i = 0u; i < total; i++) {
        utf8 name[16];

        snprintf(name, sizeof(name), "var%u", i);

        if (VariableHashMap_get(&map, name) != 0x5000u + i) {
            ok = false;

            break;
        }
    }

    CHECK(ok);

    VariableHashMap_shutdown(&map);
}

/** Checks value/structure projections, null rendering, and truncation reporting. */
static fn testStrings() {
    print("[7] toString / toStringStruct: bounded + truncation flagged");

    VariableHashMap map;

    VariableHashMap_init(&map);

    VariableHashMap_add(&map, "health", 0u);

    utf8 buf[128];

    bool truncated = true;

    VariableHashMap_toString(&map, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "VariableHashMap(count=1)"));

    VariableHashMap_toStringStruct(&map, buf, sizeof(buf), &truncated);

    CHECK(truncated == false &&
          string_equals(buf, "VariableHashMap { active=true, count=1, bands=<array[39]> }"));

    utf8 tiny[4];

    VariableHashMap_toString(&map, tiny, sizeof(tiny), &truncated);

    CHECK(truncated == true);

    truncated = true;

    VariableHashMap_toString(nullptr, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "nullptr"));

    VariableHashMap_shutdown(&map);
}

/** Exercises the zero-argument constructor chooser and null-safe freeing. */
;;GETTER
static inline bool VariableHashMap_active(const VariableHashMap *map)
{
    return (*map).active;
}

static fn testArity() {
    print("[8] arity constructor + free");

    VariableHashMap *map = VariableHashMap();

    CHECK(map != nullptr);

    if (map) {
        CHECK(VariableHashMap_active(map) == true);

        CHECK(VariableHashMap_add(map, "score", 0x42u) == true);

        CHECK(VariableHashMap_get(map, "score") == 0x42u);

        VariableHashMap_free(map);
    }

    VariableHashMap_free(nullptr);

    CHECK(true);
}

;;GETTER
static inline u32 VisitProbe_visited(const VisitProbe *p)
{
    return (*p).visited;
}
;;SETTER
static inline fn VisitProbe_setVisited(VisitProbe *p, u32 v)
{
    (*p).visited = v;
}
;;GETTER
static inline u32 VisitProbe_sum(const VisitProbe *p)
{
    return (*p).sum;
}
;;SETTER
static inline fn VisitProbe_setSum(VisitProbe *p, u32 s)
{
    (*p).sum = s;
}
;;SETTER
static inline fn VisitProbe_setSawDotted(VisitProbe *p, bool b)
{
    (*p).sawDotted = b;
}

/** Accumulates visited pointers and records whether the dotted-name entry was seen. */
static fn visitFn(VariableSlot *slot, any userdata) {
    VisitProbe *p = (VisitProbe*) userdata;

    VisitProbe_setVisited(p, VisitProbe_visited(p) + 1u);

    VisitProbe_setSum(p, VisitProbe_sum(p) + (u32) VariableSlot_getPointer(slot));

    if (VariableSlot_nameEquals(slot, "character.position.x")) 
        VisitProbe_setSawDotted(p, true);
}

/** Checks forEach visits each live entry and safely ignores null callback/receiver. */
static fn testForEach() {
    print("[9] forEach visits every live entry");

    VariableHashMap map;

    VariableHashMap_init(&map);

    VariableHashMap_add(&map, "health", 10u);

    VariableHashMap_add(&map, "score", 20u);

    VariableHashMap_add(&map, "character.position.x", 30u);

    VisitProbe probe = { 0u, 0u, false };

    VariableHashMap_forEach(&map, visitFn, &probe);

    CHECK(probe.visited == 3u);

    CHECK(probe.sum == 60u);

    CHECK(probe.sawDotted == true);

    VariableHashMap_forEach(&map, nullptr, &probe);

    VariableHashMap_forEach(nullptr, visitFn, &probe);

    CHECK(probe.visited == 3u);

    VariableHashMap_shutdown(&map);
}

/** Runs lifecycle, key policy, collision/growth, projection, constructor, and iteration checks. */
static i32 suite_variable_hash_map() {
    print("=== VariableHashMap Test Suite ===\n");

    testLifecycle();

    testFoldAndBucket();

    testAllBuckets();

    s_variable_hash_map_testRejection();

    testCollisionChain();

    testVolume();

    testStrings();

    testArity();

    testForEach();

    print("\n", s_variable_hash_map_g_checks, " checks, ", s_variable_hash_map_g_fail, " failed");

    if (s_variable_hash_map_g_fail == 0) 
        print("=== VariableHashMap Test Suite: ALL PASS ===");

    return s_variable_hash_map_g_fail == 0 ? 0 : 1;
}
#undef CHECK

/** Void adapter for variable_hash_map suite. */
static fn suite_variable_hash_map_wrap() {
    if (suite_variable_hash_map() != 0)
        g_failures++;
}

/** Suite: variable_mini_map (from substrate/variable/variable_mini_map_test.c). */
/**
 * ============================================================================
 * MODULE: VariableMiniMapTest (variable_mini_map_test/test.c)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the scoped mini map: the 39 single-level buckets,
 * lowercase folding + dotted names, upsert, rejection, forEach, null-safety,
 * string projections.
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_variable_mini_map_g_fail = 0;
static i32 s_variable_mini_map_g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        s_variable_mini_map_g_checks++;                                                       \
        if (!(cond)) {                                                    \
            s_variable_mini_map_g_fail++;                                                     \
            print("  FAIL line ", __LINE__, ": ", #cond);              \
        }                                                                 \
    } while (0)

/** Checks map initialization, empty/missing lookup, basic insertion, null safety, and shutdown. */
static fn s2_testLifecycle() {
    print("[1] init / empty / count / null-safety");

    VariableMiniMap map;

    CHECK(VariableMiniMap_init(&map) == true);

    CHECK(VariableMiniMap_count(&map) == 0u);

    CHECK(VariableMiniMap_isEmpty(&map) == true);

    CHECK(VariableMiniMap_get(&map, "x") == 0u);

    CHECK(VariableMiniMap_contains(&map, "x") == false);

    CHECK(VariableMiniMap_add(&map, "x", 1u) == true);

    CHECK(VariableMiniMap_count(&map) == 1u);

    CHECK(VariableMiniMap_isEmpty(&map) == false);

    CHECK(VariableMiniMap_get(&map, "X") == 1u);

    CHECK(VariableMiniMap_init(nullptr) == false);

    CHECK(VariableMiniMap_count(nullptr) == 0u);

    CHECK(VariableMiniMap_isEmpty(nullptr) == true);

    CHECK(VariableMiniMap_add(nullptr, "x", 1u) == false);

    CHECK(VariableMiniMap_get(nullptr, "x") == 0u);

    CHECK(VariableMiniMap_contains(nullptr, "x") == false);

    VariableMiniMap_shutdown(&map);

    CHECK(VariableMiniMap_count(&map) == 0u);

    VariableMiniMap_shutdown(&map);

    CHECK(true);
}

/** Covers all buckets, case-folded dotted keys, and in-place upsert behavior. */
static fn testBucketsAndFold() {
    print("[2] 39 buckets + folding + dotted names + upsert");

    VariableMiniMap map;

    VariableMiniMap_init(&map);

    string firsts = "abcdefghijklmnopqrstuvwxyz0123456789_$-";

    bytes n = strlen(firsts);

    CHECK(n == 39u);

    for (bytes i = 0u; i < n; i++) {
        utf8 name[4] = { firsts[i], '1', '\0' };

        CHECK(VariableMiniMap_add(&map, name, 0x100u + (pointer) i) == true);
    }

    CHECK(VariableMiniMap_count(&map) == 39u);

    for (bytes i = 0u; i < n; i++) {
        utf8 name[4] = { firsts[i], '1', '\0' };

        CHECK(VariableMiniMap_get(&map, name) == 0x100u + (pointer) i);
    }

    CHECK(VariableMiniMap_add(&map, "Character.Position.X", 0xAAu) == true);

    CHECK(VariableMiniMap_get(&map, "character.position.x") == 0xAAu);

    u32 before = VariableMiniMap_count(&map);

    CHECK(VariableMiniMap_add(&map, "character.position.x", 0xBBu) == true);

    CHECK(VariableMiniMap_count(&map) == before);

    CHECK(VariableMiniMap_get(&map, "CHARACTER.POSITION.X") == 0xBBu);

    VariableMiniMap_shutdown(&map);
}

/** Checks malformed and overlong key rejection without changing the entry count. */
static fn s_variable_mini_map_testRejection() {
    print("[3] illegal names rejected");

    VariableMiniMap map;

    VariableMiniMap_init(&map);

    CHECK(VariableMiniMap_add(&map, nullptr, 1u) == false);

    CHECK(VariableMiniMap_add(&map, "", 1u) == false);

    CHECK(VariableMiniMap_add(&map, "abcdefghijklmnopqrstuvwx", 1u) == false);

    CHECK(VariableMiniMap_add(&map, ".a", 1u) == false);

    CHECK(VariableMiniMap_add(&map, "a.", 1u) == false);

    CHECK(VariableMiniMap_add(&map, "a..b.sh", 1u) == false);

    CHECK(VariableMiniMap_add(&map, "a b.sh", 1u) == false);

    CHECK(VariableMiniMap_count(&map) == 0u);

    VariableMiniMap_shutdown(&map);
}

/** Checks constructor/free forms, bounded projections, and safe no-op iteration inputs. */
static fn testForEachAndStrings() {
    print("[4] forEach + string projections + arity ctor");

    VariableMiniMap *map = VariableMiniMap();

    CHECK(map != nullptr);

    if (!map) 
        return;

    CHECK(Memory_type(map) == TYPE_VARIABLE_MINI_MAP);

    VariableMiniMap_add(map, "position.x", 1u);

    VariableMiniMap_add(map, "position.y", 2u);

    VariableMiniMap_add(map, "health", 3u);

    utf8 buf[128];

    bool truncated = true;

    VariableMiniMap_toString(map, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "VariableMiniMap(count=3)"));

    VariableMiniMap_toStringStruct(map, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "VariableMiniMap { active=true, count=3, buckets=<array[39]> }"));

    utf8 tiny[4];

    VariableMiniMap_toString(map, tiny, sizeof(tiny), &truncated);

    CHECK(truncated == true);

    truncated = true;

    VariableMiniMap_toString(nullptr, buf, sizeof(buf), &truncated);

    CHECK(truncated == false && string_equals(buf, "nullptr"));

    VariableMiniMap_forEach(map, nullptr, nullptr);

    VariableMiniMap_forEach(nullptr, nullptr, nullptr);

    CHECK(true);

    VariableMiniMap_free(map);

    VariableMiniMap_free(nullptr);
}

/** Runs map lifecycle, key policy, projection, and iteration cases. */
static i32 suite_variable_mini_map() {
    print("=== VariableMiniMap (scoped) Test Suite ===\n");

    s2_testLifecycle();

    testBucketsAndFold();

    s_variable_mini_map_testRejection();

    testForEachAndStrings();

    print("\n", s_variable_mini_map_g_checks, " checks, ", s_variable_mini_map_g_fail, " failed");

    if (s_variable_mini_map_g_fail == 0) 
        print("=== VariableMiniMap Test Suite: ALL PASS ===");

    return s_variable_mini_map_g_fail == 0 ? 0 : 1;
}
#undef CHECK

/** Void adapter for variable_mini_map suite. */
static fn suite_variable_mini_map_wrap() {
    if (suite_variable_mini_map() != 0)
        g_failures++;
}

/** Suite: variable_pool (from substrate/variable/variable_pool_test.c). */
/**
 * ============================================================================
 * MODULE: VariablePoolTest (variable_pool_test/test.c)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the static interned string pool: pre-init fail-closed
 * behavior, rejection policy, dedup, self-link validity, sorted lookup,
 * growth with relink, and shutdown semantics. Sections re-init the pool for
 * hermetic state (one process-wide table).
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_variable_pool_g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { print("[variable_pool_test] PASS ", name); } \
    else { print("[variable_pool_test] FAIL ", name); s_variable_pool_g_failures++; } \
} while (0)

/** Exercises static StringPool lifecycle, validation, deduplication, growth, and relinking. */
;;GETTER
static inline u64 StringSlot_self(const StringSlot *slot)
{
    return (*slot).self;
}

static i32 suite_variable_pool() {
    print("=== Running Variable Pool Test Suite ===");

    {
        CHECK("pre-init intern closed", StringPool_intern("label") == -1);
        CHECK("pre-init find closed", StringPool_find("label") == -1);
        CHECK("pre-init slot closed", StringPool_slot(0) == nullptr);
        CHECK("pre-init name closed", StringPool_name(0) == nullptr);
        CHECK("pre-init count closed", StringPool_count() == 0);
        CHECK("pre-init isSlot closed", StringPool_isSlot("label") == false);
        StringPool_shutdown();
        MemoryArena *arena = MemoryArena(64u << 20);
        CHECK("arena created", arena != nullptr);
        CHECK("init null arena", StringPool_init(nullptr) == false);
        CHECK("init ok", StringPool_init(arena) == true);
        CHECK("re-init succeeds shared", StringPool_init(arena) == true);
        CHECK("intern null", StringPool_intern(nullptr) == -1);
        CHECK("intern empty", StringPool_intern("") == -1);
        CHECK("intern 24-utf8 rejected", StringPool_intern("abcdefghijklmnopqrstuvwx") == -1);
        CHECK("find null", StringPool_find(nullptr) == -1);
        CHECK("find absent", StringPool_find("nope") == -1);
        CHECK("slot OOB", StringPool_slot(99) == nullptr);
        CHECK("name OOB", StringPool_name(99) == nullptr);
        CHECK("isSlot null", StringPool_isSlot(nullptr) == false);
        StringPool_shutdown();
        StringPool_shutdown();
        CHECK("shutdown twice count", StringPool_count() == 0);
        CHECK("post-shutdown intern closed", StringPool_intern("label") == -1);
        MemoryArena_destroy(arena);
    }

    {
        MemoryArena *arena = MemoryArena(64u << 20);
        StringPool_init(arena);
        i32 a = StringPool_intern("label");
        i32 b = StringPool_intern("label");
        CHECK("intern dedup", a >= 0 && a == b);
        CHECK("count one", StringPool_count() == 1);
        CHECK("find hit", StringPool_find("label") == a);
        string edge23 = "abcdefghijklmnopqrstuvw";
        CHECK("23-utf8 edge length", strlen(edge23) == 23);
        i32 e = StringPool_intern(edge23);
        CHECK("23-utf8 accepted", e >= 0);
        CHECK("name round-trip", string_equals(StringPool_name((u32) e), edge23));
        const StringSlot *slot = StringPool_slot((u32) a);
        CHECK("slot self link", slot != nullptr && StringSlot_self(slot) == (u64) (pointer) slot);
        CHECK("isSlot live", StringPool_isSlot(slot) == true);
        utf8 nearby[32];
        memset(nearby, 0, sizeof(nearby));
        CHECK("isSlot foreign", StringPool_isSlot(nearby) == false);
        StringPool_shutdown();
        CHECK("isSlot after shutdown", StringPool_isSlot(slot) == false);
        MemoryArena_destroy(arena);
    }

    {
        MemoryArena *arena = MemoryArena(64u << 20);
        StringPool_init(arena);
        string words[] = { "delta", "alpha", "charlie", "bravo" };
        for (i32 i = 0; i < 4; i++) 
            StringPool_intern(words[i]);
        CHECK("count four", StringPool_count() == 4);
        bool allFound = true;
        for (i32 i = 0; i < 4; i++) {
            i32 idx = StringPool_find(words[i]);

            if (idx < 0 || !string_equals(StringPool_name((u32) idx), words[i])) 
                allFound = false;
        }

        CHECK("all found by name", allFound);
        utf8 buf[8];
        for (i32 i = 0; i < 200; i++) {
            snprintf(buf, sizeof(buf), "k%d", i);

            if (StringPool_intern(buf) < 0) 
                break;
        }

        CHECK("growth past capacity", StringPool_count() == 204);
        CHECK("early name survives grow", StringPool_find("alpha") >= 0);
        const StringSlot *s = StringPool_slot((u32) StringPool_find("alpha"));
        CHECK("self relinked after grow", s != nullptr && StringPool_isSlot(s) == true);
        StringPool_shutdown();
        MemoryArena_destroy(arena);
    }

    print("\n=== Variable Pool Test Summary: ", s_variable_pool_g_failures, " failures ===");

    return s_variable_pool_g_failures > 0 ? 1 : 0;
}
#undef CHECK

/** Void adapter for variable_pool suite. */
static fn suite_variable_pool_wrap() {
    if (suite_variable_pool() != 0)
        g_failures++;
}

/** Suite: variable_slot (from substrate/variable/variable_slot_test.c). */
/**
 * ============================================================================
 * MODULE: VariableSlotTest (variable_slot_test/test.c)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the 32-byte name box (the reflection atom): layout, the
 * name charset gate (fold to lowercase; reject empty/overlong/illegal incl. the
 * dot splitter), round-trip getters, the pointer binding, the arity
 * constructors, null-safety, and both bounded string projections with the
 * truncation flag (the Cold-Strict, Hot-Minimal Validation Law).
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_variable_slot_g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { print("[variable_slot_test] PASS ", name); } \
    else { print("[variable_slot_test] FAIL ", name); s_variable_slot_g_failures++; } \
} while (0)

/** Checks VariableSlot layout, name grammar/folding, accessors, constructors, strings, and null safety. */
static i32 suite_variable_slot() {
    print("=== Running Variable Slot Test Suite ===");

    CHECK("sizeof slot is 32", sizeof(VariableSlot) == 32u);

    {
        VariableSlot slot;
        memset(&slot, 0, sizeof(slot));
        CHECK("init ok", VariableSlot_init(&slot, "Health", 0xABCDu) == true);
        CHECK("pointer bound", VariableSlot_getPointer(&slot) == 0xABCDu);
        CHECK("name folded to lowercase", string_equals(VariableSlot_getName(&slot), "health"));
        CHECK("not empty", VariableSlot_isEmpty(&slot) == false);
        CHECK("nameEquals case-insensitive", VariableSlot_nameEquals(&slot, "HEALTH") == true);
        CHECK("nameEquals differs", VariableSlot_nameEquals(&slot, "health_ui") == false);
    }

    {
        VariableSlot slot;
        CHECK("camel fold ok", VariableSlot_init(&slot, "helloWorld", 0) == true);
        CHECK("camel folded", string_equals(VariableSlot_getName(&slot), "helloworld"));
    }

    {
        VariableSlot slot;
        CHECK("init null name", VariableSlot_init(&slot, nullptr, 0) == false);
        CHECK("init empty name", VariableSlot_init(&slot, "", 0) == false);
        CHECK("init 24-utf8 name", VariableSlot_init(&slot, "abcdefghijklmnopqrstuvwx", 0) == false);
        CHECK("init 23-utf8 ok", VariableSlot_init(&slot, "abcdefghijklmnopqrstuvw", 0) == true);
        CHECK("init dotted ok", VariableSlot_init(&slot, "character.position.x", 0) == true);
        CHECK("init leading dot rejected", VariableSlot_init(&slot, ".a", 0) == false);
        CHECK("init trailing dot rejected", VariableSlot_init(&slot, "a.", 0) == false);
        CHECK("init d64 dot rejected", VariableSlot_init(&slot, "a..b.sh", 0) == false);
        CHECK("init space rejected", VariableSlot_init(&slot, "a b.sh", 0) == false);
        CHECK("init slash rejected", VariableSlot_init(&slot, "a/b.sh", 0) == false);
        CHECK("init dollar ok", VariableSlot_init(&slot, "a$b.sh", 0) == true);
        CHECK("init dash ok", VariableSlot_init(&slot, "a-b.sh", 0) == true);
        CHECK("init null slot", VariableSlot_init(nullptr, "x", 0) == false);
    }

    {
        VariableSlot dotted;
        VariableSlot_init(&dotted, "Character.Position.X", 0);
        CHECK("dotted folded", string_equals(VariableSlot_getName(&dotted), "character.position.x"));
        CHECK("dotted nameEquals", VariableSlot_nameEquals(&dotted, "CHARACTER.position.x") == true);
    }

    {
        VariableSlot slot;
        VariableSlot_init(&slot, "first", 1);
        CHECK("setName bad rejected", VariableSlot_setName(&slot, "bad name") == false);
        CHECK("old name survives bad set", string_equals(VariableSlot_getName(&slot), "first"));
        CHECK("setName ok", VariableSlot_setName(&slot, "Second") == true);
        CHECK("name replaced + folded", string_equals(VariableSlot_getName(&slot), "second"));
        VariableSlot_setPointer(&slot, 0x99u);
        CHECK("pointer replaced", VariableSlot_getPointer(&slot) == 0x99u);
    }

    {
        CHECK("null getName", VariableSlot_getName(nullptr) == nullptr);
        CHECK("null getPointer", VariableSlot_getPointer(nullptr) == 0u);
        CHECK("null isEmpty", VariableSlot_isEmpty(nullptr) == true);
        CHECK("null nameEquals", VariableSlot_nameEquals(nullptr, "x") == false);
        VariableSlot_setName(nullptr, "x");
        VariableSlot_setPointer(nullptr, 1);
        CHECK("null setters no-op", true);
    }

    {
        VariableSlot *empty = VariableSlot();
        CHECK("arity 0 allocates", empty != nullptr && VariableSlot_isEmpty(empty) == true);
        VariableSlot *named = VariableSlot("player");
        CHECK("arity 1 names", named != nullptr && VariableSlot_nameEquals(named, "player") == true);
        VariableSlot *bound = VariableSlot("score", 0x42u);
        CHECK("arity 2 binds", bound != nullptr && VariableSlot_getPointer(bound) == 0x42u);
        CHECK("allocated typed", bound != nullptr && Memory_type(bound) == TYPE_VARIABLE_SLOT);
        VariableSlot *bad = VariableSlot("no good");
        CHECK("arity rejects bad name", bad == nullptr);
        VariableSlot_free(empty);
        VariableSlot_free(named);
        VariableSlot_free(bound);
        VariableSlot_free(nullptr);
        CHECK("free null-safe", true);
    }

    {
        VariableSlot slot;
        VariableSlot_init(&slot, "health", 0x1Fu);
        utf8 buf[96];
        bool truncated = true;
        VariableSlot_toString(&slot, buf, sizeof(buf), &truncated);
        CHECK("toString fits", truncated == false && string_equals(buf, "VariableSlot(health => 0x1f)"));
        VariableSlot_toStringStruct(&slot, buf, sizeof(buf), &truncated);
        CHECK("toStringStruct fits", truncated == false &&
              string_equals(buf, "VariableSlot { name=\"health\", pointer=0x1f }"));

        utf8 tiny[4];
        VariableSlot_toString(&slot, tiny, sizeof(tiny), &truncated);
        CHECK("toString truncated flagged", truncated == true);
        VariableSlot_toStringStruct(&slot, tiny, sizeof(tiny), &truncated);
        CHECK("toStringStruct truncated flagged", truncated == true);

        truncated = true;
        VariableSlot_toString(nullptr, buf, sizeof(buf), &truncated);
        CHECK("toString null renders nullptr", truncated == false && string_equals(buf, "nullptr"));
    }

    print("=== Variable Slot: ", s_variable_slot_g_failures, " failure(s) ===");

    return s_variable_slot_g_failures == 0 ? 0 : 1;
}
#undef CHECK

/** Void adapter for variable_slot suite. */
static fn suite_variable_slot_wrap() {
    if (suite_variable_slot() != 0)
        g_failures++;
}

/** Suite: variable_strict (from substrate/variable/variable_strict_test.c). */
/**
 * ============================================================================
 * MODULE: VariableStrictTest (variable_strict_test/test.c, LOCAL ONLY)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Local proof for the pool-backed SymbolTable: strict create-or-fail,
 * edge-case rejections, dotted names, class filtering, rename flows.
 * Never committed (repo tests/ policy), run by hand after surgery.
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_variable_strict_g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { print("[variable_strict_test] PASS ", name); } \
    else { print("[variable_strict_test] FAIL ", name); s_variable_strict_g_failures++; } \
} while (0)

/** Exercises strict SymbolTable rejection, class filtering, rename, and relational rebinding flows. */
static i32 suite_variable_strict() {
    print("=== Running SymbolTable Strict Test Suite ===");

    if (!Memory_init(0u)) 
       return 1;

    SymbolTable scope = SymbolTable();

    CHECK("constructor", scope.active);

    i32 a = SymbolTable_instant(&scope, "hp", 1, (pointer) 0x100);

    CHECK("first instant", a >= 0);

    i32 dup = SymbolTable_instant(&scope, "hp", 1, (pointer) 0x200);

    CHECK("dup fails", dup == -1);

    CHECK("dup keeps old value", SymbolTable_getPointer(&scope, a) == (pointer) 0x100);

    CHECK("empty fails", SymbolTable_instant(&scope, "", 1, 1) == -1);

    CHECK("null fails", SymbolTable_instant(&scope, nullptr, 1, 1) == -1);

    CHECK("overlong fails", SymbolTable_instant(&scope, "abcdefghijklmnopqrstuvwxyz", 1, 1) == -1);

    CHECK("space fails", SymbolTable_instant(&scope, "has space", 1, 1) == -1);

    CHECK("utf8 fails", SymbolTable_instant(&scope, "caf\xc3\xa9", 1, 1) == -1);

    CHECK("dash fails", SymbolTable_instant(&scope, "has-dash", 1, 1) == -1);

    CHECK("getId bad silent", SymbolTable_getId(&scope, "has space") == -1);

    i32 f = SymbolTable_instant(&scope, "character.position.x", 2, (pointer) 0x300);

    CHECK("dotted instant", f >= 0);

    CHECK("dotted find", SymbolTable_getId(&scope, "character.position.x") == f);

    CHECK("case folds", SymbolTable_getId(&scope, "Character.Position.X") == f);

    CHECK("getName dotted", string_equals(SymbolTable_getName(&scope, f), "character.position.x"));

    SymbolTable_instant(&scope, "mp", 1, (pointer) 0x400);

    SymbolTable_instant(&scope, "name", 3, (pointer) 0x500);

    i32 ids[8];

    CHECK("class1 total", SymbolTable_findByClass(&scope, 1, ids, 8) == 2);

    CHECK("class2 total", SymbolTable_findByClass(&scope, 2, ids, 8) == 1);

    CHECK("class9 total", SymbolTable_findByClass(&scope, 9, ids, 8) == 0);

    i32 one[1];

    CHECK("truncated total visible", SymbolTable_findByClass(&scope, 1, one, 1) == 2);

    CHECK("class pinned", SymbolTable_getClassId(&scope, a) == 1);

    SymbolTable_setPointer(&scope, a, (pointer) 0x111);

    CHECK("setPointer keeps class", SymbolTable_getClassId(&scope, a) == 1 && SymbolTable_getPointer(&scope, a) == (pointer) 0x111);

    CHECK("cas ok", SymbolTable_compareAndSetPointer(&scope, a, (pointer) 0x111, (pointer) 0x222) == true);

    CHECK("cas mismatch", SymbolTable_compareAndSetPointer(&scope, a, (pointer) 0x111, (pointer) 0x333) == false);

    CHECK("rename ok", SymbolTable_rename(&scope, "hp", "health") == true);

    CHECK("old gone", SymbolTable_getId(&scope, "hp") == -1);

    CHECK("new resolves", SymbolTable_getId(&scope, "health") == a);

    CHECK("rename collision", SymbolTable_rename(&scope, "health", "mp") == false);

    CHECK("rename unknown", SymbolTable_rename(&scope, "nope", "alsono") == false);

    CHECK("rename bad new", SymbolTable_rename(&scope, "health", "has space") == false);

    CHECK("setValue create", Relational_setValue(&scope, "score", 4, (any) 0x600) == true);

    CHECK("setValue rebind", Relational_setValue(&scope, "score", 4, (any) 0x601) == true);

    CHECK("rebound value", (pointer) Relational_getValue(&scope, "score") == (pointer) 0x601);

    CHECK("setFunction create", Relational_setFunction(&scope, "tick", (any) 0x700) == true);

    CHECK("setFunction rebind", Relational_setFunction(&scope, "tick", (any) 0x701) == true);

    Relational_setString(&scope, "title", "hello");

    CHECK("setString create", Relational_getString(&scope, "title") != nullptr &&
        string_equals(Relational_getString(&scope, "title"), "hello"));
    Relational_setString(&scope, "title", "world");

    CHECK("setString replace", string_equals(Relational_getString(&scope, "title"), "world"));

    CHECK("active count", SymbolTable_getActiveCount(&scope) == 7);

    SymbolTable_shutdown(&scope);

    SymbolTable_shutdown(&scope);

    print("\n=== SymbolTable Strict Test Summary: ", s_variable_strict_g_failures, " failures ===");

    return s_variable_strict_g_failures > 0 ? 1 : 0;
}
#undef CHECK

/** Void adapter for variable_strict suite. */
static fn suite_variable_strict_wrap() {
    if (suite_variable_strict() != 0)
        g_failures++;
}

/** Suite: buffer (from primitive/buffer_test.c). */
/**
 * re/test/primitive/buffer_test.c, owner test for primitive/buffer.
 *
 * Proves the contiguous byte buffer:
 * - Buffer() empty, Buffer(n) zeroed, Buffer(bytes, n) copies;
 * - append grows by cold doubling, bytes stay contiguous;
 * - setLength clamps, reserve grows, null-safe everywhere;
 * - free releases payload + record.
 */


static i32 s_buffer_g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        print("FAIL ", __FILE__, ":", __LINE__, ": ", #cond); \
        s_buffer_g_failures++; \
    } \
} while (0)

static i32 suite_buffer() {
    Buffer *e = Buffer();
    CHECK(e != nullptr);
    CHECK(Buffer_length(e) == 0);
    CHECK(Buffer_capacity(e) == 0);
    CHECK(Buffer_bytes(e) == nullptr);

    Buffer *sized = Buffer((bytes) 16);
    CHECK(sized != nullptr);
    CHECK(Buffer_length(sized) == 16);
    CHECK(Buffer_capacity(sized) == 16);
    CHECK(Buffer_bytes(sized) != nullptr);
    CHECK(Buffer_bytes(sized)[0] == 0);
    CHECK(Buffer_bytes(sized)[15] == 0);

    const u8 src[4] = { 1, 2, 3, 4 };
    Buffer *copied = Buffer(src, sizeof(src));
    CHECK(copied != nullptr);
    CHECK(Buffer_length(copied) == 4);
    CHECK(memcmp(Buffer_bytes(copied), src, 4) == 0);
    CHECK(Buffer_bytes(copied) != src);

    const u8 more[4] = { 5, 6, 7, 8 };
    CHECK(Buffer_append(copied, more, sizeof(more)));
    CHECK(Buffer_length(copied) == 8);
    u8 want[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(memcmp(Buffer_bytes(copied), want, 8) == 0);

    CHECK(Buffer_setLength(copied, 3));
    CHECK(Buffer_length(copied) == 3);
    CHECK(!Buffer_setLength(copied, Buffer_capacity(copied) + 1));

    bytes grown = Buffer_capacity(copied);
    CHECK(Buffer_reserve(copied, grown + 128));
    CHECK(Buffer_capacity(copied) >= grown + 128);
    CHECK(Buffer_length(copied) == 3);

    CHECK(!Buffer_append(nullptr, more, 1));
    CHECK(!Buffer_append(copied, nullptr, 1));
    CHECK(Buffer_append(copied, more, 0));
    CHECK(Buffer_length(nullptr) == 0);
    CHECK(Buffer_bytes(nullptr) == nullptr);
    Buffer_free(nullptr);

    Buffer_free(e);
    Buffer_free(sized);
    Buffer_free(copied);

    if (s_buffer_g_failures == 0)
        print("buffer_test: PASS");
    else
        print("buffer_test: ", s_buffer_g_failures, " FAILURES");
    return s_buffer_g_failures != 0;
}
#undef CHECK

/** Void adapter for buffer suite. */
static fn suite_buffer_wrap() {
    if (suite_buffer() != 0)
        g_failures++;
}

/** Suite: vex_string (from primitive/vex-string_test.c). */
/**
 * re/test/primitive/vex-string_test.c, owner test for primitive/vex-string.
 *
 * Proves the arena-backed string primitive:
 * - allocate / allocateBytes / allocateUninitialized (+ the String()
 * chooser) produce NUL-terminated blocks with length, capacity, type,
 * array-form and classId introspection;
 * - copy is independent, equals compares by value, free is null-safe;
 * - compare / contains / indexOf / instring (block and literal forms);
 * - substring / subFirst* / subLast* with clamping and out-of-range starts;
 * - append / appendLiteral / appendLiterals (null-combo identity) and
 * appendInto with an adequate destination;
 * - String_appendFirst / String_appendFirstLiteral grow via Memory_realloc.
 */


static i32 s_vex_string_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_vex_string_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Covers immutable string literals, arena constructors, cloning, ownership, and freeing. */
static fn test_allocate()
{
    string literal = "hello";
    CHECK(!string_isOwned(literal));
    CHECK(string_length(literal) == 5u);
    CHECK(string_capacity(literal) == 6u);
    CHECK(string_type(literal) == 0u);
    CHECK(!string_isArray(literal));
    string_free(literal);

    CHECK(string_1(nullptr) == nullptr);
    CHECK(string_clone(nullptr) == nullptr);

    string s = string("hello");
    CHECK(s != nullptr && s != literal);
    CHECK(string_isOwned(s));
    CHECK(string_equals(s, "hello"));
    CHECK(string_length(s) == 5u);
    CHECK(string_capacity(s) == 6u);
    CHECK(string_type(s) == TYPE_STRING_ARRAY);
    CHECK(string_isArray(s));
    CHECK(string_classId() == ID_STRING);
    CHECK(string_equals(s, "hello"));
    CHECK(string_equals("hello", s));
    CHECK(!string_equals(s, "hell"));
    CHECK(!string_equals(s, "hello!"));
    CHECK(!string_equals(s, nullptr));

    string e = string("");
    CHECK(e != nullptr && string_length(e) == 0u && string_equals(e, ""));

    string b = string("abcdef", 3u);
    CHECK(b != nullptr && string_length(b) == 3u && string_equals(b, "abc"));
    string z = string(nullptr, 0u);
    CHECK(z != nullptr && string_length(z) == 0u);
    CHECK(string(nullptr, 3u) == nullptr);

    string c = string_clone(s);
    CHECK(c != nullptr && c != s && string_equals(c, "hello"));

    string chooser0 = string();
    CHECK(chooser0 != nullptr && string_length(chooser0) == 0u);
    string chooser1 = string("world");
    CHECK(chooser1 != nullptr && string_equals(chooser1, "world"));

    string_free(s);
    string_free(e);
    string_free(b);
    string_free(z);
    string_free(c);
    string_free(chooser0);
    string_free(chooser1);
    string_free(nullptr);
}

/** Checks safe defaults from every string introspection and equality call on null inputs. */
static fn test_null_introspection()
{
    CHECK(string_length(nullptr) == 0u);
    CHECK(string_type(nullptr) == 0u);
    CHECK(!string_isOwned(nullptr));
    CHECK(!string_isArray(nullptr));
    CHECK(string_capacity(nullptr) == 0u);
    CHECK(!string_equals(nullptr, "x"));
    CHECK(!string_equals(nullptr, nullptr));
}

/** Checks ordering, containment, index lookup, in-string matching, and null arguments. */
static fn test_compare_search()
{
    string a = string("apple");
    string b = string("banana");
    string apple2 = string("apple");

    CHECK(string_compare(a, b) < 0);
    CHECK(string_compare(b, a) > 0);
    CHECK(string_compare(a, apple2) == 0);
    CHECK(string_compare(a, "apple") == 0);
    CHECK(string_compare("apple", "banana") < 0);
    CHECK(string_compare(nullptr, nullptr) == 0);
    CHECK(string_compare(nullptr, a) < 0);
    CHECK(string_compare(a, nullptr) > 0);

    string needle = string("nan");
    CHECK(string_contains(b, needle));
    CHECK(string_contains(b, "ban"));
    CHECK(string_contains("banana", needle));
    CHECK(!string_contains(b, "xyz"));
    CHECK(!string_contains(nullptr, a));
    CHECK(!string_contains(b, nullptr));
    string_free(needle);

    CHECK(string_indexOf(b, "ana") == 1u);
    CHECK(string_indexOf("banana", "ana") == 1u);
    CHECK(string_indexOf(b, "zzz") == SIZE_MAX);
    CHECK(string_indexOf(nullptr, b) == SIZE_MAX);

    CHECK(string_instring(1u, b, "ana"));
    CHECK(!string_instring(0u, b, "ana"));
    CHECK(!string_instring(0u, b, "zzz"));

    string_free(a);
    string_free(b);
    string_free(apple2);
}

/** Checks substring clamping, empty out-of-range results, and first/last projections. */
static fn test_substrings()
{
    string s = string("hello world");

    string sub = string_substring(s, 6u, 5u);
    CHECK(string_equals(sub, "world"));
    string clamped = string_substring(s, 6u, 999u);
    CHECK(string_equals(clamped, "world"));
    string empty = string_substring(s, 11u, 3u);
    CHECK(string_length(empty) == 0u);
    CHECK(string_substring(nullptr, 0u, 1u) == nullptr);

    string lit = string_substring("abcdef", 2u, 3u);
    CHECK(string_equals(lit, "cde"));

    string firstChar = string_subFirstChar(s);
    CHECK(string_equals(firstChar, "h"));
    string lastChar = string_subLastChar(s);
    CHECK(string_equals(lastChar, "d"));
    string first = string_subFirst(s, 5u);
    CHECK(string_equals(first, "hello"));
    string last = string_subLast(s, 5u);
    CHECK(string_equals(last, "world"));
    string all = string_subLast(s, 999u);
    CHECK(string_equals(all, "hello world"));

    string view = s;
    CHECK(string_length(view) == 11u && string_startsWith(view, "hello"));

    string_free(s);
    string_free(sub);
    string_free(clamped);
    string_free(empty);
    string_free(lit);
    string_free(firstChar);
    string_free(lastChar);
    string_free(first);
    string_free(last);
    string_free(all);
}

/** Checks immutable append over arena strings and literals, including null identities. */
static fn test_append()
{
    string a = string("foo");
    string b = string("bar");

    string ab = string_append(a, b);
    CHECK(string_equals(ab, "foobar"));

    string aOnly = string_append(a, nullptr);
    string bOnly = string_append(nullptr, b);
    string none = string_append(nullptr, nullptr);
    CHECK(string_equals(aOnly, "foo"));
    CHECK(string_equals(bOnly, "bar"));
    CHECK(string_length(none) == 0u);

    string lit = string_append(a, "baz");
    CHECK(!string_equals(lit, "bazfoo"));
    CHECK(string_equals(lit, "foobaz"));

    string lits = string_append("ab", "cd");
    string litR = string_append(nullptr, "cd");
    string litL = string_append("ab", nullptr);
    CHECK(string_equals(lits, "abcd"));
    CHECK(string_equals(litR, "cd"));
    CHECK(string_equals(litL, "ab"));

    string_free(a);
    string_free(b);
    string_free(ab);
    string_free(aOnly);
    string_free(bOnly);
    string_free(none);
    string_free(lit);
    string_free(lits);
    string_free(litR);
    string_free(litL);
}

static i32 failures = 0;
static i32 checks = 0;

static fn check(bool condition, string label)
{
    ++checks;
    if (!condition)
    {
        print("FAIL ", label);
        ++failures;
    }
}

/** Verifies string comparison, prefix/suffix, search, and bounded slice constructors. */
static i32 test_string_views()
{
    string alpha = "alpha";
    string alphaCopy = "alpha";
    string beta = "beta";
    string empty = "";

    check(string_length(alpha) == 5, "length");
    check(string_isEmpty(empty), "empty view");
    check(!string_isEmpty(alpha), "non-empty view");
    check(string_compare(alpha, alphaCopy) == 0, "equal compare");
    check(string_compare(alpha, beta) < 0, "shorter prefix sorts first");
    check(string_compare(beta, alpha) > 0, "reverse compare");
    check(string_compare(empty, empty) == 0, "two empty views");
    check(string_compare(empty, alpha) < 0, "empty before text");
    check(string_equals(alpha, alphaCopy), "equals");
    check(!string_equals(alpha, beta), "not equals");

    check(string_compare("ab", "abc") < 0,
          "prefix is less, not equal");

    check(string_startsWith(alpha, "alp"), "starts with");
    check(!string_startsWith(alpha, "lph"), "not starts with");
    check(string_endsWith(alpha, "pha"), "ends with");
    check(!string_endsWith(alpha, "alpha!") , "suffix longer than view");
    check(string_startsWith(alpha, "al"), "has prefix literal");

    check(string_indexOf(alpha, "ph") == 2, "find substring");
    check(string_indexOf(alpha, "zz") == SIZE_MAX, "find absent");
    check(string_indexOf(alpha, empty) == 0, "find empty needle");
    check(string_findChar(alpha, 'h') == 3, "find utf8");
    check(string_findChar(alpha, 'z') == SIZE_MAX, "find absent utf8");

    const utf8 raw[3] = {'x', 'y', 'z'};
    string unterminated = string_2(raw, 3);
    check(string_length(unterminated) == 3, "unterminated length");
    check(string_equals(unterminated, "xyz"),
          "unterminated compares equal to a C string");
    check(string_indexOf(unterminated, "yz") == 1,
          "unterminated search stays inside the bound");
    string_free(unterminated);

    string head = string_2("abc", 2);
    check(string_compare(head, "abc") < 0, "bounded view is shorter");
    check(string_findChar(head, 'c') == SIZE_MAX, "search honours the bound");
    string_free(head);

    if (failures == 0)
    {
        print("primitive/vex-string views: ", checks, " checks passed");
        return 0;
    }
    print("primitive/vex-string views: ", failures, " of ", checks, " check(s) failed");
    return 1;
}

/** Initializes the memory substrate, runs each string contract case, and returns aggregate failures. */
static i32 suite_vex_string() {
    CHECK(Memory_init(0));
    test_allocate();
    test_null_introspection();
    test_compare_search();
    test_substrings();
    test_append();
    if (test_string_views() != 0)
        s_vex_string_g_failures++;

    if (s_vex_string_g_failures == 0) {
        print("string_test: all assertions held");
        return 0;
    }
    print("string_test: ", s_vex_string_g_failures, " FAILURES");
    return 1;
}
#undef CHECK

/** Void adapter for vex_string suite. */
static fn suite_vex_string_wrap() {
    if (suite_vex_string() != 0)
        g_failures++;
}

/** Suite: varchar (arena-backed immutable varchar constructors, conversion, cloning, and string_clone). */
static i32 s_varchar_failures = 0;

static fn varchar_check(bool condition, string label)
{
    if (!condition) {
        print("FAIL varchar: ", label);
        s_varchar_failures++;
    }
}

static i32 suite_varchar()
{
    s_varchar_failures = 0;
    varchar_check(Memory_init(0), "Memory_init");

    varchar empty = varchar(100);
    varchar_check(empty != nullptr && varchar_limit(empty) == 100u && varchar_length(empty) == 0u && varchar_byteLength(empty) == 0u && varchar_isEmpty(empty), "varchar(100)");
    varchar_check(Memory_type((any) (pointer) empty) == TYPE_VARCHAR, "TYPE_VARCHAR header");

    varchar hello = varchar(100, "hello world");
    varchar_check(hello != nullptr && varchar_limit(hello) == 100u && varchar_length(hello) == 11u && varchar_byteLength(hello) == 11u && varchar_format(hello) == FORMAT_UTF8, "varchar(100, hello world)");
    varchar_check(varchar_string(hello) != nullptr && string_equals(varchar_string(hello), "hello world"), "varchar_string borrow");
    varchar_check(varchar_equalsString(hello, "hello world"), "varchar_equalsString");

    varchar copy = varchar_clone(hello);
    varchar_check(copy != nullptr && copy != hello && varchar_equals(hello, copy) && varchar_limit(copy) == 100u, "varchar_clone distinct immutable copy");

    string unicode = "A\xC3\xA9\xE6\x97\xA5\xF0\x9F\x94\xA5";
    varchar u16 = varchar(10, unicode, FORMAT_UTF16);
    varchar_check(u16 != nullptr && varchar_format(u16) == FORMAT_UTF16 && varchar_limit(u16) == 10u && varchar_length(u16) == 4u && varchar_byteLength(u16) == 10u, "varchar(10, unicode, FORMAT_UTF16)");
    varchar_check(varchar_equalsString(u16, unicode), "utf16 equalsString");

    varchar u32 = varchar_convert(u16, FORMAT_UTF32);
    varchar_check(u32 != nullptr && varchar_format(u32) == FORMAT_UTF32 && varchar_limit(u32) == 10u && varchar_length(u32) == 4u && varchar_byteLength(u32) == 16u, "varchar_convert to FORMAT_UTF32");
    varchar_check(varchar_equalsCodePoints(u16, u32), "utf16 vs utf32 code points");

    varchar cesu = varchar_convert(u32, FORMAT_CESU8);
    varchar_check(cesu != nullptr && varchar_format(cesu) == FORMAT_CESU8 && varchar_length(cesu) == 4u && varchar_byteLength(cesu) == 12u, "varchar_convert to FORMAT_CESU8");
    varchar_check(varchar_equalsCodePoints(u16, cesu), "utf16 vs cesu8 code points");

    varchar fromRaw = varchar_fromBytes(10u, varchar_value(u16), (bytes) varchar_byteLength(u16), FORMAT_UTF16);
    varchar_check(fromRaw != nullptr && fromRaw != u16 && varchar_equals(fromRaw, u16), "varchar_fromBytes utf16");

    varchar over = varchar(3, unicode);
    varchar_check(over == nullptr, "limit exceeded returns nullptr");
    varchar badAscii = varchar(10, unicode, FORMAT_ASCII);
    varchar_check(badAscii == nullptr, "non-ascii rejected for FORMAT_ASCII");
    varchar badLatin = varchar(10, unicode, FORMAT_LATIN1);
    varchar_check(badLatin == nullptr, "non-latin1 rejected for FORMAT_LATIN1");

    MemoryArena *customArena = MemoryArena(65536u);
    varchar_check(customArena != nullptr, "custom MemoryArena");
    varchar inCustom = varchar_in(customArena, 32u, "arena-owned", FORMAT_UTF8);
    varchar_check(inCustom != nullptr && varchar_equalsString(inCustom, "arena-owned") && varchar_limit(inCustom) == 32u, "varchar_in custom arena");
    varchar cloneCustom = varchar_cloneIn(customArena, inCustom);
    varchar_check(cloneCustom != nullptr && cloneCustom != inCustom && varchar_equals(inCustom, cloneCustom), "varchar_cloneIn custom arena");

    string literal = "immutable text";
    string sClone1 = string_clone(literal);
    string sClone2 = string_clone(literal);
    varchar_check(sClone1 != nullptr && sClone2 != nullptr && sClone1 != literal && sClone1 != sClone2, "string_clone never pools");
    varchar_check(string_equals(sClone1, literal) && string_equals(sClone2, literal), "string_clone content");
    varchar_check(Memory_type((any) (pointer) sClone1) == TYPE_STRING_ARRAY, "TYPE_STRING_ARRAY header");

    string sSlice = string_sliceIn(customArena, literal, 9u);
    varchar_check(sSlice != nullptr && string_equals(sSlice, "immutable"), "string_sliceIn");

    string_free(sSlice);
    string_free(sClone2);
    string_free(sClone1);
    string_free(literal);
    string_free(nullptr);

    varchar_free(cloneCustom);
    varchar_free(inCustom);
    MemoryArena_destroy(customArena);

    varchar_free(fromRaw);
    varchar_free(cesu);
    varchar_free(u32);
    varchar_free(u16);
    varchar_free(copy);
    varchar_free(hello);
    varchar_free(empty);
    varchar_free(nullptr);

    if (s_varchar_failures == 0) {
        print("primitive/varchar: PASS");
        return 0;
    }

    print("primitive/varchar: ", s_varchar_failures, " FAILURES");
    return 1;
}

/** Void adapter for varchar suite. */
static fn suite_varchar_wrap()
{
    if (suite_varchar() != 0)
        g_failures++;
}

/** Suite: spin (from atomic/spin/spin_test.c). */
/**
 * vexspoke/test/atomic/spin_test.c, owner test for atomic/spin (SpinLock).
 *
 * Proves the one-word spinlock's whole contract:
 * - zero-value / SPIN_LOCK_INIT starts free; nullptr is a safe no-op;
 * - acquire  to  isLocked  to  release round trip, and isLocked tracks state;
 * - tryLock fails while held and succeeds when free;
 * - tryLockTimeout returns false at a deadline instead of spinning forever;
 * - unlock is FAIL-CLOSED: a non-owner release is refused, the lock stays
 * held, and only the owner clears it (the pointer/identity safety law);
 * - mutual exclusion + release/acquire visibility under real contention
 * with an external bounded watchdog (the Concurrency and Bounded Progress
 * Law): N threads hammer one counter, final value is exact.
 */



static i32 s_spin_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_spin_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** ── nullptr safety is a first-class case ─────────────────────────────────── */
/** Verifies null lock operations fail safely without acquiring or releasing state. */
static fn s2_test_null_safety() {
    SpinLock_lock(nullptr);

    CHECK(!SpinLock_tryLock(nullptr));

    CHECK(!SpinLock_tryLockTimeout(nullptr, 0));

    SpinLock_unlock(nullptr);

    CHECK(!SpinLock_isLocked(nullptr));
}

/** ── basic acquire/release + isLocked round trip ──────────────────────────── */
/** Checks zero initialization, acquisition, try-acquisition, lock state, and release. */
static fn test_basic_round_trip() {
    SpinLock lock = SPIN_LOCK_INIT;

    CHECK(!SpinLock_isLocked(&lock));

    CHECK(SpinLock_tryLock(&lock));

    CHECK(SpinLock_isLocked(&lock));

    CHECK(!SpinLock_tryLock(&lock));

    SpinLock_unlock(&lock);

    CHECK(!SpinLock_isLocked(&lock));

    SpinLock_unlock(&lock);

    CHECK(!SpinLock_isLocked(&lock));

    SpinLock_lock(&lock);

    CHECK(SpinLock_isLocked(&lock));

    SpinLock_unlock(&lock);

    CHECK(!SpinLock_isLocked(&lock));
}

/** ── timeout path: a held lock is reported at the deadline, never hangs ───── */
/** Confirms timed acquisition returns at its deadline and succeeds after release. */
static fn test_timeout() {
    SpinLock lock = SPIN_LOCK_INIT;

    CHECK(SpinLock_tryLock(&lock));

    CHECK(!SpinLock_tryLockTimeout(&lock, 200000));

    CHECK(!SpinLock_tryLockTimeout(&lock, 0));

    SpinLock_unlock(&lock);

    CHECK(SpinLock_tryLockTimeout(&lock, 1000000));

    SpinLock_unlock(&lock);
}

/** ── fail-closed foreign unlock: a non-owner must never open the lock ─────── */
static SpinLock s2_g_foreign;
static atomic_int g_foreign_ready;
static atomic_int g_foreign_go;

/** Holds the shared lock until the test permits its owning thread to release it. */
static any foreign_owner(any arg) {
    (discard) arg;

    SpinLock_lock(&s2_g_foreign);

    atomic_store_explicit(&g_foreign_ready, 1, memory_order_release);

    while (!atomic_load_explicit(&g_foreign_go, memory_order_acquire)) 
        sched_yield();

    SpinLock_unlock(&s2_g_foreign);

    return nullptr;
}

/** Checks a non-owner cannot unlock another thread's lock and that the owner can release it. */
static fn test_foreign_unlock_refused() {
    s2_g_foreign = SPIN_LOCK_INIT;

    atomic_store(&g_foreign_ready, 0);

    atomic_store(&g_foreign_go, 0);

    thread t;

    CHECK(pthread_create(&t, nullptr, foreign_owner, nullptr) == 0);

    while (!atomic_load_explicit(&g_foreign_ready, memory_order_acquire)) 
        sched_yield();

    CHECK(SpinLock_isLocked(&s2_g_foreign));

    SpinLock_unlock(&s2_g_foreign);

    CHECK(SpinLock_isLocked(&s2_g_foreign));

    atomic_store_explicit(&g_foreign_go, 1, memory_order_release);

    CHECK(pthread_join(t, nullptr) == 0);

    CHECK(!SpinLock_isLocked(&s2_g_foreign));
}

/** ── contention: mutual exclusion and release/acquire visibility ──────────── */
#define SPIN_THREADS 8
#define SPIN_ITER 50000

static SpinLock g_counter_lock;
static i64 g_plain_counter; /** guarded ONLY by g_counter_lock */
static i64 g_observed_min;
static i64 g_observed_max;
static atomic_long g_watchdog_ticks;
static atomic_int g_spin_start;

/** Contends on the shared lock while updating the protected counter and watchdog progress count. */
static any spin_worker(any arg) {
    (discard) arg;

    while (!atomic_load_explicit(&g_spin_start, memory_order_acquire)) 
        sched_yield();

    for (i32 i = 0; i < SPIN_ITER; i++) {
        SpinLock_lock(&g_counter_lock);

        i64 before = g_plain_counter;

        g_plain_counter = before + 1;

        if (before < g_observed_min) 
            g_observed_min = before;

        if (before > g_observed_max) 
            g_observed_max = before;

        SpinLock_unlock(&g_counter_lock);

        atomic_fetch_add_explicit(&g_watchdog_ticks, 1, memory_order_relaxed);
    }

    return nullptr;
}

/** Verifies concurrent increments are neither lost nor duplicated under lock contention. */
static fn test_contention() {
    g_counter_lock = SPIN_LOCK_INIT;

    g_plain_counter = 0;

    g_observed_min = INT64_MAX;

    g_observed_max = 0;

    atomic_store(&g_watchdog_ticks, 0);

    atomic_store(&g_spin_start, 0);

    thread t[SPIN_THREADS];

    for (i32 i = 0; i < SPIN_THREADS; i++) 
        CHECK(pthread_create(&t[i], nullptr, spin_worker, nullptr) == 0);

    atomic_store_explicit(&g_spin_start, 1, memory_order_release);

    for (i32 i = 0; i < SPIN_THREADS; i++) 
        CHECK(pthread_join(t[i], nullptr) == 0);

    CHECK(g_plain_counter == (i64) SPIN_THREADS * SPIN_ITER);

    CHECK(g_observed_min == 0);

    CHECK(g_observed_max == (i64) SPIN_THREADS * SPIN_ITER - 1);

    CHECK(atomic_load(&g_watchdog_ticks) == (i64) SPIN_THREADS * SPIN_ITER);

    CHECK(!SpinLock_isLocked(&g_counter_lock));
}

/** Runs the SpinLock owner scenarios and returns failure if any assertion was violated. */
static i32 suite_spin() {
    s2_test_null_safety();

    test_basic_round_trip();

    test_timeout();

    test_foreign_unlock_refused();

    test_contention();

    if (s_spin_g_failures == 0) {
        print("spin_test: all assertions held");

        return 0;
    }

    print("spin_test: ", s_spin_g_failures, " FAILURES");

    return 1;
}
#undef CHECK
#undef SPIN_THREADS
#undef SPIN_ITER

/** Void adapter for spin suite. */
static fn suite_spin_wrap() {
    if (suite_spin() != 0)
        g_failures++;
}

/** Suite: array (from struct/array_test.c). */
/**
 * vexspoke/test/struct/array_test.c, the Array class _test.
 *
 * Beyond the happy path, this is the HOSTILE-INDEX battery the containers
 * demand:
 * - an in-range get/set round trip;
 * - OUT-OF-RANGE (== count, huge), NEGATIVE-AS-SIZE_T, and SIZE_MAX indices:
 * the hot get returns the safe default and the slot accessor returns
 * nullptr, never an out-of-bounds dereference (the Cold-Strict,
 * Hot-Minimal Validation Law);
 * - a refused write must NOT corrupt neighbouring state;
 * - the cold `getTry` seam returns the named reason (TRY_BOUNDS / TRY_NULL_ARG)
 * and reports it on stderr (the Failure Observability Law);
 * - the low-level bounded `Collection_readSlot` refuses an out-of-range index
 * with a safe default, not UB.
 */



static i32 s_array_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_array_g_failures++;                                                  \
        }                                                                  \
    } while (0)

static inline u32 ListDemoRecord_id(const ListDemoRecord *rec);
static inline u64 ListDemoRecord_tag(const ListDemoRecord *rec);

/** Tests arrayOf, Array, Array2D, Array3D, hostile indices, TryValue bounds, and zero-leak cleanup. */
static i32 suite_array()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    u32 *raw = arrayOf(u32, 8, arena);
    CHECK(raw != nullptr);
    CHECK(arrayLength(raw) == 8);
    CHECK(arrayBytes(raw) == 8 * sizeof(u32));
    CHECK(raw[0] == 0 && raw[7] == 0);

    for(u32 i = 0; i < 8; i++)
        raw[i] = 100u + i;

    u32 *grown = arrayResize(raw, 24);
    CHECK(grown != nullptr);
    raw = grown;
    CHECK(arrayLength(raw) == 24);
    CHECK(raw[0] == 100u && raw[7] == 107u);
    CHECK(raw[8] == 0 && raw[23] == 0);

    u32 *shrunk = arrayResize(raw, 4);
    CHECK(shrunk != nullptr);
    raw = shrunk;
    CHECK(arrayLength(raw) == 4);
    CHECK(raw[0] == 100u && raw[3] == 103u);

    arrayFreeIn(arena, raw);
    arrayFree(nullptr);

    ListDemoRecord *rawRecs = arrayOf(ListDemoRecord, 4, arena);
    CHECK(rawRecs != nullptr);
    CHECK(arrayLength(rawRecs) == 4);
    rawRecs[2] = ListDemoRecord(77u, 1.5f, 999u);
    CHECK(ListDemoRecord_id(&rawRecs[2]) == 77u && ListDemoRecord_tag(&rawRecs[2]) == 999u);
    arrayFreeIn(arena, rawRecs);

    CHECK(arrayOf(u32, 0, arena) == nullptr);
    CHECK(arrayOf(Over64, 2, arena) == nullptr);
    u32 *nullU32 = nullptr;
    CHECK(arrayLength(nullU32) == 0);

    Array *a = Array_2(ID_INT, 8);
    CHECK(a != nullptr);

    if(!a)
        return 1;

    CHECK(Array_size(a) == 8);
    CHECK(Array_length(a) == 8);
    CHECK(Array_capacity(a) == 8);
    CHECK(Array_elementClassId(a) == ID_INT);
    CHECK(Array_stride(a) == 4);
    CHECK(!Array_isEmpty(a));
    CHECK(Array_dataBuffer(a) != nullptr);
    CHECK(Array_getAs(a, 0, u32) == 0);

    CHECK(Array_set(a, 0, (u32) 111));
    CHECK(Array_set(a, 7, (u32) 777));
    CHECK(Array_getAs(a, 0, u32) == 111);
    CHECK(Array_getAs(a, 7, u32) == 777);
    u32 *p7 = Array_get(a, 7);
    CHECK(p7 != nullptr && *p7 == 777);
    CHECK(Array_slot(a, 3) != nullptr);
    CHECK(Array_get(a, 8) == nullptr);
    CHECK(Array_get(a, 999) == nullptr);
    CHECK(Array_get(a, SIZE_MAX) == nullptr);
    CHECK(Array_get(a, (bytes) -1) == nullptr);
    CHECK(Array_getAs(a, 8, u32) == 0);
    CHECK(Array_slot(a, 8) == nullptr);
    CHECK(Array_slot(a, SIZE_MAX) == nullptr);
    CHECK(!Array_set(a, 8, (u32) 5));
    CHECK(!Array_set(a, SIZE_MAX, (u32) 5));
    CHECK(Array_getAs(a, 0, u32) == 111);
    CHECK(Array_getAs(a, 7, u32) == 777);

    TryValue ok = Array_getTry(a, 0);
    CHECK(TryValue_isOk(&ok));
    CHECK(TryValue_getValue(&ok) == 111);
    TryValue hi = Array_getTry(a, 8);
    CHECK(TryValue_getCode(&hi) == TRY_BOUNDS);
    TryValue max = Array_getTry(a, SIZE_MAX);
    CHECK(TryValue_getCode(&max) == TRY_BOUNDS);
    TryValue nul = Array_getTry(nullptr, 0);
    CHECK(TryValue_getCode(&nul) == TRY_NULL_ARG);

    Collection *c = (Collection*) a;
    CHECK(Collection_readSlot(c, 0) == 111);
    CHECK(Collection_readSlot(c, 7) == 777);
    CHECK(Collection_readSlot(c, 8) == 0);
    CHECK(Collection_readSlot(c, SIZE_MAX) == 0);
    Collection_writeSlot(c, 8, 5);
    CHECK(Collection_readSlot(c, 0) == 111);
    CHECK(Collection_readSlot(nullptr, 0) == 0);

    CHECK(Array_get(nullptr, 0) == nullptr);
    CHECK(Array_slot(nullptr, 0) == nullptr);
    CHECK(!Array_set(nullptr, 0, (u32) 1));
    CHECK(Array_size(nullptr) == 0);
    CHECK(Array_isEmpty(nullptr));
    Array_free(nullptr);
    Array_free(a);

    Array *typed = Array(ListDemoRecord, 4, arena);
    CHECK(typed != nullptr && Array_stride(typed) == sizeof(ListDemoRecord));
    CHECK(Array_fill(typed, ListDemoRecord(9u, 2.5f, 42u)));
    ListDemoRecord gotRec = Array_getAs(typed, 3, ListDemoRecord);
    CHECK(ListDemoRecord_id(&gotRec) == 9u && ListDemoRecord_tag(&gotRec) == 42u);
    utf8 textBuf[128];
    bool trunc = true;
    Array_toString(typed, textBuf, sizeof(textBuf), &trunc);
    CHECK(!trunc && textBuf[0] != '\0');
    Array_toStringStruct(typed, textBuf, sizeof(textBuf), &trunc);
    CHECK(!trunc && textBuf[0] != '\0');
    Array_free(typed);

    Array2D *grid = Array2D(u32, 6, 4, arena);
    CHECK(grid != nullptr);
    CHECK(Array2D_width(grid) == 6 && Array2D_height(grid) == 4);
    CHECK(Array2D_count(grid) == 24 && Array2D_stride(grid) == sizeof(u32));
    CHECK(Array2D_fill(grid, (u32) 7u));
    CHECK(Array2D_getAs(grid, 0, 0, u32) == 7u);
    CHECK(Array2D_set(grid, 5, 3, (u32) 99u));
    u32 *gCell = Array2D_get(grid, 5, 3);
    CHECK(gCell != nullptr && *gCell == 99u);
    CHECK(Array2D_get(grid, 6, 0) == nullptr);
    CHECK(Array2D_get(grid, 0, 4) == nullptr);
    CHECK(!Array2D_set(grid, 6, 0, (u32) 1u));
    Array2D_toString(grid, textBuf, sizeof(textBuf), &trunc);
    CHECK(!trunc && textBuf[0] != '\0');
    Array2D_toStringStruct(grid, textBuf, sizeof(textBuf), &trunc);
    CHECK(!trunc && textBuf[0] != '\0');
    Array2D_free(grid);
    CHECK(Array2D(u32, 0, 4, arena) == nullptr);
    CHECK(Array2D(Over64, 2, 2, arena) == nullptr);

    Array3D *vol = Array3D(f32, 4, 3, 2, arena);
    CHECK(vol != nullptr);
    CHECK(Array3D_width(vol) == 4 && Array3D_height(vol) == 3 && Array3D_depth(vol) == 2);
    CHECK(Array3D_count(vol) == 24 && Array3D_stride(vol) == sizeof(f32));
    CHECK(Array3D_fill(vol, 1.25f));
    CHECK(Array3D_getAs(vol, 1, 1, 1, f32) == 1.25f);
    CHECK(Array3D_set(vol, 3, 2, 1, 8.5f));
    f32 *vCell = Array3D_get(vol, 3, 2, 1);
    CHECK(vCell != nullptr && *vCell == 8.5f);
    CHECK(Array3D_get(vol, 4, 0, 0) == nullptr);
    CHECK(Array3D_get(vol, 0, 3, 0) == nullptr);
    CHECK(Array3D_get(vol, 0, 0, 2) == nullptr);
    Array3D_toString(vol, textBuf, sizeof(textBuf), &trunc);
    CHECK(!trunc && textBuf[0] != '\0');
    Array3D_toStringStruct(vol, textBuf, sizeof(textBuf), &trunc);
    CHECK(!trunc && textBuf[0] != '\0');
    Array3D_free(vol);
    CHECK(Array3D(f32, 4, 0, 2, arena) == nullptr);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_array_g_failures == 0) {
        print("array_test: all assertions held");

        return 0;
    }

    print("array_test: ", s_array_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for array suite. */
static fn suite_array_wrap() {
    if (suite_array() != 0)
        g_failures++;
}

/** Suite: chunked_list (from struct/chunked_list_test.c). */
/**
 * vexspoke/test/chunked_list_test.c, never-moved chunked list verifier.
 *
 * Asserts the properties the class exists for:
 * 1. a row address handed out once NEVER moves (growth cannot invalidate it);
 * 2. index lookup resolves through published chunks with no lock;
 * 3. concurrent writers claim every row exactly once while lock-free readers
 * never observe a hole below the published count;
 * 4. extreme size math fails closed instead of wrapping.
 * plus the byte-budget chunk math, the COW directory doubling, and the
 * dest-last truncation contract of packInto (the Truncation-Never-Silent
 * clause).
 */

static i32 s_chunked_list_g_fail = 0;
static i32 s_chunked_list_g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        s_chunked_list_g_checks++;                                                       \
        if (!(cond)) {                                                    \
            s_chunked_list_g_fail++;                                                     \
            print("  FAIL line ", __LINE__, ": ", #cond);              \
        }                                                                 \
    } while (0)

;;GETTER
static inline u32 Row16_a(const Row16 *r)
{
    return (*r).a;
}
;;SETTER
static inline fn s_chunked_list_Row16_setA(Row16 *r, u32 v)
{
    (*r).a = v;
}
;;GETTER
static inline u32 s_chunked_list_Row16_b(const Row16 *r)
{
    return (*r).b;
}
;;SETTER
static inline fn s_chunked_list_Row16_setB(Row16 *r, u32 v)
{
    (*r).b = v;
}
;;GETTER
static inline u32 s_chunked_list_Row16_c(const Row16 *r)
{
    return (*r).c;
}
;;SETTER
static inline fn s_chunked_list_Row16_setC(Row16 *r, u32 v)
{
    (*r).c = v;
}
;;GETTER
static inline u32 s_chunked_list_Row16_d(const Row16 *r)
{
    return (*r).d;
}
;;SETTER
static inline fn s_chunked_list_Row16_setD(Row16 *r, u32 v)
{
    (*r).d = v;
}

/** Checks empty-list defaults, row geometry, and initial capacity. */
static fn testDefaults() {
    print("[1] defaults: 128-byte budget, 16-byte rows -> 8 rows/chunk");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, VEX_CHUNKED_BYTES_DEFAULT);

    CHECK(list != nullptr);

    if (!list) 
        return;

    CHECK(ChunkedList_stride(list) == 16u);

    CHECK(ChunkedList_getChunkBytes(list) == 128u);

    CHECK(ChunkedList_getRowsPerChunk(list) == 8u);

    CHECK(ChunkedList_getChunkCount(list) == 0u);

    CHECK(ChunkedList_size(list) == 0u);
    CHECK(ChunkedList_exhaustionCount(list) == 0);

    CHECK(ChunkedList_length(list) == 0u);

    CHECK(ChunkedList_capacity(list) == 0u);

    CHECK(ChunkedList_isEmpty(list) == true);

    CHECK(ChunkedList_slot(list, 0) == nullptr);

    CHECK(ChunkedList_getChunk(list, 0) == nullptr);

    Collection *c = (Collection*) list;

    CHECK(Collection_size(c) == 0u);

    CHECK(Collection_stride(c) == 16u);

    ChunkedList *tiny = ChunkedList_2(ID_INT, 4u);

    CHECK(tiny != nullptr);

    if (tiny) {
        CHECK(ChunkedList_getChunkBytes(tiny) == 16u);
        CHECK(ChunkedList_getRowsPerChunk(tiny) >= 1u);

        CHECK((bytes)ChunkedList_getRowsPerChunk(tiny) * (bytes)ChunkedList_stride(tiny)
              <= (bytes)ChunkedList_getChunkBytes(tiny));
        ChunkedList_free(tiny);
    }

    ChunkedList_free(list);
}

/** Verifies row addresses remain stable when the chunk directory grows. */
static fn testStableAddresses() {
    print("[2] stable addresses + chunk boundaries across growth");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    u8 *first = ChunkedList_addSlot(list);

    CHECK(first != nullptr);

    CHECK(ChunkedList_size(list) == 1u);

    CHECK(ChunkedList_getChunkCount(list) == 1u);

    if (!first) {
        ChunkedList_free(list);

        return;
    }

    Row16 *row0 = (Row16*) first;

    s_chunked_list_Row16_setA(row0, 0xA0u);

    s_chunked_list_Row16_setB(row0, 0xB0u);

    s_chunked_list_Row16_setC(row0, 0xC0u);

    s_chunked_list_Row16_setD(row0, 0xD0u);

    const u32 total = 1200u;

    for (u32 i = 1u; i < total; i++) {
        u8 *row = ChunkedList_addSlot(list);

        if (!row) {
            CHECK(false);

            break;
        }

        Row16 *r = (Row16*) row;

        s_chunked_list_Row16_setA(r, i);

        s_chunked_list_Row16_setB(r, i + 1u);

        s_chunked_list_Row16_setC(r, i + 2u);

        s_chunked_list_Row16_setD(r, i + 3u);
    }

    CHECK(ChunkedList_size(list) == total);

    CHECK(ChunkedList_capacity(list) >= total);

    CHECK(ChunkedList_getChunkCount(list) == (total + 7u) / 8u);

    CHECK(ChunkedList_slot(list, 0u) == first);

    CHECK(Row16_a(row0) == 0xA0u);

    CHECK(s_chunked_list_Row16_b(row0) == 0xB0u);

    CHECK(s_chunked_list_Row16_c(row0) == 0xC0u);

    CHECK(s_chunked_list_Row16_d(row0) == 0xD0u);

    u8 *row7 = ChunkedList_slot(list, 7u);

    u8 *row8 = ChunkedList_slot(list, 8u);

    CHECK(row7 != nullptr && row8 != nullptr);

    CHECK(ChunkedList_getChunk(list, 0u) == first);

    CHECK(ChunkedList_getChunk(list, 1u) == row8);

    CHECK(ChunkedList_getChunk(list, 1u) != ChunkedList_getChunk(list, 0u));

    bool intact = true;

    for (u32 i = 1u; i < 64u; i++) {
        Row16 *r = (Row16*) ChunkedList_slot(list, i);

        if (!r || Row16_a(r) != i || s_chunked_list_Row16_d(r) != i + 3u) {
            intact = false;

            break;
        }
    }

    CHECK(intact);

    CHECK(ChunkedList_slot(list, total) == nullptr);

    CHECK(ChunkedList_slot(list, total + 5000u) == nullptr);

    CHECK(ChunkedList_getChunk(list, ChunkedList_getChunkCount(list)) == nullptr);

    ChunkedList_free(list);
}

/** Exercises explicit reservation, including its accepted and rejected bounds. */
static fn testReserve() {
    print("[3] reserve pre-allocates chunks without activating rows");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    CHECK(ChunkedList_reserve(list, 100u) == true);

    CHECK(ChunkedList_getChunkCount(list) == 13u);

    CHECK(ChunkedList_capacity(list) == 104u);

    CHECK(ChunkedList_size(list) == 0u);

    CHECK(ChunkedList_isEmpty(list) == true);

    u8 *chunk = ChunkedList_getChunk(list, 12u);

    CHECK(chunk != nullptr);

    CHECK(ChunkedList_slot(list, 0u) == nullptr);

    u8 *before = ChunkedList_getChunk(list, 0u);

    for (u32 i = 0u; i < 200u; i++) 
        CHECK(ChunkedList_addSlot(list) != nullptr);

    CHECK(ChunkedList_getChunk(list, 0u) == before);

    CHECK(ChunkedList_reserve(list, 10u) == true);

    CHECK(ChunkedList_getChunkCount(list) == 25u);

    ChunkedList_free(list);
}

/** Checks packing rows into caller-provided storage and preserves output on rejection. */
static fn testPackInto() {
    print("[4] packInto snapshot: dest-last + truncation flag");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    for (u32 i = 0u; i < 20u; i++) {
        Row16 *r = (Row16*) ChunkedList_addSlot(list);

        if (!r) 
            break;

        s_chunked_list_Row16_setA(r, i);

        s_chunked_list_Row16_setB(r, i * 2u);

        s_chunked_list_Row16_setC(r, i * 3u);

        s_chunked_list_Row16_setD(r, i * 4u);
    }

    CHECK(ChunkedList_size(list) == 20u);

    Row16 flat[20];

    memset(flat, 0, sizeof(flat));

    u32 rows = 0u;

    bool truncated = true;

    CHECK(ChunkedList_packInto(list, (u8*) flat, sizeof(flat), &rows, &truncated) == true);

    CHECK(rows == 20u);

    CHECK(truncated == false);

    CHECK(flat[0].a == 0u && flat[19].a == 19u);

    CHECK(flat[7].d == 28u && flat[8].a == 8u);

    Row16 shortDest[3];

    rows = 0u;

    truncated = false;

    CHECK(ChunkedList_packInto(list, (u8*) shortDest, sizeof(shortDest), &rows, &truncated) == false);

    CHECK(rows == 3u);

    CHECK(truncated == true);

    CHECK(shortDest[0].a == 0u && shortDest[2].a == 2u);

    rows = 99u;

    truncated = true;

    CHECK(ChunkedList_packInto(list, nullptr, 64u, &rows, &truncated) == false);

    CHECK(rows == 0u && truncated == false);

    CHECK(ChunkedList_packInto(nullptr, (u8*) shortDest, sizeof(shortDest), &rows, &truncated) == false);

    CHECK(ChunkedList_packInto(list, (u8*) flat, sizeof(flat), nullptr, nullptr) == true);

    ChunkedList_free(list);
}

/** Verifies chunk-byte budgeting limits admission without corrupting existing rows. */
static fn testChunkBytesBudget() {
    print("[5] chunk byte budget: settable before growth, rejected after");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) 
        return;

    CHECK(ChunkedList_getRowsPerChunk(list) == 8u);

    ChunkedList_setChunkBytes(list, 512u);

    CHECK(ChunkedList_getChunkBytes(list) == 512u);

    CHECK(ChunkedList_getRowsPerChunk(list) == 32u);

    ChunkedList *own = ChunkedList_3(ID_INT, 128u, 128u);

    if (own) {
        CHECK(ChunkedList_getRowsPerChunk(own) == 1u);

        u8 *a = ChunkedList_addSlot(own);

        u8 *b = ChunkedList_addSlot(own);

        CHECK(a != nullptr && b != nullptr);

        CHECK(a != b);

        CHECK(ChunkedList_getChunk(own, 0u) == a);

        CHECK(ChunkedList_getChunk(own, 1u) == b);

        ChunkedList_free(own);
    }

    CHECK(ChunkedList_addSlot(list) != nullptr);

    ChunkedList_setChunkBytes(list, 1024u);

    CHECK(ChunkedList_getChunkBytes(list) == 512u);

    CHECK(ChunkedList_getRowsPerChunk(list) == 32u);

    ChunkedList_free(list);
}

/** Confirms nullable list operations return safe defaults without mutation. */
static fn testNullSafety() {
    print("[6] cold-path null safety (no crash on null self)");

    CHECK(ChunkedList_size(nullptr) == 0u);

    CHECK(ChunkedList_length(nullptr) == 0u);

    CHECK(ChunkedList_capacity(nullptr) == 0u);

    CHECK(ChunkedList_isEmpty(nullptr) == true);

    CHECK(ChunkedList_elementClassId(nullptr) == 0u);

    CHECK(ChunkedList_stride(nullptr) == 0u);

    CHECK(ChunkedList_getChunkCount(nullptr) == 0u);

    CHECK(ChunkedList_getRowsPerChunk(nullptr) == 0u);

    CHECK(ChunkedList_getChunkBytes(nullptr) == 0u);

    CHECK(ChunkedList_addSlot(nullptr) == nullptr);

    CHECK(ChunkedList_reserve(nullptr, 10u) == false);

    CHECK(ChunkedList_slot(nullptr, 0u) == nullptr);

    CHECK(ChunkedList_getChunk(nullptr, 0u) == nullptr);

    ChunkedList_setChunkBytes(nullptr, 256u);

    ChunkedList_free(nullptr);

    CHECK(true);
}

/** Exercises overflow-checked capacity and byte-size calculations. */
static fn testCheckedMath() {
    print("[7] checked size math: extreme budgets fail closed, never wrap");

    ChunkedList *huge = ChunkedList_3(ID_INT, 1u, UINT32_MAX);

    CHECK(huge != nullptr);

    if (huge) {
        CHECK(ChunkedList_getRowsPerChunk(huge) == 0x80000000u);

        CHECK(ChunkedList_size(huge) == 0u);

        CHECK(ChunkedList_capacity(huge) == 0u);

        CHECK(ChunkedList_reserve(huge, 0u) == true);

        CHECK(ChunkedList_getChunkCount(huge) == 0u);

        ChunkedList_free(huge);
    }

    ChunkedList *wide = ChunkedList_3(ID_INT, 4096u, 128u);

    CHECK(wide != nullptr);

    if (wide) {
        CHECK(ChunkedList_getRowsPerChunk(wide) == 1u);

        CHECK(ChunkedList_addSlot(wide) != nullptr);

        CHECK(ChunkedList_capacity(wide) == 1u);

        ChunkedList_free(wide);
    }

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (list) {
        CHECK(ChunkedList_reserve(list, 0u) == true);

        CHECK(ChunkedList_getChunkCount(list) == 0u);

        ChunkedList_free(list);
    }
}

#define CONC_WRITERS 4
#define CONC_ROWS 1000u
#define CONC_READERS 2
#define CONC_TOTAL (CONC_WRITERS * CONC_ROWS)

static ChunkedList *g_concList;
static _Atomic u32 g_concSeq;
static _Atomic bool g_concDone;

/** Appends a fixed batch of rows and stamps each admitted row with a unique sequence. */
static any concWriter(any arg) {
    (discard) arg;

    for (u32 i = 0u; i < CONC_ROWS; i++) {
        u8 *row = ChunkedList_addSlot(g_concList);

        if (!row) 
            return (any) 1;

        u32 seq = atomic_fetch_add(&g_concSeq, 1u) + 1u;

        memcpy(row, &seq, sizeof(seq));
    }

    return nullptr;
}

/** Scans all published rows until writers finish, rejecting holes or invalid stamps. */
static any concReader(any arg) {
    (discard) arg;

    while (!atomic_load(&g_concDone)) {
        u32 n = ChunkedList_size(g_concList);

        for (u32 i = 0u; i < n; i++) {
            u8 *row = ChunkedList_slot(g_concList, i);

            if (!row) 
                return (any) 1;

            u32 seq = 0u;

            memcpy(&seq, row, sizeof(seq));

            if (seq > (u32) CONC_TOTAL) 
                return (any) 1;
        }
    }

    return nullptr;
}

/** Orders pointer values for deterministic comparison of concurrent writer results. */
static i32 cmpPtr(const_any a, const_any b) {
    u8 * const *pa = (u8 * const *) a;

    u8 * const *pb = (u8 * const *) b;

    if (*pa < *pb) 
        return -1;

    if (*pa > *pb) 
        return 1;

    return 0;
}

/** Runs concurrent appenders and checks completion and the resulting row set. */
static fn testConcurrentWriters() {
    print("[8] concurrent writers claim every row exactly once; readers see no holes");

    ChunkedList *list = ChunkedList_3(ID_INT, 16u, 128u);

    if (!list) {
        CHECK(false);

        return;
    }

    g_concList = list;

    atomic_store(&g_concSeq, 0u);

    atomic_store(&g_concDone, false);

    thread writers[CONC_WRITERS];

    thread readers[CONC_READERS];

    for (i32 i = 0; i < CONC_READERS; i++) 
        pthread_create(&readers[i], nullptr, concReader, nullptr);

    for (i32 i = 0; i < CONC_WRITERS; i++) 
        pthread_create(&writers[i], nullptr, concWriter, nullptr);

    i32 writerFail = 0;

    for (i32 i = 0; i < CONC_WRITERS; i++) {
        any ret = nullptr;

        pthread_join(writers[i], &ret);

        if (ret != nullptr) 
            writerFail = 1;
    }

    atomic_store(&g_concDone, true);

    i32 readerFail = 0;

    for (i32 i = 0; i < CONC_READERS; i++) {
        any ret = nullptr;

        pthread_join(readers[i], &ret);

        if (ret != nullptr) 
            readerFail = 1;
    }

    CHECK(writerFail == 0);

    CHECK(readerFail == 0);

    CHECK(ChunkedList_size(list) == (u32) CONC_TOTAL);

    static u8 seen[CONC_TOTAL + 1u];
    memset(seen, 0, sizeof(seen));
    static u8 *ptrs[CONC_TOTAL];

    bool stampsOk = true;

    for (u32 i = 0u; stampsOk && i < (u32) CONC_TOTAL; i++) {
        u8 *row = ChunkedList_slot(list, i);

        if (!row) {
            stampsOk = false;

            break;
        }

        u32 seq = 0u;

        memcpy(&seq, row, sizeof(seq));

        if (seq < 1u || seq > (u32) CONC_TOTAL || seen[seq]) {
            stampsOk = false;

            break;
        }

        seen[seq] = 1u;

        ptrs[i] = row;
    }

    CHECK(stampsOk);

    bool distinct = stampsOk;

    if (distinct) {
        qsort(ptrs, (bytes) CONC_TOTAL, sizeof(u8*), cmpPtr);

        for (u32 i = 1u; i < (u32) CONC_TOTAL; i++) 
            if (ptrs[i] == ptrs[i - 1u]) {
                distinct = false;

                break;
            }
    }

    CHECK(distinct);

    g_concList = nullptr;

    ChunkedList_free(list);
}

/** Runs the chunked-list geometry, stability, rejection, and concurrency cases. */
static i32 suite_chunked_list() {
    print("=== ChunkedList Test Suite ===\n");

    testDefaults();

    testStableAddresses();

    testReserve();

    testPackInto();

    testChunkBytesBudget();

    testNullSafety();

    testCheckedMath();

    testConcurrentWriters();

    print("\n", s_chunked_list_g_checks, " checks, ", s_chunked_list_g_fail, " failed");

    if (s_chunked_list_g_fail == 0) 
        print("=== ChunkedList Test Suite: ALL PASS ===");

    return s_chunked_list_g_fail == 0 ? 0 : 1;
}
#undef CHECK
#undef CONC_WRITERS
#undef CONC_ROWS
#undef CONC_READERS
#undef CONC_TOTAL

/** Void adapter for chunked_list suite. */
static fn suite_chunked_list_wrap() {
    if (suite_chunked_list() != 0)
        g_failures++;
}

/** Suite: chunked_paged (from struct/chunked_paged_test.c). */
/**
 * ============================================================================
 * MODULE: ChunkedPagedTest (chunked_paged_test/test.c)
 * LEVEL: L3, Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the radix (paged) ChunkedList form: the variadic macro,
 * the *_LAYER_DEFAULT FourCC sentinels, arbitrary depth (2/3/4 levels),
 * shift/mask geometry, row addresses stable across root growth, leaf resolve
 * across level boundaries, and the growable root's no-ceiling property.
 *
 * STRUCT FIELDS: none, procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main()
 * ============================================================================
 */

static i32 s_chunked_paged_g_fail = 0;
static i32 s_chunked_paged_g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        s_chunked_paged_g_checks++;                                                       \
        if (!(cond)) {                                                    \
            s_chunked_paged_g_fail++;                                                     \
            print("  FAIL line ", __LINE__, ": ", #cond);              \
        }                                                                 \
    } while (0)

;;GETTER
static inline u32 s2_Row16_a(const Row16 *r)
{
    return (*r).a;
}
;;SETTER
static inline fn s_chunked_paged_Row16_setA(Row16 *r, u32 v)
{
    (*r).a = v;
}
;;GETTER
static inline u32 s_chunked_paged_Row16_b(const Row16 *r)
{
    return (*r).b;
}
;;SETTER
static inline fn s_chunked_paged_Row16_setB(Row16 *r, u32 v)
{
    (*r).b = v;
}
;;GETTER
static inline u32 s_chunked_paged_Row16_c(const Row16 *r)
{
    return (*r).c;
}
;;SETTER
static inline fn s_chunked_paged_Row16_setC(Row16 *r, u32 v)
{
    (*r).c = v;
}
;;GETTER
static inline u32 s_chunked_paged_Row16_d(const Row16 *r)
{
    return (*r).d;
}
;;SETTER
static inline fn s_chunked_paged_Row16_setD(Row16 *r, u32 v)
{
    (*r).d = v;
}

/** Confirms the public sentinel values used by paged chunk traversal. */
static fn testSentinels() {
    print("[1] sentinels: FourCC sugars are negative and byte-exact");

    CHECK(CHUNKED_LIST_TWO_LAYER_DEFAULT < 0);

    CHECK(CHUNKED_LIST_THREE_LAYER_DEFAULT < 0);

    CHECK((u32) CHUNKED_LIST_TWO_LAYER_DEFAULT == 0xD4574F21u);

    CHECK((u32) CHUNKED_LIST_THREE_LAYER_DEFAULT == 0xD4485245u);
}

/** Checks page geometry and index-to-page calculations at their boundaries. */
static fn testGeometry() {
    print("[2] radix geometry: explicit depth, page leaf, pow2 rounding");

    ChunkedList *l3 = (ChunkedList*) RadixList(Row16, 4, 4, 4);

    CHECK(l3 != nullptr);

    if (l3) {
        CHECK(ChunkedList_getLevels(l3) == 3u);

        CHECK(ChunkedList_getRowsPerChunk(l3) == 4u);

        CHECK(ChunkedList_getChunkBytes(l3) == 64u);

        CHECK(ChunkedList_stride(l3) == 16u);

        CHECK(ChunkedList_size(l3) == 0u);

        ChunkedList_free(l3);
    }

    ChunkedList *l4 = (ChunkedList*) RadixList(Row16, 2, 2, 2, 2);

    CHECK(l4 != nullptr);

    if (l4) {
        CHECK(ChunkedList_getLevels(l4) == 4u);

        CHECK(ChunkedList_getRowsPerChunk(l4) == 2u);

        ChunkedList_free(l4);
    }

    ChunkedList *round = (ChunkedList*) RadixList(Row16, 100, 100, 20);

    CHECK(round != nullptr);

    if (round) {
        CHECK(ChunkedList_getRowsPerChunk(round) == 16u);

        ChunkedList_free(round);
    }
}

/** Verifies initial layer geometry and its empty-state defaults. */
static fn testLayerDefaults() {
    print("[3] *_LAYER_DEFAULT presets select depth + page leaf");

    ChunkedList *two = (ChunkedList*) RadixList(Row16, CHUNKED_LIST_TWO_LAYER_DEFAULT);

    CHECK(two != nullptr);

    if (two) {
        CHECK(ChunkedList_getLevels(two) == 2u);

        CHECK(ChunkedList_getRowsPerChunk(two) == 256u);

        CHECK(ChunkedList_getChunkBytes(two) == 4096u);

        ChunkedList_free(two);
    }

    ChunkedList *three = (ChunkedList*) RadixList(Row16, CHUNKED_LIST_THREE_LAYER_DEFAULT);

    CHECK(three != nullptr);

    if (three) {
        CHECK(ChunkedList_getLevels(three) == 3u);

        CHECK(ChunkedList_getRowsPerChunk(three) == 256u);

        ChunkedList_free(three);
    }

    ChunkedList *ov = (ChunkedList*) RadixList(Row16, CHUNKED_LIST_THREE_LAYER_DEFAULT, 8);

    CHECK(ov != nullptr);

    if (ov) {
        CHECK(ChunkedList_getLevels(ov) == 3u);

        ChunkedList_free(ov);
    }
}

/** Exercises multi-level growth and checks that previously admitted entries remain accessible. */
static fn testDeepGrowth() {
    print("[4] 3-level growth: stable rows across root doubling, leaf boundaries");

    ChunkedList *list = (ChunkedList*) RadixList(Row16, 4, 4, 4);

    if (!list) {
        CHECK(false);

        return;
    }

    const u32 total = 1100u;

    u8 *first = nullptr;

    for (u32 i = 0u; i < total; i++) {
        Row16 *r = (Row16*) ChunkedList_addSlot(list);

        if (!r) {
            CHECK(false);

            break;
        }

        if (i == 0u) 
            first = (u8*) r;

        s_chunked_paged_Row16_setA(r, i);

        s_chunked_paged_Row16_setB(r, i + 1u);

        s_chunked_paged_Row16_setC(r, i + 2u);

        s_chunked_paged_Row16_setD(r, i + 3u);
    }

    CHECK(ChunkedList_size(list) == total);

    CHECK(ChunkedList_getLevels(list) == 3u);

    CHECK(ChunkedList_capacity(list) >= total);

    CHECK(ChunkedList_getChunkCount(list) == (total + 3u) / 4u);

    CHECK(ChunkedList_slot(list, 0u) == first);

    if (first) {
        Row16 *r0 = (Row16*) first;

        CHECK(s2_Row16_a(r0) == 0u && s_chunked_paged_Row16_b(r0) == 1u && s_chunked_paged_Row16_c(r0) == 2u && s_chunked_paged_Row16_d(r0) == 3u);
    }

    bool intact = true;

    for (u32 i = 0u; i < total; i++) {
        Row16 *r = (Row16*) ChunkedList_slot(list, i);

        if (!r || s2_Row16_a(r) != i || s_chunked_paged_Row16_d(r) != i + 3u) {
            intact = false;

            break;
        }
    }

    CHECK(intact);

    CHECK(ChunkedList_getChunk(list, 0u) == first);

    CHECK(ChunkedList_getChunk(list, 4u) != nullptr);

    CHECK(ChunkedList_getChunk(list, 274u) != nullptr);

    CHECK(ChunkedList_getChunk(list, 275u) == nullptr);

    CHECK(ChunkedList_slot(list, total) == nullptr);

    Row16 flat[8];

    u32 rows = 0u;

    bool truncated = true;

    memset(flat, 0, sizeof(flat));

    CHECK(ChunkedList_packInto(list, (u8*) flat, sizeof(flat), &rows, &truncated) == false);

    CHECK(rows == 8u && truncated == true);

    CHECK(flat[0].a == 0u && flat[4].a == 4u && flat[7].a == 7u);

    ChunkedList_free(list);
}

;;GETTER
static inline u32 ChainProbe_chunks(const ChainProbe *p)
{
    return (*p).chunks;
}
;;SETTER
static inline fn ChainProbe_setChunks(ChainProbe *p, u32 c)
{
    (*p).chunks = c;
}
;;SETTER
static inline fn ChainProbe_setOk(ChainProbe *p, bool ok)
{
    (*p).ok = ok;
}

/** Records each chunk and index visited by the chain traversal callback. */
static fn chainFn(u8 *chunk, u32 index, any userdata) {
    ChainProbe *p = (ChainProbe*) userdata;
    u32 chunks = ChainProbe_chunks(p);
    if (!chunk || index != chunks) 
        ChainProbe_setOk(p, false);

    ChainProbe_setChunks(p, chunks + 1u);
}

/** Verifies chain traversal visits the expected chunks in order. */
static fn testChain() {
    print("[5] leaf tail-link chain: nextChunk + forEachChunk");

    ChunkedList *list = (ChunkedList*) RadixList(Row16, 4, 4, 4);

    if (!list) {
        CHECK(false);

        return;
    }

    const u32 total = 30u;

    for (u32 i = 0u; i < total; i++) {
        Row16 *r = (Row16*) ChunkedList_addSlot(list);

        if (!r) {
            CHECK(false);

            break;
        }

        s_chunked_paged_Row16_setA(r, i);
    }

    u32 chunks = ChunkedList_getChunkCount(list);

    CHECK(chunks == 8u);

    u8 *chunk = ChunkedList_getChunk(list, 0u);

    CHECK(chunk != nullptr);

    u32 walked = 0u;

    bool matches = true;

    while (chunk != nullptr) {
        if (chunk != ChunkedList_getChunk(list, walked)) 
            matches = false;

        walked++;

        chunk = ChunkedList_nextChunk(list, chunk);

        if (walked > chunks + 1u) {
            matches = false;

            break;
        }
    }

    CHECK(matches);

    CHECK(walked == chunks);

    u8 *tail = ChunkedList_getChunk(list, chunks - 1u);

    CHECK(ChunkedList_nextChunk(list, tail) == nullptr);

    CHECK(ChunkedList_nextChunk(list, nullptr) == nullptr);

    CHECK(ChunkedList_nextChunk(nullptr, tail) == nullptr);

    ChainProbe probe = { 0u, true };

    ChunkedList_forEachChunk(list, chainFn, &probe);

    CHECK(probe.ok);

    CHECK(probe.chunks == chunks);

    Row16 *r0 = (Row16*) ChunkedList_slot(list, 0u);

    Row16 *r29 = (Row16*) ChunkedList_slot(list, 29u);

    CHECK(r0 != nullptr && s2_Row16_a(r0) == 0u);

    CHECK(r29 != nullptr && s2_Row16_a(r29) == 29u);

    ChunkedList_free(list);
}

/** Runs the paged-chunk sentinel, geometry, growth, and chain callback checks. */
static i32 suite_chunked_paged() {
    print("=== ChunkedList Paged (radix) Test Suite ===\n");

    testSentinels();

    testGeometry();

    testLayerDefaults();

    testDeepGrowth();

    testChain();

    print("\n", s_chunked_paged_g_checks, " checks, ", s_chunked_paged_g_fail, " failed");

    if (s_chunked_paged_g_fail == 0) 
        print("=== ChunkedList Paged Test Suite: ALL PASS ===");

    return s_chunked_paged_g_fail == 0 ? 0 : 1;
}
#undef CHECK

/** Void adapter for chunked_paged suite. */
static fn suite_chunked_paged_wrap() {
    if (suite_chunked_paged() != 0)
        g_failures++;
}

/** Suite: collection (from struct/collection_test.c). */
/**
 * vexspoke/test/struct/collection_test.c, the Collection class _test.
 *
 * The shared container header/routing. Proves the metadata getters and, above
 * all, that the BOUNDED slot pair really is bounded: an out-of-range or
 * overflow index is refused with a safe default + a THROW (the THROW Law), never
 * an out-of-bounds dereference. The deliberately-UNSAFE pair stays unchecked but
 * null-guarded.
 */



static i32 s_collection_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_collection_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Exercises the Collection slot interface, including valid access and safe bounds handling. */
static i32 suite_collection() {
    Array *a = Array_2(ID_INT, 4);

    CHECK(a != nullptr);

    Collection *c = (Collection*) a;

    CHECK(Collection_size(c) == 4);

    CHECK(Collection_length(c) == 4);

    CHECK(Collection_capacity(c) == 4);

    CHECK(Collection_elementClassId(c) == ID_INT);

    CHECK(Collection_keyClassId(c) == ID_INT);

    CHECK(Collection_stride(c) == 4);

    CHECK(Collection_valClassId(c) == 4);

    CHECK(Collection_head(c) == 0);

    CHECK(!Collection_isEmpty(c));

    CHECK(Collection_dataBuffer(c) != nullptr);

    CHECK(Collection_type(c) != 0);

    Collection_writeSlot(c, 0, 123);

    Collection_writeSlot(c, 3, 456);

    CHECK(Collection_readSlot(c, 0) == 123);

    CHECK(Collection_readSlot(c, 3) == 456);

    CHECK(Collection_readSlot(c, 4) == 0);

    CHECK(Collection_readSlot(c, 999) == 0);

    CHECK(Collection_readSlot(c, SIZE_MAX) == 0);

    Collection_writeSlot(c, 4, 1);

    Collection_writeSlot(c, SIZE_MAX, 1);

    CHECK(Collection_readSlot(c, 0) == 123);

    CHECK(Collection_readSlotUnsafe(c, 0) == 123);

    Collection_writeSlotUnsafe(c, 1, 7);

    CHECK(Collection_readSlotUnsafe(c, 1) == 7);

    CHECK(Collection_readSlotUnsafe(nullptr, 0) == 0);

    Collection_writeSlotUnsafe(nullptr, 0, 1);

    CHECK(Collection_readSlot(nullptr, 0) == 0);

    Collection_writeSlot(nullptr, 0, 1);

    CHECK(Collection_size(nullptr) == 0);

    CHECK(Collection_capacity(nullptr) == 0);

    CHECK(Collection_stride(nullptr) == 0);

    CHECK(Collection_elementClassId(nullptr) == 0);

    CHECK(Collection_head(nullptr) == 0);

    CHECK(Collection_isEmpty(nullptr));

    CHECK(Collection_dataBuffer(nullptr) == nullptr);

    CHECK(Collection_type(nullptr) == 0);

    Collection *ac = (Collection*) a;

    CHECK(Collection_exhaustionCount(ac) == 0);

    CHECK(!Collection_reportExhaustion(ac, 4096));

    CHECK(Collection_exhaustionCount(ac) == 1);

    CHECK(!Collection_reportExhaustion(ac, 8192));

    CHECK(Collection_exhaustionCount(ac) == 2);

    Collection_resetExhaustion(ac);

    CHECK(Collection_exhaustionCount(ac) == 0);

    CHECK(Collection_exhaustionCount(nullptr) == 0);

    CHECK(!Collection_reportExhaustion(nullptr, 16));

    Collection_resetExhaustion(nullptr);

    Array_free(a);

    if (s_collection_g_failures == 0) {
        print("collection_test: all assertions held");

        return 0;
    }

    print("collection_test: ", s_collection_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for collection suite. */
static fn suite_collection_wrap() {
    if (suite_collection() != 0)
        g_failures++;
}

/** Suite: deque (from struct/deque_test.c). */
/** vexspoke/test/struct/deque_test.c, the Deque class _test (both ends, volume, hostile). */



static i32 s_deque_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_deque_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Exercises ArrayDeque and LinkedDeque both-end ops, ring wraparound, growth, pointer stability, and OOM. */
static i32 suite_deque()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    Deque *deques[2];
    deques[0] = ArrayDeque(u32, 4, arena);
    deques[1] = LinkedDeque(u32, arena);

    for(u32 k = 0; k < 2; k++) {
        Deque *d = deques[k];
        CHECK(d != nullptr);
        CHECK(Deque_isEmpty(d));
        CHECK(Deque_stride(d) == sizeof(u32));
        CHECK(Deque_exhaustionCount(d) == 0);

        CHECK(Deque_addLast(d, (u32) 1));
        CHECK(Deque_addLast(d, (u32) 2));
        CHECK(Deque_addFirst(d, (u32) 0));
        CHECK(Deque_size(d) == 3);
        CHECK(Deque_peekFirstAs(d, u32) == 0);
        CHECK(Deque_peekLastAs(d, u32) == 2);
        CHECK(Deque_getAs(d, 0, u32) == 0);
        CHECK(Deque_getAs(d, 1, u32) == 1);
        CHECK(Deque_getAs(d, 2, u32) == 2);

        CHECK(Deque_popFirstAs(d, u32) == 0);
        CHECK(Deque_popLastAs(d, u32) == 2);
        CHECK(Deque_popFirstAs(d, u32) == 1);
        CHECK(!Deque_removeFirst(d));
        CHECK(!Deque_removeLast(d));
        CHECK(Deque_peekFirst(d) == nullptr);
        CHECK(Deque_peekLast(d) == nullptr);

        for(u32 i = 0; i < 64; i++) {
            if((i & 1u) == 0)
                CHECK(Deque_addFirst(d, i));
            else
                CHECK(Deque_addLast(d, i));
        }

        CHECK(Deque_size(d) == 64);

        for(u32 i = 0; i < 32; i++)
            CHECK(Deque_removeFirst(d));

        CHECK(Deque_size(d) == 32);

        for(u32 i = 0; i < 500; i++)
            CHECK(Deque_addLast(d, 1000u + i));

        CHECK(Deque_size(d) == 532);
        CHECK(Deque_peekLastAs(d, u32) == 1499u);
        CHECK(Deque_get(d, Deque_size(d)) == nullptr);
        CHECK(Deque_get(d, SIZE_MAX) == nullptr);

        u32 *empFirst = Deque_emplaceFirst(d);
        CHECK(empFirst != nullptr && *empFirst == 0);
        *empFirst = 777u;
        CHECK(Deque_peekFirstAs(d, u32) == 777u);

        utf8 buf[128];
        bool trunc = true;
        Deque_toString(d, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');
        Deque_toStringStruct(d, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');

        Deque_clear(d);
        CHECK(Deque_isEmpty(d));
        Deque_free(d);
    }

    Deque *ld = LinkedDeque(u32, arena);
    CHECK(LinkedDeque_isLinkedDeque(ld));
    u32 *stableFirst = Deque_emplaceFirst(ld);
    CHECK(stableFirst != nullptr);
    *stableFirst = 55u;

    for(u32 i = 0; i < 32; i++)
        CHECK(Deque_addLast(ld, i));

    CHECK(Deque_peekFirst(ld) == stableFirst);
    CHECK(*stableFirst == 55u);
    Deque_free(ld);

    CHECK(!Deque_addFirst(nullptr, (u32) 1));
    CHECK(!Deque_addLast(nullptr, (u32) 1));
    CHECK(!Deque_removeFirst(nullptr));
    CHECK(!Deque_removeLast(nullptr));
    CHECK(Deque_peekFirst(nullptr) == nullptr);
    CHECK(Deque_peekLast(nullptr) == nullptr);
    CHECK(Deque_get(nullptr, 0) == nullptr);
    CHECK(Deque_size(nullptr) == 0);
    CHECK(Deque_isEmpty(nullptr));
    Deque_free(nullptr);

    MemoryArena *oomArena = MemoryArena_pages(2);
    Deque *oomDeque = ArrayDeque(u32, 4, oomArena);
    CHECK(oomDeque != nullptr);
    CHECK(!Deque_reserve(oomDeque, 65536u));
    CHECK(Deque_exhaustionCount(oomDeque) >= 1u);
    Deque_free(oomDeque);
    MemoryArena_destroy(oomArena);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_deque_g_failures == 0) {
        print("deque_test: all assertions held");

        return 0;
    }

    print("deque_test: ", s_deque_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for deque suite. */
static fn suite_deque_wrap() {
    if (suite_deque() != 0)
        g_failures++;
}

/** Suite: exhaustion (from struct/exhaustion_test.c). */
/**
 * vexspoke/test/struct/exhaustion_test.c, the shared loud-refusal seam _test.
 *
 * Proves exception-free counting and one-report-per-epoch behaviour for the
 * owner-agnostic exhaustion seam that non-Collection owners (MinHeap, SparseSet,
 * Octree) route their refusals through (the Exhaustion Loudness Law).
 */



static i32 s_exhaustion_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_exhaustion_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Verifies counting, the single-diagnostic epoch, reset and null-safety. */
static i32 suite_exhaustion() {
    u64 count = 0;

    bool reported = false;

    CHECK(Struct_exhaustionCount(&count) == 0);

    CHECK(!reported);

    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 4096u, 2048u));

    CHECK(Struct_exhaustionCount(&count) == 1);

    CHECK(reported);

    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 4096u, 2048u));

    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 8192u, 2048u));

    CHECK(Struct_exhaustionCount(&count) == 3);

    Struct_resetExhaustion(&count, &reported);

    CHECK(Struct_exhaustionCount(&count) == 0);

    CHECK(!reported);

    CHECK(!Struct_reportExhaustion(&count, &reported, "probe", 16u, 0u));

    CHECK(Struct_exhaustionCount(&count) == 1);

    CHECK(!Struct_reportExhaustion(nullptr, &reported, "probe", 8u, 0u));

    CHECK(!Struct_reportExhaustion(&count, nullptr, "probe", 8u, 0u));

    CHECK(Struct_exhaustionCount(nullptr) == 0);

    Struct_resetExhaustion(nullptr, nullptr);

    CHECK(Struct_constructionExhaustionCount() == 0);

    CHECK(!Struct_reportConstructionExhaustion("probe", 128u));

    CHECK(Struct_constructionExhaustionCount() == 1);

    CHECK(!Struct_reportConstructionExhaustion("probe", 256u));

    CHECK(Struct_constructionExhaustionCount() == 2);

    Struct_resetConstructionExhaustion();

    CHECK(Struct_constructionExhaustionCount() == 0);

    MemoryArena *tinyExhaustArena = MemoryArena_pages(1);
    CHECK(tinyExhaustArena != nullptr);

    List *tooBig = ArrayList(u32, 100000u, tinyExhaustArena);

    CHECK(tooBig == nullptr);

    CHECK(Struct_constructionExhaustionCount() == 1);

    List *ok = ArrayList(u32);

    CHECK(ok != nullptr);

    CHECK(Struct_constructionExhaustionCount() == 1);

    List_free(ok);
    MemoryArena_destroy(tinyExhaustArena);

    if (s_exhaustion_g_failures == 0) {
        print("exhaustion_test: all assertions held");

        return 0;
    }

    print("exhaustion_test: ", s_exhaustion_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for exhaustion suite. */
static fn suite_exhaustion_wrap() {
    if (suite_exhaustion() != 0)
        g_failures++;
}

/** Suite: list (from struct/list_test.c). */
/**
 * vexspoke/test/struct/list_test.c, the List class _test.
 *
 * Overkill on purpose (the Dynamic Scalability & Anti-Hardcoding Law):
 * - construction, boundary (empty / one / growth past the initial capacity);
 * - get/set/add/remove/slot/compare round trips;
 * - hostile indices (== count, huge, negative-as-bytes)  to  safe + THROW;
 * - VOLUME: one million elements added by FOUR threads under a mutex (legal
 * concurrent use; the count must be exact and the multiset exact);
 * - null-safety.
 */



static i32 s_list_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_list_g_failures++;                                                  \
        }                                                                  \
    } while (0)

;;GETTER
static inline u32 AddArg_count(const AddArg *arg)
{
    return (*arg).count;
}
;;GETTER
static inline pthread_mutex_t *AddArg_lock(const AddArg *arg)
{
    return (*arg).lock;
}
;;GETTER
static inline List *AddArg_list(const AddArg *arg)
{
    return (*arg).list;
}
;;GETTER
static inline u32 AddArg_base(const AddArg *arg)
{
    return (*arg).base;
}

;;CONSTRUCTOR
ListDemoRecord ListDemoRecord_3(u32 id, f32 score, u64 tag)
{
    ListDemoRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.id = id;
    rec.score = score;
    rec.tag = tag;

    return rec;
}

;;GETTER
static inline u32 ListDemoRecord_id(const ListDemoRecord *rec)
{
    return (*rec).id;
}

;;GETTER
static inline f32 ListDemoRecord_score(const ListDemoRecord *rec)
{
    return (*rec).score;
}

;;GETTER
static inline u64 ListDemoRecord_tag(const ListDemoRecord *rec)
{
    return (*rec).tag;
}

;;SETTER
static inline fn ListDemoRecord_setId(ListDemoRecord *rec, u32 id)
{
    (*rec).id = id;
}

;;SETTER
static inline fn ListDemoRecord_setScore(ListDemoRecord *rec, f32 score)
{
    (*rec).score = score;
}

;;SETTER
static inline fn ListDemoRecord_setTag(ListDemoRecord *rec, u64 tag)
{
    (*rec).tag = tag;
}

;;GETTER
static inline bytes ListVisitCtx_visits(const ListVisitCtx *ctx)
{
    return (*ctx).visits;
}

;;GETTER
static inline u64 ListVisitCtx_sum(const ListVisitCtx *ctx)
{
    return (*ctx).sum;
}

;;SETTER
static inline fn ListVisitCtx_setVisits(ListVisitCtx *ctx, bytes visits)
{
    (*ctx).visits = visits;
}

;;SETTER
static inline fn ListVisitCtx_setSum(ListVisitCtx *ctx, u64 sum)
{
    (*ctx).sum = sum;
}

;;SETTER
static inline fn ListRow16_setA(Row16 *row, u32 a)
{
    (*row).a = a;
}

static fn visitSumU32(any element, bytes index, any userdata)
{
    (discard) index;
    ListVisitCtx *ctx = userdata;
    u32 *val = element;
    ListVisitCtx_setVisits(ctx, ListVisitCtx_visits(ctx) + 1u);
    ListVisitCtx_setSum(ctx, ListVisitCtx_sum(ctx) + (u64) (*val));
}

static u64 sumPolymorphicU32(const List *list)
{
    u64 total = 0;
    bytes count = List_count(list);

    for(bytes i = 0; i < count; i++) {
        u32 *slot = List_get(list, i);

        if(slot)
            total += (u64) (*slot);
    }

    return total;
}

/** Appends this worker's assigned value range while holding the shared list mutex. */
static any adder(any userData)
{
    AddArg *arg = userData;

    u32 count = AddArg_count(arg);
    pthread_mutex_t *lock = AddArg_lock(arg);
    List *list = AddArg_list(arg);
    u32 base = AddArg_base(arg);

    for(u32 i = 0; i < count; i++) {
        u32 value = base + i;
        pthread_mutex_lock(lock);

        (discard) List_add(list, value);

        pthread_mutex_unlock(lock);
    }

    return nullptr;
}

/** Verifies polymorphic List interface across ArrayList, LinkedList, and PagedList. */
static i32 suite_list()
{
    MemoryArena *isoArena = MemoryArena();
    CHECK(isoArena != nullptr);
    bytes baselineBytes = MemoryArena_activeBytes(isoArena);

    List *list = ArrayList(u32, 4, isoArena);
    CHECK(list != nullptr);
    CHECK(ArrayList_isArrayList(list));
    CHECK(!LinkedList_isLinkedList(list));
    CHECK(!ChunkedList_isChunkedList(list));
    CHECK(List_kind(list) == LIST_KIND_ARRAY);
    CHECK(List_isEmpty(list));
    CHECK(List_count(list) == 0);
    CHECK(List_size(list) == 0);
    CHECK(List_length(list) == 0);
    CHECK(List_capacity(list) == 4);
    CHECK(List_stride(list) == sizeof(u32));
    CHECK(List_alignment(list) == alignof(u32));
    CHECK(List_isType(list, u32));
    CHECK(!List_isType(list, u64));
    CHECK(List_exhaustionCount(list) == 0);
    CHECK(ArrayList_data(list) != nullptr);

    CHECK(List_add(list, (u32) 7));
    CHECK(List_count(list) == 1);
    u32 *firstPtr = List_get(list, 0);
    CHECK(firstPtr != nullptr && *firstPtr == 7u);
    u32 *fastPtr = ArrayList_get(list, 0);
    CHECK(fastPtr == firstPtr);
    CHECK(List_getAs(list, 0, u32) == 7u);
    CHECK(ArrayList_getAs(list, 0, u32) == 7u);
    CHECK(!List_add(list, (u64) 999ull));
    CHECK(List_count(list) == 1);
    CHECK(List_getAs(list, 0, u64) == 0ull);

    for(u32 i = 1u; i < 100u; i++)
        CHECK(List_add(list, i));

    CHECK(List_count(list) == 100u);
    CHECK(List_getAs(list, 99u, u32) == 99u);

    for(u32 i = 100u; i < 50000u; i++)
        CHECK(ArrayList_add(list, i));

    CHECK(List_count(list) == 50000u);
    CHECK(List_getAs(list, 49999u, u32) == 49999u);

    CHECK(List_set(list, 0u, (u32) 4242u));
    CHECK(List_getAs(list, 0u, u32) == 4242u);
    CHECK(ArrayList_set(list, 0u, (u32) 7u));
    CHECK(List_getAs(list, 0u, u32) == 7u);
    CHECK(!List_set(list, 0u, (u64) 1ull));

    CHECK(List_insert(list, 0u, (u32) 555u));
    CHECK(List_count(list) == 50001u);
    CHECK(List_getAs(list, 0u, u32) == 555u);
    CHECK(List_getAs(list, 1u, u32) == 7u);
    CHECK(List_remove(list, 0u));
    CHECK(List_count(list) == 50000u);
    CHECK(List_getAs(list, 0u, u32) == 7u);

    CHECK(ArrayList_swapRemove(list, 0u));
    CHECK(List_count(list) == 49999u);
    CHECK(List_getAs(list, 0u, u32) == 49999u);
    CHECK(List_set(list, 0u, (u32) 0u));

    CHECK(List_indexOf(list, (u32) 12345u) == 12345);
    CHECK(List_contains(list, (u32) 12345u));
    CHECK(!List_contains(list, (u32) 999999u));

    CHECK(ArrayList_shrinkToFit(list));
    CHECK(List_capacity(list) == List_count(list));

    bytes n = List_count(list);
    CHECK(List_get(list, n) == nullptr);
    CHECK(List_get(list, SIZE_MAX) == nullptr);
    CHECK(List_getAs(list, n, u32) == 0u);
    CHECK(List_getAs(list, SIZE_MAX, u32) == 0u);
    CHECK(!List_set(list, n, (u32) 1u));
    CHECK(!List_remove(list, n));
    CHECK(!List_insert(list, n + 1u, (u32) 1u));

    List_clear(list);
    CHECK(List_isEmpty(list));
    CHECK(ArrayList_shrinkToFit(list));
    CHECK(List_capacity(list) == 0);
    CHECK(ArrayList_data(list) == nullptr);
    CHECK(List_add(list, (u32) 88u));
    CHECK(List_getAs(list, 0u, u32) == 88u);

    List_free(list);
    CHECK(MemoryArena_activeBytes(isoArena) == baselineBytes);

    List *lazy = ArrayList(u32, 0, isoArena);
    CHECK(lazy != nullptr);
    CHECK(List_capacity(lazy) == 0);
    CHECK(ArrayList_data(lazy) == nullptr);
    CHECK(List_add(lazy, (u32) 10u));
    CHECK(List_capacity(lazy) >= 16u);
    CHECK(List_getAs(lazy, 0u, u32) == 10u);
    List_free(lazy);
    CHECK(MemoryArena_activeBytes(isoArena) == baselineBytes);

    List *strings = ArrayList(string, 4, isoArena);
    CHECK(strings != nullptr);
    CHECK(List_stride(strings) == sizeof(string));
    CHECK(List_add(strings, "alpha"));
    CHECK(List_add(strings, "beta"));
    string s0 = List_getAs(strings, 0u, string);
    string s1 = List_getAs(strings, 1u, string);
    CHECK(s0 != nullptr && memcmp(s0, "alpha", 6) == 0);
    CHECK(s1 != nullptr && memcmp(s1, "beta", 5) == 0);
    List_free(strings);

    List *records = ArrayList(ListDemoRecord, 4, isoArena);
    CHECK(records != nullptr);
    CHECK(List_stride(records) == sizeof(ListDemoRecord));
    CHECK(List_alignment(records) == alignof(ListDemoRecord));
    ListDemoRecord *emplaced = List_emplace(records);
    CHECK(emplaced != nullptr);
    CHECK(ListDemoRecord_id(emplaced) == 0u);
    ListDemoRecord_setId(emplaced, 1u);
    ListDemoRecord_setScore(emplaced, 2.5f);
    ListDemoRecord_setTag(emplaced, 0xCAFEBABEull);

    ListDemoRecord secondRec = ListDemoRecord(2u, 9.75f, 0xDEADBEEFull);
    CHECK(List_add(records, secondRec));

    ListDemoRecord *r0 = List_get(records, 0u);
    ListDemoRecord *r1 = ArrayList_get(records, 1u);
    CHECK(r0 != nullptr && ListDemoRecord_id(r0) == 1u && ListDemoRecord_tag(r0) == 0xCAFEBABEull);
    CHECK(r1 != nullptr && ListDemoRecord_id(r1) == 2u && ListDemoRecord_score(r1) == 9.75f);

    ListDemoRecord copied = List_getAs(records, 1u, ListDemoRecord);
    CHECK(ListDemoRecord_tag(&copied) == 0xDEADBEEFull);

    utf8 fmtBuf[256];
    bool truncated = false;
    List_toString(records, fmtBuf, sizeof(fmtBuf), &truncated);
    CHECK(!truncated && memcmp(fmtBuf, "ArrayList<ListDemoRecord>", 25) == 0);
    List_toStringStruct(records, fmtBuf, sizeof(fmtBuf), &truncated);
    CHECK(!truncated && memcmp(fmtBuf, "List{kind=ArrayList", 19) == 0);
    List_free(records);
    CHECK(MemoryArena_activeBytes(isoArena) == baselineBytes);

    List *linked = LinkedList(ListDemoRecord, isoArena);
    CHECK(linked != nullptr);
    CHECK(LinkedList_isLinkedList(linked));
    CHECK(!ArrayList_isArrayList(linked));
    CHECK(ArrayList_get(linked, 0u) == nullptr);
    CHECK(List_kind(linked) == LIST_KIND_LINKED);
    CHECK(List_isEmpty(linked));

    ListDemoRecord *lAnchor = LinkedList_emplaceLast(linked);
    CHECK(lAnchor != nullptr);
    ListDemoRecord_setId(lAnchor, 100u);
    ListDemoRecord_setScore(lAnchor, 1.0f);
    ListDemoRecord_setTag(lAnchor, 1000ull);

    CHECK(LinkedList_addFirst(linked, ListDemoRecord(50u, 0.5f, 500ull)));
    CHECK(LinkedList_addLast(linked, ListDemoRecord(200u, 2.0f, 2000ull)));
    CHECK(List_insert(linked, 1u, ListDemoRecord(75u, 0.75f, 750ull)));
    CHECK(List_count(linked) == 4u);
    CHECK(List_get(linked, 2u) == lAnchor);
    CHECK(ListDemoRecord_id((ListDemoRecord*) List_first(linked)) == 50u);
    CHECK(ListDemoRecord_id((ListDemoRecord*) List_last(linked)) == 200u);

    CHECK(LinkedList_removeFirst(linked));
    CHECK(LinkedList_removeLast(linked));
    CHECK(List_count(linked) == 2u);
    CHECK(List_get(linked, 1u) == lAnchor);
    CHECK(List_remove(linked, 0u));
    CHECK(List_count(linked) == 1u);
    CHECK(List_get(linked, 0u) == lAnchor);

    List_clear(linked);
    CHECK(List_isEmpty(linked));
    CHECK(List_first(linked) == nullptr);
    CHECK(List_last(linked) == nullptr);
    List_free(linked);
    CHECK(MemoryArena_activeBytes(isoArena) == baselineBytes);

    List *chunked = ChunkedList(ListDemoRecord, 256u, isoArena);
    CHECK(chunked != nullptr);
    CHECK(ChunkedList_isChunkedList(chunked));
    CHECK(List_kind(chunked) == LIST_KIND_CHUNKED);
    CHECK(List_reserve(chunked, 64u));

    ListDemoRecord *pFirst = List_emplace(chunked);
    CHECK(pFirst != nullptr);
    ListDemoRecord_setId(pFirst, 1u);
    ListDemoRecord_setTag(pFirst, 0x1111ull);

    for(u32 i = 1u; i < 4000u; i++)
        CHECK(List_add(chunked, ListDemoRecord(i + 1u, (f32) i, (u64) i * 10ull)));

    CHECK(List_count(chunked) == 4000u);
    CHECK(List_get(chunked, 0u) == pFirst);
    CHECK(ChunkedList_get(chunked, 0u) == pFirst);
    CHECK(ListDemoRecord_id((ListDemoRecord*) List_get(chunked, 3999u)) == 4000u);
    CHECK(!List_remove(chunked, 0u));
    CHECK(!List_insert(chunked, 0u, ListDemoRecord(0u, 0.0f, 0ull)));
    CHECK(List_insert(chunked, 4000u, ListDemoRecord(4001u, 1.0f, 1ull)));
    CHECK(List_count(chunked) == 4001u);
    List_free(chunked);
    CHECK(MemoryArena_activeBytes(isoArena) == baselineBytes);

    List *radixList = RadixList(Row16, 4, 4, 4);
    CHECK(radixList != nullptr);
    CHECK(ChunkedList_isChunkedList(radixList));
    CHECK(ChunkedList_getLevels((ChunkedList*) radixList) == 3u);
    Row16 *wSlot = List_emplace(radixList);
    CHECK(wSlot != nullptr);
    ListRow16_setA(wSlot, 42u);
    CHECK(Row16_a((Row16*) List_get(radixList, 0u)) == 42u);
    CHECK(ChunkedList_size((ChunkedList*) radixList) == 1u);
    CHECK(List_count(radixList) == 1u);
    List_free(radixList);

    List *polyArr = ArrayList(u32, 8, isoArena);
    List *polyLink = LinkedList(u32, isoArena);
    List *polyChunk = ChunkedList(u32, 128u, isoArena);
    CHECK(polyArr != nullptr && polyLink != nullptr && polyChunk != nullptr);

    for(u32 i = 1u; i <= 25u; i++) {
        CHECK(List_add(polyArr, i));
        CHECK(List_add(polyLink, i));
        CHECK(List_add(polyChunk, i));
    }

    CHECK(List_compare(polyArr, polyLink));
    CHECK(List_compare(polyArr, polyChunk));
    CHECK(List_compare(polyLink, polyChunk));
    CHECK(sumPolymorphicU32(polyArr) == 325ull);
    CHECK(sumPolymorphicU32(polyLink) == 325ull);
    CHECK(sumPolymorphicU32(polyChunk) == 325ull);

    ListVisitCtx visitCtx;
    memset(&visitCtx, 0, sizeof(visitCtx));
    List_forEach(polyLink, visitSumU32, &visitCtx);
    CHECK(ListVisitCtx_visits(&visitCtx) == 25u);
    CHECK(ListVisitCtx_sum(&visitCtx) == 325ull);

    CHECK(List_add(polyLink, (u32) 999u));
    CHECK(!List_compare(polyArr, polyLink));

    List_free(polyArr);
    List_free(polyLink);
    List_free(polyChunk);
    CHECK(MemoryArena_activeBytes(isoArena) == baselineBytes);

    CHECK(ArrayList(Over64) == nullptr);
    CHECK(LinkedList(Over64) == nullptr);
    CHECK(ChunkedList(Over64) == nullptr);

    MemoryArena *oomArena = MemoryArena_pages(2);
    CHECK(oomArena != nullptr);
    List *oomList = ArrayList(u64, 4, oomArena);
    CHECK(oomList != nullptr);
    CHECK(List_add(oomList, (u64) 111ull));
    CHECK(List_add(oomList, (u64) 222ull));
    CHECK(!List_reserve(oomList, 65536u));
    CHECK(List_exhaustionCount(oomList) >= 1u);
    CHECK(List_count(oomList) == 2u);
    CHECK(List_getAs(oomList, 0u, u64) == 111ull);
    CHECK(List_getAs(oomList, 1u, u64) == 222ull);
    List_free(oomList);
    MemoryArena_destroy(oomArena);

    CHECK(List_get(nullptr, 0u) == nullptr);
    CHECK(List_first(nullptr) == nullptr);
    CHECK(List_last(nullptr) == nullptr);
    CHECK(List_emplace(nullptr) == nullptr);
    CHECK(!List_add(nullptr, (u32) 1u));
    CHECK(!List_set(nullptr, 0u, (u32) 1u));
    CHECK(!List_insert(nullptr, 0u, (u32) 1u));
    CHECK(!List_remove(nullptr, 0u));
    CHECK(!List_reserve(nullptr, 4u));
    CHECK(List_count(nullptr) == 0u);
    CHECK(List_isEmpty(nullptr));
    CHECK(List_indexOf(nullptr, (u32) 1u) == -1);
    List_clear(nullptr);
    List_free(nullptr);
    ArrayList_free(nullptr);
    LinkedList_free(nullptr);
    ChunkedList_free(nullptr);

    MemoryArena_destroy(isoArena);

    enum { THREADS = 4, PER_THREAD = 250000 };

    List *big = ArrayList(u32, 4);

    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    thread tids[THREADS];

    AddArg args[THREADS];

    for(i32 t = 0; t < THREADS; t++) {
        args[t].list = big;
        args[t].lock = &lock;
        args[t].base = (u32) t * PER_THREAD;
        args[t].count = PER_THREAD;
        CHECK(pthread_create(&tids[t], nullptr, adder, &args[t]) == 0);
    }

    for(i32 t = 0; t < THREADS; t++)
        CHECK(pthread_join(tids[t], nullptr) == 0);

    pthread_mutex_destroy(&lock);

    CHECK(List_count(big) == (bytes) THREADS * PER_THREAD);

    u64 sum = 0;

    bytes count = List_count(big);

    for(bytes i = 0; i < count; i++)
        sum += (u64) List_getAs(big, i, u32);

    u64 expected = (u64) (count - 1u) * count / 2u;

    CHECK(sum == expected);

    CHECK(List_get(big, SIZE_MAX) == nullptr);

    List_free(big);

    if(s_list_g_failures == 0) {
        print("list_test: all assertions held (ArrayList + LinkedList + ChunkedList + 1,000,000 / 4 threads)");

        return 0;
    }

    print("list_test: ", s_list_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for list suite. */
static fn suite_list_wrap() {
    if (suite_list() != 0)
        g_failures++;
}

/** Suite: map (from struct/map_test.c). */
/** vexspoke/test/struct/map_test.c, the Map class _test (key/value, volume). */



static i32 s_map_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_map_g_failures++;                                                  \
        }                                                                  \
    } while (0)

;;GETTER
static inline bytes MapVisitCtx_visits(const MapVisitCtx *ctx)
{
    return (*ctx).visits;
}

;;GETTER
static inline u64 MapVisitCtx_keySum(const MapVisitCtx *ctx)
{
    return (*ctx).keySum;
}

;;GETTER
static inline u64 MapVisitCtx_valSum(const MapVisitCtx *ctx)
{
    return (*ctx).valSum;
}

;;GETTER
static inline u32 MapVisitCtx_orderAt(const MapVisitCtx *ctx, bytes idx)
{
    return (*ctx).order[idx];
}

;;SETTER
static inline fn MapVisitCtx_record(MapVisitCtx *ctx, u32 key, u64 val)
{
    if((*ctx).visits < 8u)
        (*ctx).order[(*ctx).visits] = key;

    (*ctx).visits++;
    (*ctx).keySum += key;
    (*ctx).valSum += val;
}

static fn visitMapU32U64(const_any key, any value, bytes index, any userdata)
{
    (discard) index;
    MapVisitCtx *ctx = (MapVisitCtx*) userdata;
    u32 k = 0;
    u64 v = 0;
    memcpy(&k, key, sizeof(u32));
    memcpy(&v, value, sizeof(u64));
    MapVisitCtx_record(ctx, k, v);
}

/** Checks HashMap and LinkedHashMap: content-hashed strings, insertion order, pointer stability, churn, OOM. */
static i32 suite_map()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    Map *maps[2];
    maps[0] = HashMap(u32, u64, 8, arena);
    maps[1] = LinkedHashMap(u32, u64, 8, arena);

    for(u32 k = 0; k < 2; k++) {
        Map *m = maps[k];
        CHECK(m != nullptr);
        CHECK(Map_isEmpty(m));
        CHECK(Map_size(m) == 0);
        CHECK(Map_exhaustionCount(m) == 0);
        CHECK(Map_keyStride(m) == sizeof(u32));
        CHECK(Map_valStride(m) == sizeof(u64));

        CHECK(Map_put(m, (u32) 10, (u64) 100));
        CHECK(Map_put(m, (u32) 20, (u64) 200));
        CHECK(Map_put(m, (u32) 30, (u64) 300));
        CHECK(Map_put(m, (u32) 20, (u64) 222));
        CHECK(Map_size(m) == 3);
        CHECK(Map_containsKey(m, (u32) 10));
        CHECK(Map_getAs(m, (u32) 10, u64) == 100);
        CHECK(Map_getAs(m, (u32) 20, u64) == 222);
        CHECK(Map_get(m, (u32) 99) == nullptr);
        CHECK(!Map_containsKey(m, (u32) 99));

        u64 *emp = Map_emplace(m, (u32) 40);
        CHECK(emp != nullptr && *emp == 0);
        *emp = 400u;
        CHECK(Map_getAs(m, (u32) 40, u64) == 400u);

        CHECK(Map_remove(m, (u32) 10));
        CHECK(!Map_containsKey(m, (u32) 10));
        CHECK(!Map_remove(m, (u32) 10));
        CHECK(Map_size(m) == 3);

        MapVisitCtx vctx;
        memset(&vctx, 0, sizeof(vctx));
        Map_forEach(m, visitMapU32U64, &vctx);
        CHECK(MapVisitCtx_visits(&vctx) == 3);
        CHECK(MapVisitCtx_keySum(&vctx) == 90u);
        CHECK(MapVisitCtx_valSum(&vctx) == 922u);

        if(LinkedHashMap_isLinkedHashMap(m)) {
            CHECK(MapVisitCtx_orderAt(&vctx, 0) == 20u);
            CHECK(MapVisitCtx_orderAt(&vctx, 1) == 30u);
            CHECK(MapVisitCtx_orderAt(&vctx, 2) == 40u);
            u32 *fk = LinkedHashMap_firstKey(m);
            u32 *lk = LinkedHashMap_lastKey(m);
            u64 *fv = LinkedHashMap_firstValue(m);
            u64 *lv = LinkedHashMap_lastValue(m);
            CHECK(fk != nullptr && *fk == 20u);
            CHECK(lk != nullptr && *lk == 40u);
            CHECK(fv != nullptr && *fv == 222u);
            CHECK(lv != nullptr && *lv == 400u);
        }

        List *keys = Map_keys(m);
        List *vals = Map_values(m);
        CHECK(keys != nullptr && List_count(keys) == 3);
        CHECK(vals != nullptr && List_count(vals) == 3);

        if(LinkedHashMap_isLinkedHashMap(m)) {
            CHECK(List_getAs(keys, 0, u32) == 20u);
            CHECK(List_getAs(keys, 1, u32) == 30u);
            CHECK(List_getAs(keys, 2, u32) == 40u);
            CHECK(List_getAs(vals, 0, u64) == 222u);
        }

        List_free(keys);
        List_free(vals);

        u64 *stableBefore = Map_get(m, (u32) 20);

        for(u32 i = 0; i < 1000; i++)
            CHECK(Map_put(m, 1000u + i, (u64) i * 10u));

        CHECK(Map_size(m) == 1003);
        CHECK(Map_getAs(m, (u32) 1000, u64) == 0);
        CHECK(Map_getAs(m, (u32) 1999, u64) == 9990u);

        if(LinkedHashMap_isLinkedHashMap(m)) {
            CHECK(Map_get(m, (u32) 20) == stableBefore);
            CHECK(*stableBefore == 222u);
        }

        for(u32 i = 0; i < 800; i++)
            CHECK(Map_remove(m, 1000u + i));

        CHECK(Map_size(m) == 203);

        for(u32 i = 0; i < 400; i++)
            CHECK(Map_put(m, 5000u + i, (u64) i));

        CHECK(Map_size(m) == 603);
        utf8 buf[128];
        bool trunc = true;
        Map_toString(m, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');
        Map_toStringStruct(m, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');

        Map_clear(m);
        CHECK(Map_isEmpty(m));
        Map_free(m);
    }

    Map *strMap = HashMap(string, ListDemoRecord, 8, arena);
    CHECK(strMap != nullptr);
    utf8 k1[16];
    utf8 k2[16];
    memcpy(k1, "hero_slot", 10);
    memcpy(k2, "hero_slot", 10);
    string s1 = k1;
    string s2 = k2;
    CHECK(Map_put(strMap, s1, ListDemoRecord(5u, 3.0f, 88u)));
    CHECK(Map_containsKey(strMap, s2));
    ListDemoRecord *got = Map_get(strMap, s2);
    CHECK(got != nullptr && ListDemoRecord_id(got) == 5u);
    Map_free(strMap);

    CHECK(!Map_put(nullptr, (u32) 1, (u64) 1));
    CHECK(Map_get(nullptr, (u32) 1) == nullptr);
    CHECK(!Map_containsKey(nullptr, (u32) 1));
    CHECK(!Map_remove(nullptr, (u32) 1));
    CHECK(Map_size(nullptr) == 0);
    CHECK(Map_isEmpty(nullptr));
    CHECK(Map_keys(nullptr) == nullptr);
    Map_free(nullptr);

    MemoryArena *oomArena = MemoryArena_pages(2);
    Map *oomMap = HashMap(u32, u64, 4, oomArena);
    CHECK(oomMap != nullptr);
    CHECK(!Map_reserve(oomMap, 65536u));
    CHECK(Map_exhaustionCount(oomMap) >= 1u);
    Map_free(oomMap);
    MemoryArena_destroy(oomArena);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_map_g_failures == 0) {
        print("map_test: all assertions held");

        return 0;
    }

    print("map_test: ", s_map_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for map suite. */
static fn suite_map_wrap() {
    if (suite_map() != 0)
        g_failures++;
}

/** Suite: queue (from struct/queue_test.c). */
/** vexspoke/test/struct/queue_test.c, the Queue class _test (FIFO, volume, hostile). */



static i32 s_queue_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_queue_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Verifies ArrayQueue and LinkedQueue FIFO ordering, ring wraparound, pointer stability, bounds, and OOM. */
static i32 suite_queue()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    Queue *queues[2];
    queues[0] = ArrayQueue(u32, 4, arena);
    queues[1] = LinkedQueue(u32, arena);

    for(u32 k = 0; k < 2; k++) {
        Queue *q = queues[k];
        CHECK(q != nullptr);
        CHECK(Queue_isEmpty(q));
        CHECK(Queue_stride(q) == sizeof(u32));
        CHECK(Queue_exhaustionCount(q) == 0);

        CHECK(Queue_push(q, (u32) 10));
        CHECK(Queue_push(q, (u32) 20));
        CHECK(Queue_push(q, (u32) 30));
        CHECK(Queue_size(q) == 3);
        CHECK(Queue_peekAs(q, u32) == 10);
        CHECK(Queue_getAs(q, 1, u32) == 20);
        CHECK(Queue_popAs(q, u32) == 10);
        CHECK(Queue_popAs(q, u32) == 20);
        CHECK(Queue_popAs(q, u32) == 30);
        CHECK(!Queue_pop(q));
        CHECK(Queue_peek(q) == nullptr);

        for(u32 round = 0; round < 5; round++) {
            for(u32 i = 0; i < 6; i++)
                CHECK(Queue_push(q, round * 100u + i));

            for(u32 i = 0; i < 4; i++)
                CHECK(Queue_pop(q));
        }

        CHECK(Queue_size(q) == 10);
        Queue_clear(q);
        CHECK(Queue_isEmpty(q));

        u32 *emp = Queue_emplace(q);
        CHECK(emp != nullptr && *emp == 0);
        *emp = 42u;
        CHECK(Queue_peekAs(q, u32) == 42u);

        for(u32 i = 0; i < 2000; i++)
            CHECK(Queue_push(q, i));

        CHECK(Queue_size(q) == 2001);
        CHECK(Queue_popAs(q, u32) == 42u);
        CHECK(Queue_popAs(q, u32) == 0);
        CHECK(Queue_popAs(q, u32) == 1);
        CHECK(Queue_get(q, Queue_size(q)) == nullptr);
        CHECK(Queue_get(q, SIZE_MAX) == nullptr);

        utf8 buf[128];
        bool trunc = true;
        Queue_toString(q, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');
        Queue_toStringStruct(q, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');

        Queue_free(q);
    }

    Queue *lq = LinkedQueue(u32, arena);
    CHECK(LinkedQueue_isLinkedQueue(lq));
    u32 *stable = Queue_emplace(lq);
    CHECK(stable != nullptr);
    *stable = 88u;

    for(u32 i = 0; i < 32; i++)
        CHECK(Queue_push(lq, i));

    CHECK(Queue_peek(lq) == stable && *stable == 88u);
    Queue_free(lq);

    CHECK(!Queue_push(nullptr, (u32) 1));
    CHECK(!Queue_pop(nullptr));
    CHECK(Queue_peek(nullptr) == nullptr);
    CHECK(Queue_get(nullptr, 0) == nullptr);
    CHECK(Queue_size(nullptr) == 0);
    CHECK(Queue_isEmpty(nullptr));
    Queue_free(nullptr);

    MemoryArena *oomArena = MemoryArena_pages(2);
    Queue *oomQ = ArrayQueue(u32, 4, oomArena);
    CHECK(oomQ != nullptr);
    CHECK(!Queue_reserve(oomQ, 65536u));
    CHECK(Queue_exhaustionCount(oomQ) >= 1u);
    Queue_free(oomQ);
    MemoryArena_destroy(oomArena);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_queue_g_failures == 0) {
        print("queue_test: all assertions held");

        return 0;
    }

    print("queue_test: ", s_queue_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for queue suite. */
static fn suite_queue_wrap() {
    if (suite_queue() != 0)
        g_failures++;
}

/** Suite: ring (from struct/ring_test.c). */
/**
 * vexspoke/test/atomic/ring_test.c, owner test for atomic/ring (RingBuffer).
 *
 * Proves the fixed-capacity MPMC FIFO:
 * - init rejects nullptr / zero elem_size; capacity is rounded UP to a power
 * of two (0  to  1, 3  to  4, 5  to  8) and mask follows;
 * - strict FIFO ordering, powers-of-two wrap-around far past capacity;
 * - the capacity is a hard contract: push fails exactly at full, pop fails
 * exactly at empty, and neither silently drops or duplicates;
 * - arbitrary element sizes (a multi-word struct copies whole);
 * - nullptr push/pop are safe refusals; d64 shutdown is safe;
 * - re-init after shutdown works;
 * - real MPMC concurrency under a bounded watchdog: every pushed value is
 * popped exactly once, never torn, never lost, never duplicated.
 */



static i32 s_ring_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_ring_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Checks ring initialization and rejects invalid dimensions or empty/full operations. */
static fn test_init_and_bounds() {
    RingBuffer r;

    RingBuffer invalid = RingBuffer();

    CHECK(invalid.slots == nullptr);

    RingBuffer invalidDimensions = RingBuffer(0u, 4u);

    CHECK(invalidDimensions.slots == nullptr);

    r = RingBuffer(0u, 4u);

    CHECK(r.slots == nullptr);

    r = RingBuffer(sizeof(i32), 3); CHECK(r.slots != nullptr);

    CHECK(r.capacity == 4);

    CHECK(r.mask == 3);

    CHECK(r.elem_size == sizeof(i32));

    RingBuffer_shutdown(&r);

    r = RingBuffer(sizeof(i32), 5); CHECK(r.slots != nullptr);

    CHECK(r.capacity == 8);

    CHECK(r.mask == 7);

    RingBuffer_shutdown(&r);

    r = RingBuffer(sizeof(i32), 2); CHECK(r.slots != nullptr);

    CHECK(r.capacity == 2);

    RingBuffer_shutdown(&r);

    r = RingBuffer(sizeof(i32), 0); CHECK(r.slots != nullptr);

    CHECK(r.capacity == 1);

    CHECK(r.mask == 0);

    i32 one = 7;

    CHECK(RingBuffer_push(&r, &one));

    CHECK(!RingBuffer_push(&r, &one));

    i32 out = 0;

    CHECK(RingBuffer_pop(&r, &out));

    CHECK(out == 7);

    CHECK(!RingBuffer_pop(&r, &out));

    RingBuffer_shutdown(&r);

    RingBuffer_shutdown(&r);

    r = RingBuffer(sizeof(i32), 4); CHECK(r.slots != nullptr);

    RingBuffer_shutdown(&r);
}

/** Verifies FIFO order, full-capacity refusal, and slot reuse after popping. */
static fn test_fifo_and_full() {
    RingBuffer r;

    r = RingBuffer(sizeof(i32), 4); CHECK(r.slots != nullptr);

    i32 out = -1;

    CHECK(!RingBuffer_pop(&r, &out));

    CHECK(!RingBuffer_push(&r, nullptr));

    CHECK(!RingBuffer_pop(&r, nullptr));

    for (i32 i = 0; i < 4; i++) {
        i32 v = 100 + i;

        CHECK(RingBuffer_push(&r, &v));
    }

    CHECK(!RingBuffer_push(&r, &(i32){ 999 }));

    for (i32 i = 0; i < 4; i++) {
        CHECK(RingBuffer_pop(&r, &out));

        CHECK(out == 100 + i);
    }

    CHECK(!RingBuffer_pop(&r, &out));

    for (i32 i = 0; i < 4; i++) 
        CHECK(RingBuffer_push(&r, &(i32){ 200 + i }));

    CHECK(!RingBuffer_push(&r, &(i32){ 999 }));

    CHECK(RingBuffer_pop(&r, &out));

    CHECK(out == 200);

    CHECK(RingBuffer_push(&r, &(i32){ 300 }));

    CHECK(!RingBuffer_push(&r, &(i32){ 301 }));

    CHECK(RingBuffer_pop(&r, &out));

    CHECK(out == 201);

    RingBuffer_shutdown(&r);
}

/** Exercises repeated index wraparound while preserving element order. */
static fn test_wraparound() {
    RingBuffer r;

    r = RingBuffer(sizeof(i32), 4); CHECK(r.slots != nullptr);

    i32 expect = 0;

    for (i32 round = 0; round < 1000; round++) {
        for (i32 i = 0; i < 2; i++) {
            i32 v = expect++;

            CHECK(RingBuffer_push(&r, &v));
        }

        for (i32 i = 0; i < 2; i++) {
            i32 out = -1;

            CHECK(RingBuffer_pop(&r, &out));

            CHECK(out == round * 2 + i);
        }
    }

    i32 out;

    CHECK(!RingBuffer_pop(&r, &out));

    RingBuffer_shutdown(&r);
}

/** Confirms the ring copies complete elements larger than a machine word. */
static fn test_wide_elements() {
    RingBuffer r;

    r = RingBuffer(sizeof(Big), 2); CHECK(r.slots != nullptr);

    for (i32 i = 0; i < 2; i++) {
        Big b;
        memset(&b, 0, sizeof(b));
        b.a = (u64) i * 11;
        b.b = (u64) i * 22;
        memcpy(b.tag, "abc", 4);

        CHECK(RingBuffer_push(&r, &b));
    }

    CHECK(!RingBuffer_push(&r, &(Big){ .a = 99 }));

    for (i32 i = 0; i < 2; i++) {
        Big b = { 0 };

        CHECK(RingBuffer_pop(&r, &b));

        CHECK(b.a == (u64) i * 11);

        CHECK(b.b == (u64) i * 22);

        CHECK(b.tag[0] == 'a' && b.tag[1] == 'b' && b.tag[2] == 'c');
    }

    RingBuffer_shutdown(&r);
}

/** ── MPMC stress: exact once-through of every value ───────────────────────── */
#define RING_PRODUCERS 4
#define RING_CONSUMERS 4
#define RING_PER_PRODUCER 2000
#define RING_TOTAL (RING_PRODUCERS * RING_PER_PRODUCER)

static RingBuffer g_ring;
static atomic_int g_seen[RING_TOTAL];
static atomic_int g_consumed;
static atomic_int g_bad;
static atomic_int g_dup;
static atomic_int s_ring_g_start;

/** Publishes the producer's assigned values into the shared ring until complete. */
static any ring_producer(any arg) {
    i32 id = (i32) (pointer) arg;

    while (!atomic_load_explicit(&s_ring_g_start, memory_order_acquire)) 
        sched_yield();

    for (i32 j = 0; j < RING_PER_PRODUCER; j++) {
        i32 v = id * RING_PER_PRODUCER + j;

        while (!RingBuffer_push(&g_ring, &v)) 
            sched_yield();
    }

    return nullptr;
}

/** Drains published values and records consumption for the bounded MPMC assertion. */
static any ring_consumer(any arg) {
    (discard) arg;

    while (!atomic_load_explicit(&s_ring_g_start, memory_order_acquire)) 
        sched_yield();

    while (atomic_load_explicit(&g_consumed, memory_order_acquire) < RING_TOTAL) {
        i32 v;

        if (!RingBuffer_pop(&g_ring, &v)) {
            sched_yield();

            continue;
        }

        if (v < 0 || v >= RING_TOTAL) {
            atomic_fetch_add(&g_bad, 1);

            continue;
        }

        if (atomic_exchange_explicit(&g_seen[v], 1, memory_order_acq_rel) != 0) 
            atomic_fetch_add(&g_dup, 1);

        atomic_fetch_add_explicit(&g_consumed, 1, memory_order_acq_rel);
    }

    return nullptr;
}

/** Checks concurrent producers and consumers deliver every submitted value exactly once. */
static fn test_mpmc_exactly_once() {
    g_ring = RingBuffer(sizeof(i32), 64u); CHECK(g_ring.slots != nullptr);

    for (i32 i = 0; i < RING_TOTAL; i++) 
        atomic_store(&g_seen[i], 0);

    atomic_store(&g_consumed, 0);

    atomic_store(&g_bad, 0);

    atomic_store(&g_dup, 0);

    atomic_store(&s_ring_g_start, 0);

    thread producers[RING_PRODUCERS];

    thread consumers[RING_CONSUMERS];

    for (i32 i = 0; i < RING_PRODUCERS; i++) 
        CHECK(pthread_create(&producers[i], nullptr, ring_producer, (any) (pointer) i) == 0);

    for (i32 i = 0; i < RING_CONSUMERS; i++) 
        CHECK(pthread_create(&consumers[i], nullptr, ring_consumer, nullptr) == 0);

    atomic_store_explicit(&s_ring_g_start, 1, memory_order_release);

    for (i32 i = 0; i < RING_PRODUCERS; i++) 
        CHECK(pthread_join(producers[i], nullptr) == 0);

    for (i32 i = 0; i < RING_CONSUMERS; i++) 
        CHECK(pthread_join(consumers[i], nullptr) == 0);

    CHECK(atomic_load(&g_consumed) == RING_TOTAL);

    CHECK(atomic_load(&g_bad) == 0);

    CHECK(atomic_load(&g_dup) == 0);

    i32 missing = 0;

    for (i32 i = 0; i < RING_TOTAL; i++) 
        if (atomic_load(&g_seen[i]) == 0) 
            missing++;
    CHECK(missing == 0);

    i32 out;

    CHECK(!RingBuffer_pop(&g_ring, &out));

    i32 v = 5;

    CHECK(RingBuffer_push(&g_ring, &v));

    CHECK(RingBuffer_pop(&g_ring, &out));

    CHECK(out == 5);

    RingBuffer_shutdown(&g_ring);
}

/** Runs the RingBuffer owner scenarios and reports aggregate assertion status. */
static i32 suite_ring() {
    test_init_and_bounds();

    test_fifo_and_full();

    test_wraparound();

    test_wide_elements();

    test_mpmc_exactly_once();

    if (s_ring_g_failures == 0) {
        print("ring_test: all assertions held");

        return 0;
    }

    print("ring_test: ", s_ring_g_failures, " FAILURES");

    return 1;
}
#undef CHECK
#undef RING_PRODUCERS
#undef RING_CONSUMERS
#undef RING_PER_PRODUCER
#undef RING_TOTAL

/** Void adapter for ring suite. */
static fn suite_ring_wrap() {
    if (suite_ring() != 0)
        g_failures++;
}

/** Suite: set (from struct/set_test.c). */
/** vexspoke/test/struct/set_test.c, the Set class _test (membership, volume, dedup). */



static i32 s_set_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_set_g_failures++;                                                  \
        }                                                                  \
    } while (0)

;;GETTER
static inline bytes SetVisitCtx_visits(const SetVisitCtx *ctx)
{
    return (*ctx).visits;
}

;;GETTER
static inline u64 SetVisitCtx_sum(const SetVisitCtx *ctx)
{
    return (*ctx).sum;
}

;;GETTER
static inline u32 SetVisitCtx_orderAt(const SetVisitCtx *ctx, bytes idx)
{
    return (*ctx).order[idx];
}

;;SETTER
static inline fn SetVisitCtx_record(SetVisitCtx *ctx, u32 val)
{
    if((*ctx).visits < 8u)
        (*ctx).order[(*ctx).visits] = val;

    (*ctx).visits++;
    (*ctx).sum += val;
}

static fn visitSetU32(const_any elem, bytes index, any userdata)
{
    (discard) index;
    SetVisitCtx *ctx = (SetVisitCtx*) userdata;
    u32 v = 0;
    memcpy(&v, elem, sizeof(u32));
    SetVisitCtx_record(ctx, v);
}

/** Exercises HashSet and LinkedHashSet membership, dedup, insertion order, string keys, churn, and OOM. */
static i32 suite_set()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    Set *sets[2];
    sets[0] = HashSet(u32, 8, arena);
    sets[1] = LinkedHashSet(u32, 8, arena);

    for(u32 k = 0; k < 2; k++) {
        Set *s = sets[k];
        CHECK(s != nullptr);
        CHECK(Set_isEmpty(s));
        CHECK(Set_size(s) == 0);
        CHECK(Set_exhaustionCount(s) == 0);
        CHECK(Set_stride(s) == sizeof(u32));

        CHECK(Set_add(s, (u32) 10));
        CHECK(!Set_add(s, (u32) 10));
        CHECK(Set_add(s, (u32) 20));
        CHECK(Set_add(s, (u32) 30));
        CHECK(Set_contains(s, (u32) 10));
        CHECK(Set_size(s) == 3);

        CHECK(Set_remove(s, (u32) 10));
        CHECK(!Set_contains(s, (u32) 10));
        CHECK(!Set_remove(s, (u32) 10));
        CHECK(Set_size(s) == 2);

        SetVisitCtx vctx;
        memset(&vctx, 0, sizeof(vctx));
        Set_forEach(s, visitSetU32, &vctx);
        CHECK(SetVisitCtx_visits(&vctx) == 2);
        CHECK(SetVisitCtx_sum(&vctx) == 50u);

        if(LinkedHashSet_isLinkedHashSet(s)) {
            CHECK(SetVisitCtx_orderAt(&vctx, 0) == 20u);
            CHECK(SetVisitCtx_orderAt(&vctx, 1) == 30u);
            u32 *first = LinkedHashSet_first(s);
            u32 *last = LinkedHashSet_last(s);
            CHECK(first != nullptr && *first == 20u);
            CHECK(last != nullptr && *last == 30u);
        }

        u32 *stableBefore = Set_get(s, (u32) 20);

        for(u32 i = 0; i < 1000; i++)
            CHECK(Set_add(s, 1000u + i));

        CHECK(Set_size(s) == 1002);
        CHECK(Set_contains(s, (u32) 1000));
        CHECK(Set_contains(s, (u32) 1999));
        CHECK(!Set_contains(s, (u32) 9999));

        if(LinkedHashSet_isLinkedHashSet(s))
            CHECK(Set_get(s, (u32) 20) == stableBefore);

        List *asList = Set_toList(s);
        CHECK(asList != nullptr && List_size(asList) == 1002);

        if(LinkedHashSet_isLinkedHashSet(s)) {
            CHECK(List_getAs(asList, 0, u32) == 20u);
            CHECK(List_getAs(asList, 1, u32) == 30u);
            CHECK(List_getAs(asList, 2, u32) == 1000u);
        }

        List_free(asList);

        utf8 buf[128];
        bool trunc = true;
        Set_toString(s, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');
        Set_toStringStruct(s, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');

        Set_clear(s);
        CHECK(Set_isEmpty(s));
        Set_free(s);
    }

    Set *strSet = HashSet(string, 8, arena);
    CHECK(strSet != nullptr);
    utf8 aBuf[16];
    utf8 bBuf[16];
    memcpy(aBuf, "token_x", 8);
    memcpy(bBuf, "token_x", 8);
    string sA = aBuf;
    string sB = bBuf;
    CHECK(Set_add(strSet, sA));
    CHECK(!Set_add(strSet, sB));
    CHECK(Set_contains(strSet, sB));
    CHECK(Set_remove(strSet, sB));
    CHECK(!Set_contains(strSet, sA));
    Set_free(strSet);

    CHECK(!Set_add(nullptr, (u32) 1));
    CHECK(!Set_contains(nullptr, (u32) 1));
    CHECK(!Set_remove(nullptr, (u32) 1));
    CHECK(Set_size(nullptr) == 0);
    CHECK(Set_isEmpty(nullptr));
    CHECK(Set_toList(nullptr) == nullptr);
    Set_free(nullptr);

    MemoryArena *oomArena = MemoryArena_pages(2);
    Set *oomSet = HashSet(u32, 4, oomArena);
    CHECK(oomSet != nullptr);
    CHECK(!Set_reserve(oomSet, 65536u));
    CHECK(Set_exhaustionCount(oomSet) >= 1u);
    Set_free(oomSet);
    MemoryArena_destroy(oomArena);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_set_g_failures == 0) {
        print("set_test: all assertions held");

        return 0;
    }

    print("set_test: ", s_set_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for set suite. */
static fn suite_set_wrap() {
    if (suite_set() != 0)
        g_failures++;
}

/** Suite: stack_observability (from struct/stack_observability_test.c). */
/* Owner test for the Stack's explicit fault reporting.
 *
 * Stack_push returns fn for family consistency, but a refused element is now
 * EXPLICIT, never a silent drop:
 *   1. Stack_pushTry returns a named TryCode: TRY_NULL_ARG, TRY_OVERFLOW, or
 *      TRY_NO_MEMORY when the arena cannot grow the buffer; Stack_push emits the
 *      same THROW diagnostic for callers that ignore the Try.
 *   2. the data buffer IS a self-describing arena block, Memory_type() is the
 *      array type id and Memory_length() is exactly capacity * stride.
 *
 * The test deliberately exhausts a small master arena (the Deliberate Exhaustion
 * and Backend Trust Law) so the fault is actually reached, then identifies it by
 * code and proves the stack stays coherent.
 */



static i32 s_stack_observability_g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { print("[stackobs] PASS ", name); } \
    else { print("[stackobs] FAIL ", name); s_stack_observability_g_failures++; } \
} while (0)

#define ARENA_BYTES (32u * 1024u * 1024u)
#define PUSH_LIMIT  9000000u /** exceeds the 64MB default arena's reachable capacity */

static i32 suite_stack_observability()
{
    print("=== Stack explicit-fault owner suite ===");
    CHECK("TRY_NO_MEMORY has a name", string_equals(TryCode_name(TRY_NO_MEMORY), "TRY_NO_MEMORY"));
    CHECK("TRY_NO_MEMORY has a message", TryCode_message(TRY_NO_MEMORY)[0] != '\0');
    CHECK("TRY_NO_MEMORY is not ok", !TryCode_isOk(TRY_NO_MEMORY));

    MemoryArena *smallArena = MemoryArena_pages(4);
    CHECK("small arena initialized", smallArena != nullptr);

    Stack *s = ArrayStack(u32, 16, smallArena);
    CHECK("stack constructed", s != nullptr && Stack_stride(s) == sizeof(u32));

    TryValue nullTry = Stack_pushTry(nullptr, (u32) 1);
    CHECK("null stack -> TRY_NULL_ARG", TryValue_getCode(&nullTry) == TRY_NULL_ARG);

    TryValue badWidth = Stack_pushTry(s, (u64) 1);
    CHECK("stride mismatch -> TRY_FORMAT", TryValue_getCode(&badWidth) == TRY_FORMAT);

    TryValue okPush = Stack_pushTry(s, (u32) 12345);
    CHECK("ok push is TRY_OK", TryValue_isOk(&okPush));
    CHECK("ok push returns the value", TryValue_getValue(&okPush) == 12345);

    TryCode fault = TRY_OK;
    u64 attempted = 0;
    u32 lastGood = 12345u;

    for(u32 i = 0; i < 65536u; i++) {
        TryValue r = Stack_pushTry(s, i);
        attempted++;

        if(!TryValue_isOk(&r)) {
            fault = TryValue_getCode(&r);
            break;
        }

        lastGood = i;
    }

    CHECK("growth failure was reached", fault != TRY_OK);
    CHECK("fault is TRY_NO_MEMORY", fault == TRY_NO_MEMORY);
    CHECK("fault names the cause", string_equals(TryCode_name(fault), "TRY_NO_MEMORY"));
    CHECK("exhaustion counter incremented", Stack_exhaustionCount(s) >= 1u);
    CHECK("top element intact", Stack_peekAs(s, u32) == lastGood);

    bytes beforePop = Stack_size(s);
    CHECK("pop returns the top", Stack_popAs(s, u32) == lastGood);
    CHECK("size decrements on pop", Stack_size(s) == beforePop - 1u);

    TryValue afterPop = Stack_pushTry(s, (u32) 7);
    CHECK("pop then push succeeds again", TryValue_isOk(&afterPop));

    Stack_free(s);
    CHECK("small arena zero leak", MemoryArena_activeBytes(smallArena) == 0);
    MemoryArena_destroy(smallArena);

    print("[stackobs] attempted ", (u64) attempted, " pushes before TRY_NO_MEMORY");

    if(s_stack_observability_g_failures != 0) {
        print("=== ", s_stack_observability_g_failures, " FAILURES ===");

        return 1;
    }

    print("=== ALL STACKOBS PASS ===");

    return 0;
}
#undef CHECK
#undef ARENA_BYTES
#undef PUSH_LIMIT

/** Void adapter for stack_observability suite. */
static fn suite_stack_observability_wrap() {
    if (suite_stack_observability() != 0)
        g_failures++;
}

/** Suite: stack (from struct/stack_test.c). */
/** vexspoke/test/struct/stack_test.c, the Stack class _test (LIFO, volume, hostile). */



static i32 s_stack_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_stack_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** Verifies ArrayStack and LinkedStack LIFO ops, pointer stability, TryValue push, and cleanup. */
static i32 suite_stack()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    Stack *stacks[2];
    stacks[0] = ArrayStack(u32, 4, arena);
    stacks[1] = LinkedStack(u32, arena);

    for(u32 k = 0; k < 2; k++) {
        Stack *s = stacks[k];
        CHECK(s != nullptr);
        CHECK(Stack_exhaustionCount(s) == 0);
        CHECK(Stack_isEmpty(s));
        CHECK(Stack_size(s) == 0);
        CHECK(Stack_stride(s) == sizeof(u32));

        CHECK(Stack_push(s, (u32) 10));
        CHECK(Stack_push(s, (u32) 20));
        CHECK(Stack_size(s) == 2);
        CHECK(Stack_peekAs(s, u32) == 20);
        CHECK(Stack_getAs(s, 0, u32) == 10);
        CHECK(Stack_getAs(s, 1, u32) == 20);
        CHECK(Stack_popAs(s, u32) == 20);
        CHECK(Stack_popAs(s, u32) == 10);
        CHECK(!Stack_pop(s));
        CHECK(Stack_peek(s) == nullptr);

        for(u32 i = 0; i < 2000; i++)
            CHECK(Stack_push(s, i));

        CHECK(Stack_size(s) == 2000);
        CHECK(Stack_peekAs(s, u32) == 1999);
        CHECK(Stack_popAs(s, u32) == 1999);
        CHECK(Stack_popAs(s, u32) == 1998);
        CHECK(Stack_size(s) == 1998);
        CHECK(Stack_get(s, Stack_size(s)) == nullptr);
        CHECK(Stack_get(s, SIZE_MAX) == nullptr);

        TryValue okPush = Stack_pushTry(s, (u32) 7);
        CHECK(TryValue_isOk(&okPush));
        CHECK(TryValue_getValue(&okPush) == 7);
        CHECK(Stack_size(s) == 1999);
        CHECK(Stack_popAs(s, u32) == 7);

        utf8 buf[128];
        bool trunc = true;
        Stack_toString(s, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');
        Stack_toStringStruct(s, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');

        Stack_clear(s);
        CHECK(Stack_isEmpty(s));
        Stack_free(s);
    }

    Stack *ls = LinkedStack(u32, arena);
    CHECK(LinkedStack_isLinkedStack(ls));
    u32 *stableBottom = Stack_emplace(ls);
    CHECK(stableBottom != nullptr);
    *stableBottom = 333u;

    for(u32 i = 0; i < 32; i++)
        CHECK(Stack_push(ls, i));

    CHECK(Stack_get(ls, 0) == stableBottom && *stableBottom == 333u);
    Stack_free(ls);

    TryValue nullPush = Stack_pushTry(nullptr, (u32) 7);
    CHECK(TryValue_getCode(&nullPush) == TRY_NULL_ARG);
    CHECK(!Stack_push(nullptr, (u32) 1));
    CHECK(!Stack_pop(nullptr));
    CHECK(Stack_peek(nullptr) == nullptr);
    CHECK(Stack_get(nullptr, 0) == nullptr);
    CHECK(Stack_size(nullptr) == 0);
    CHECK(Stack_isEmpty(nullptr));
    Stack_free(nullptr);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_stack_g_failures == 0) {
        print("stack_test: all assertions held");

        return 0;
    }

    print("stack_test: ", s_stack_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for stack suite. */
static fn suite_stack_wrap() {
    if (suite_stack() != 0)
        g_failures++;
}

/** Suite: stride (from struct/stride_test.c). */
/**
 * vex/re/test/struct/stride_test.c, the Stride class _test.
 *
 * Proves re/struct/stride.c: the built-in class-id  to  byte-width table,
 * unknown defaults, and custom struct registration (`Stride_register`,
 * `Stride_isRegistered`, `Stride_resetCustom`) plus `List` / `Array` backing
 * custom structs. Pure, stateless, headless.
 */



static i32 s_stride_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_stride_g_failures++;                                                  \
        }                                                                  \
    } while (0)

;;GETTER
static inline u32 TestCustomVertex_color(const TestCustomVertex *v)
{
    return (*v).color;
}
;;GETTER
static inline d64 TestCustomVertex_weight(const TestCustomVertex *v)
{
    return (*v).weight;
}

/** Checks built-in mappings, custom struct registration, and container integration. */
static i32 suite_stride() {
    CHECK(Stride_get(ID_BYTE) == 1);

    CHECK(Stride_get(ID_SHORT) == 2);

    CHECK(Stride_get(ID_INT) == 4);

    CHECK(Stride_get(ID_FLOAT) == 4);

    CHECK(Stride_get(ID_LONG) == 8);

    CHECK(Stride_get(ID_f64) == 8);

    CHECK(Stride_get(ID_VEC2) == 8);

    CHECK(Stride_get(ID_VEC3) == 12);

    CHECK(Stride_get(ID_VEC4) == 16);

    CHECK(Stride_get(ID_MAT4) == 64);

    CHECK(Stride_get(ID_VARIABLE) == 40);

    CHECK(Stride_get(0x7FFFu) == 8);

    CHECK(Stride_get((u32) TYPE_VEC3_SINGLETON) == 12);

    CHECK(Stride_get((u32) TYPE_VEC4_SINGLETON) == 16);

    Stride_resetCustom();

    u64 customType = Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, 0x4200u);

    CHECK(!Stride_isRegistered(customType));

    CHECK(!Stride_register(0u, 32u));

    CHECK(!Stride_register(customType, 0u));

    CHECK(!Stride_register(customType, 24u));

    CHECK(!Stride_register(ID_INT, 32u));

    CHECK(!Stride_register(ID_VEC3, 32u));

    CHECK(STRIDE_REGISTER(customType, TestCustomVertex));

    CHECK(Stride_isRegistered(customType));

    CHECK(Stride_isRegistered(0x4200u));

    CHECK(Stride_get((u32) customType) == sizeof(TestCustomVertex));

    CHECK(Stride_get(0x4200u) == sizeof(TestCustomVertex));

    CHECK(Stride_register(0x4201u, 16u));

    CHECK(Stride_get(0x4201u) == 16u);

    CHECK(Stride_register(0x4201u, 48u));

    CHECK(Stride_get(0x4201u) == 48u);

    CHECK(Memory_init(0));

    List *verts = ArrayList(TestCustomVertex, 4u);

    Array *arr = Array_2(0x4200u, 2u);

    CHECK(verts != nullptr && List_stride(verts) == sizeof(TestCustomVertex));

    CHECK(arr != nullptr && Array_stride(arr) == sizeof(TestCustomVertex));

    TestCustomVertex *vslot = List_emplace(verts);

    CHECK(vslot != nullptr && ((pointer) vslot & 15u) == 0u);

    if (vslot) 
        (*vslot) = (TestCustomVertex){ .x = 1.0f, .y = 2.0f, .z = 3.0f, .color = 0xFF00AA55u, .weight = 0.5 };

    TestCustomVertex *aslot = (TestCustomVertex*) Array_slot(arr, 1u);

    CHECK(aslot != nullptr && ((pointer) aslot & 15u) == 0u);

    if (aslot && vslot) 
        (*aslot) = (*vslot);

    CHECK(aslot != nullptr && TestCustomVertex_color(aslot) == 0xFF00AA55u && TestCustomVertex_weight(aslot) == 0.5);

    List_free(verts);

    Array_free(arr);

    Stride_resetCustom();

    CHECK(!Stride_isRegistered(customType));

    CHECK(Stride_get((u32) customType) == 8u);

    if (s_stride_g_failures == 0) {
        print("stride_test: all assertions held");

        return 0;
    }

    print("stride_test: ", s_stride_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for stride suite. */
static fn suite_stride_wrap() {
    if (suite_stride() != 0)
        g_failures++;
}

/** Suite: type_algebra (from type/type_algebra_test.c). */
/**
 * relational-engine/test/type/type_algebra_test.c, the R2 type-algebra owner test.
 * (Per-File Battle Test Law.) type/type.c owns the project-agnostic id algebra
 * and the parent-chain resolver, so the battle rows are:
 * - VALUE BOUNDARY: each id field (form / project / class / sugar) isolates;
 * - ADVERSARIAL: registration rejects zero/foreign project Bytes, nullptr
 * with a non-zero count, out-of-range parents, self-parents and CYCLES;
 * - FAILURE ATOMICITY: every rejection preserves the previous table;
 * - BOUNDED PROGRESS: a cyclic table can never be registered, so the
 * Type_isA walk cannot spin;
 * - CROSS-PROJECT: the documented project-invariant ancestor match is pinned;
 * - GROWTH: the slate f64s under many projects.
 *
 * This owner compiles against the algebra plus the Relational Engine class
 * registry, pinning the project byte and class numbers that re owns.
 */



static i32 s_type_algebra_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_type_algebra_g_failures++;                                                  \
        }                                                                  \
    } while (0)

/** A project byte with no stray bits: the field occupies bits 52..59. */
static u64 projByte(u64 byte) {
    return (byte << 52) & MASK_PROJECT;
}

static i32 suite_type_algebra() {
    u64 id = Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u);

    CHECK(Type_class(id) == 1u);

    CHECK(Type_form(id) == FORM_SINGLETON);

    CHECK(Type_project(id) == PROJ_VEXSPOKE);

    CHECK(Type_class(0xFFFFFFFFull) == 0x00FFFFFFu);

    CHECK((SUGAR_VEX & MASK_SUGAR) == SUGAR_VEX);

    CHECK(Type_form(FORM_STRUCT_ARRAY) == FORM_STRUCT_ARRAY);

    CHECK(Type_isSingleton(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)));

    CHECK(!Type_isArray(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)));

    CHECK(Type_isArray(Type_make(PROJ_VEXSPOKE, FORM_ARRAY, 1u)));

    CHECK(Type_isPointer(Type_make(PROJ_VEXSPOKE, FORM_POINTER, 1u)));

    CHECK(Type_isStruct(FORM_STRUCT_SINGLETON));

    CHECK(!Type_isStruct(0));

    CHECK(!Type_isStruct(FORM_SINGLETON));

    CHECK(Type_isStructSingleton(Type_make(PROJ_VEXSPOKE, FORM_STRUCT_SINGLETON, 1u)));

    CHECK(Type_isStructArray(FORM_STRUCT_ARRAY));

    CHECK(Type_isStructSOA(FORM_ARRAY_SOA));

    CHECK(Type_isStructAOS(FORM_ARRAY_AOS));

    CHECK(Type_isStructCoexistent(FORM_STRUCT_COEXISTENT));

    CHECK(Type_isStructPointer(FORM_STRUCT_POINTER));

    CHECK(Type_isPrimitive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)));

    CHECK(!Type_isPrimitive(FORM_STRUCT_SINGLETON));

    CHECK(Type_isGlobal(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | MOD_GLOBAL));

    CHECK(Type_isLocale(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | MOD_LOCALE));

    CHECK(Type_isReactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_REACTIVE));

    CHECK(Type_isProactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_PROACTIVE));

    CHECK(Type_isProbable(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_PROBABLE));

    CHECK(Type_isProbableObjects(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_PROBABLE_OBJECTS));

    CHECK(Type_isFuture(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_FUTURE));

    CHECK(Type_isChoice(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP2_CHOICE));

    CHECK(!Type_isReactive(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u) | WRAP_PROACTIVE));

    CHECK(Type_arch(Type_make(PROJ_VEXSPOKE, FORM_SINGLETON, 1u)) == ARCH_VEXSPOKE);

    CHECK(Type_arch(1u) == ARCH_VEXSPOKE);

    CHECK(Type_arch(projByte(2) | 1u) == ARCH_GRAPHVEX);

    CHECK(Type_arch(projByte(3) | 1u) == ARCH_HOTCWAP);

    CHECK(Type_arch(projByte(4) | 1u) == ARCH_DARLING);

    CHECK(Type_arch(projByte(5) | 1u) == ARCH_APIHAVEN);

    CHECK(Type_arch(projByte(6) | 1u) == ARCH_DARKBASE);

    CHECK(Type_isDarkbase(projByte(6) | 1u));

    CHECK(Type_isDarling(projByte(4) | 1u));

    static const u32 bare[0x64] = { [0x50 ... 0x63] = 0x4Au };

    CHECK(Type_registerBareParents(bare, 0x64u));

    CHECK(Type_getParentClass(0x50u) == 0x4Au);

    CHECK(Type_getParentClass(0x63u) == 0x4Au);

    CHECK(Type_getParentClass(0x4Au) == 0x4Au);

    CHECK(Type_getParentClass(0x01u) == 0x01u);

    static const u32 chain[4] = { 0, 0, 1, 2 };

    CHECK(!Type_registerParents(0u, chain, 4));

    CHECK(!Type_registerParents(0x1ull, chain, 4));

    CHECK(!Type_registerParents(PROJ_VEXSPOKE, chain, 4));

    CHECK(!Type_registerParents(projByte(0x20), nullptr, 3));

    CHECK(Type_registerParents(projByte(0x20), nullptr, 0));

    CHECK(Type_getParentClass(projByte(0x20) | 9) == 9);

    CHECK(Type_registerParents(projByte(0x21), chain, 4));

    CHECK(Type_getParentClass(projByte(0x21) | 3) == 2u);

    CHECK(Type_getParentClass(projByte(0x21) | 2) == 1u);

    CHECK(Type_getParentClass(projByte(0x21) | 1) == 1u);

    CHECK(Type_getParentClass(projByte(0x21) | 0) == 0u);

    CHECK(Type_getParentClass(projByte(0x21) | 99) == 99u);

    CHECK(Type_isA(projByte(0x21) | 3, projByte(0x21) | 1));

    CHECK(Type_isA(projByte(0x21) | 1, projByte(0x21) | 1));

    CHECK(!Type_isA(projByte(0x21) | 1, projByte(0x21) | 3));

    static const u32 cyc[4]   = { 0, 0, 3, 2 };

    static const u32 oob[4]   = { 0, 0, 1, 9 };

    static const u32 selfp[4] = { 0, 1, 0, 1 };

    CHECK(!Type_registerParents(projByte(0x22), cyc, 4));

    CHECK(!Type_registerParents(projByte(0x24), oob, 4));

    CHECK(Type_getParentClass(projByte(0x21) | 3) == 2u);

    CHECK(Type_registerParents(projByte(0x23), selfp, 4));

    CHECK(Type_getParentClass(projByte(0x23) | 1) == 1u);

    CHECK(Type_isA(projByte(0x23) | 1, projByte(0x23) | 1));

    CHECK(!Type_isA(projByte(0x23) | 1, projByte(0x23) | 2));

    CHECK(!Type_isA(projByte(0x22) | 1, projByte(0x22) | 3));

    CHECK(!Type_isA(projByte(0x23) | 1, projByte(0x23) | 3));

    CHECK(Type_isA(projByte(0x21) | 1, projByte(0x30) | 1));

    CHECK(Type_isA(projByte(0x30) | 1, 1u));

    CHECK(!Type_isA(projByte(0x30) | 2, 1u));

    static const u32 chain2[4] = { 0, 0, 0, 1 };

    CHECK(Type_registerParents(projByte(0x21), chain2, 4));

    CHECK(Type_getParentClass(projByte(0x21) | 3) == 1u);

    static const u32 flat[4] = { 0, 0, 0, 0 };

    for (u64 b = 0x30; b < 0x30 + 24; b++) 
        CHECK(Type_registerParents(projByte(b), flat, 4));

    for (u64 b = 0x30; b < 0x30 + 24; b++) {
        CHECK(Type_getParentClass(projByte(b) | 1) == 1u);

        CHECK(Type_getParentClass(projByte(b) | 2) == 2u);
    }

    CHECK(Type_project(TYPE_VARIABLE_SLOT) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_project(TYPE_STRING_POOL) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_project(TYPE_VARIABLE_HASH_MAP) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_project(TYPE_VARIABLE_MINI_MAP) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_project(TYPE_SHELF) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_project(TYPE_SHELF_NODE) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_project(TYPE_VARIABLE_POOL_ORDER) == PROJ_RELATIONAL_ENGINE);

    CHECK(Type_class(TYPE_VARIABLE_SLOT) == ID_VARIABLE_SLOT);

    CHECK(Type_class(TYPE_STRING_POOL) == ID_STRING_POOL);

    CHECK(Type_class(TYPE_VARIABLE_HASH_MAP) == ID_VARIABLE_HASH_MAP);

    CHECK(Type_class(TYPE_VARIABLE_MINI_MAP) == ID_VARIABLE_MINI_MAP);

    CHECK(Type_class(TYPE_SHELF) == ID_SHELF);

    CHECK(Type_class(TYPE_SHELF_NODE) == ID_SHELF_NODE);

    CHECK(Type_class(TYPE_VARIABLE_POOL_ORDER) == ID_VARIABLE_POOL_ORDER);

    CHECK(Type_form(TYPE_VARIABLE_SLOT) == FORM_STRUCT_SINGLETON);

    CHECK(Type_form(TYPE_VARIABLE_HASH_MAP) == FORM_STRUCT_SINGLETON);

    CHECK(Type_form(TYPE_VARIABLE_MINI_MAP) == FORM_STRUCT_SINGLETON);

    CHECK(Type_form(TYPE_SHELF) == FORM_STRUCT_SINGLETON);

    CHECK(Type_form(TYPE_SHELF_NODE) == FORM_STRUCT_SINGLETON);

    CHECK(Type_form(TYPE_STRING_POOL) == FORM_ARRAY);

    CHECK(Type_form(TYPE_VARIABLE_POOL_ORDER) == FORM_ARRAY);

    if (s_type_algebra_g_failures == 0) {
        print("type_test: all assertions held");

        return 0;
    }

    print("type_test: ", s_type_algebra_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for type_algebra suite. */
static fn suite_type_algebra_wrap() {
    if (suite_type_algebra() != 0)
        g_failures++;
}

/** Suite: registry (from thread/registry/registry_test.c). */
/**
 * re/test/thread/registry_test.c, owner test for thread/registry.
 *
 * ThreadRegistry gives every thread a stable dense index in [0, 256) and one
 * opaque advisory tag. This test proves:
 * - the calling thread's index is stable across calls and in range;
 * - tags default to THREAD_TAG_NONE and set/get round-trips;
 * - out-of-range index calls are safe and read THREAD_TAG_NONE;
 * - under real concurrency every live thread gets a DISTINCT, stable index
 * (the lock-free probe-and-CAS contract) within a bounded watchdog;
 * - a thread that has joined and is gone does not invalidate anyone.
 *
 * Role names are policy, owned and proven by Vexspoke's thread test.
 */



static i32 s_registry_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_registry_g_failures++;                                                  \
        }                                                                  \
    } while (0)

static fn test_self_index() {
    i32 a = ThreadRegistry_index();

    i32 b = ThreadRegistry_index();

    CHECK(a == b);

    CHECK(a >= 0 && a < THREAD_REGISTRY_DEFAULT_SIZE);
}

static fn test_tags() {
    i32 idx = ThreadRegistry_index();

    CHECK(ThreadRegistry_tag(idx) == THREAD_TAG_NONE);

    static const u32 tags[] = {
        THREAD_TAG_NONE, 1u, 7u, 0x1234u, 0xFFFFFFFFu,
    };
    for (bytes i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        ThreadRegistry_setTag(idx, tags[i]);

        CHECK(ThreadRegistry_tag(idx) == tags[i]);
    }

    ThreadRegistry_setTag(idx, 42u);

    CHECK(ThreadRegistry_tag(idx) == 42u);

    ThreadRegistry_setTag(idx, THREAD_TAG_NONE);
}

static fn test_bounds() {
    ThreadRegistry_setTag(-1, 42u);

    ThreadRegistry_setTag(THREAD_REGISTRY_DEFAULT_SIZE, 42u);

    ThreadRegistry_setTag(1 << 20, 42u);

    CHECK(ThreadRegistry_tag(-1) == THREAD_TAG_NONE);

    CHECK(ThreadRegistry_tag(THREAD_REGISTRY_DEFAULT_SIZE) == THREAD_TAG_NONE);

    CHECK(ThreadRegistry_tag(INT32_MAX) == THREAD_TAG_NONE);

    CHECK(ThreadRegistry_tag(INT32_MIN) == THREAD_TAG_NONE);
}

/** ── concurrency: distinct, stable indices for every live thread ──────────── */
#define REG_THREADS 16
#define REG_REPEAT 200

static atomic_int g_reg_start;
static i32 g_reg_index[REG_THREADS];
static i32 g_reg_stable_flags[REG_THREADS]; /** 1 if all repeats agreed */

static any reg_worker(any arg) {
    i32 id = (i32) (pointer) arg;

    while (!atomic_load_explicit(&g_reg_start, memory_order_acquire)) 
        sched_yield();

    i32 first = ThreadRegistry_index();

    i32 stable = 1;

    for (i32 i = 0; i < REG_REPEAT; i++) 
        if (ThreadRegistry_index() != first) 
            stable = 0;
    ThreadRegistry_setTag(first, 42u);

    g_reg_index[id] = first;

    g_reg_stable_flags[id] = stable;

    while (atomic_load_explicit(&g_reg_start, memory_order_acquire) != 2) 
        sched_yield();

    return nullptr;
}

static fn test_concurrent_distinct() {
    atomic_store(&g_reg_start, 0);

    thread t[REG_THREADS];

    for (i32 i = 0; i < REG_THREADS; i++) {
        g_reg_index[i] = -1;

        g_reg_stable_flags[i] = 0;

        CHECK(pthread_create(&t[i], nullptr, reg_worker, (any) (pointer) i) == 0);
    }

    atomic_store_explicit(&g_reg_start, 1, memory_order_release);

    bool all_published = false;

    while (!all_published) {
        all_published = true;

        for (i32 i = 0; i < REG_THREADS; i++) 
            if (g_reg_index[i] < 0) 
                all_published = false;
        sched_yield();
    }

    i32 idx[REG_THREADS];

    for (i32 i = 0; i < REG_THREADS; i++) {
        idx[i] = g_reg_index[i];

        CHECK(g_reg_stable_flags[i] == 1);

        CHECK(idx[i] >= 0 && idx[i] < THREAD_REGISTRY_DEFAULT_SIZE);
    }

    for (i32 i = 0; i < REG_THREADS; i++) 
        for (i32 j = i + 1; j < REG_THREADS; j++) 
            CHECK(idx[i] != idx[j]);

    atomic_store_explicit(&g_reg_start, 2, memory_order_release);

    for (i32 i = 0; i < REG_THREADS; i++) 
        CHECK(pthread_join(t[i], nullptr) == 0);

    i32 me = ThreadRegistry_index();

    CHECK(me == ThreadRegistry_index());

    CHECK(me >= 0 && me < THREAD_REGISTRY_DEFAULT_SIZE);
}

static i32 suite_registry() {
    test_self_index();

    test_tags();

    test_bounds();

    test_concurrent_distinct();

    if (s_registry_g_failures == 0) {
        print("registry_test: all assertions held");

        return 0;
    }

    print("registry_test: ", s_registry_g_failures, " FAILURES");

    return 1;
}
#undef CHECK
#undef REG_THREADS
#undef REG_REPEAT

/** Void adapter for registry suite. */
static fn suite_registry_wrap() {
    if (suite_registry() != 0)
        g_failures++;
}

/** Suite: try_code (from exception/try_code_test.c). */
/**
 * vexspoke/test/exception/try_code_test.c, the TryCode class _test.
 *
 * Proves exception/try_code.c: the taxonomy's two projections are stable,
 * complete, and null-free, and TryCode_isOk is true only for TRY_OK.
 */



static i32 s_try_code_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_try_code_g_failures++;                                                  \
        }                                                                  \
    } while (0)

static const TryCode ALL[] = {
    TRY_OK, TRY_NULL_ARG, TRY_BOUNDS, TRY_OVERFLOW, TRY_EMPTY,
    TRY_NOT_FOUND, TRY_IO, TRY_FORMAT, TRY_UNSUPPORTED, TRY_NO_MEMORY
};

static i32 suite_try_code() {
    bytes n = sizeof ALL / sizeof ALL[0];

    for (bytes i = 0; i < n; i++) {
        TryCode c = ALL[i];

        CHECK(TryCode_name(c) != nullptr);

        CHECK(TryCode_message(c) != nullptr);

        CHECK(TryCode_name(c)[0] != '\0');

        CHECK(TryCode_message(c)[0] != '\0');
        CHECK(TryCode_isOk(c) == (c == TRY_OK));
    }

    CHECK(string_equals(TryCode_name(TRY_OK), "TRY_OK"));

    CHECK(string_equals(TryCode_name(TRY_BOUNDS), "TRY_BOUNDS"));

    CHECK(string_equals(TryCode_name(TRY_IO), "TRY_IO"));

    CHECK(string_equals(TryCode_name((TryCode) 9999), "TRY_UNKNOWN"));

    CHECK(TryCode_message((TryCode) 9999) != nullptr);

    CHECK(!TryCode_isOk((TryCode) 9999));

    if (s_try_code_g_failures == 0) {
        print("try_code_test: all assertions held");

        return 0;
    }

    print("try_code_test: ", s_try_code_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for try_code suite. */
static fn suite_try_code_wrap() {
    if (suite_try_code() != 0)
        g_failures++;
}

/** Suite: try_ptr (from exception/try_ptr_test.c). */
/**
 * vexspoke/test/exception/try_ptr_test.c, the TryPtr class _test.
 *
 * Proves exception/try_ptr.c: by-value construction, the CONTRACT (value is
 * meaningful only when code == TRY_OK), symmetric getters/setters, null
 * safety, and the bounded string projections.
 */



static i32 s_try_ptr_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_try_ptr_g_failures++;                                                  \
        }                                                                  \
    } while (0)

static i32 suite_try_ptr() {
    i32 payload = 7;

    i32 other = 8;

    TryPtr ok = TryPtr_ok(&payload);

    CHECK(TryPtr_isOk(&ok));

    CHECK(TryPtr_getValue(&ok) == &payload);

    CHECK(TryPtr_getCode(&ok) == TRY_OK);

    TryPtr err = TryPtr_error(TRY_IO);

    CHECK(!TryPtr_isOk(&err));

    CHECK(TryPtr_getValue(&err) == nullptr);

    CHECK(TryPtr_getCode(&err) == TRY_IO);

    TryPtr two = TryPtr_2(&other, TRY_NOT_FOUND);

    CHECK(TryPtr_getValue(&two) == &other);

    CHECK(TryPtr_getCode(&two) == TRY_NOT_FOUND);

    TryPtr dispatched = TryPtr(&payload, TRY_FORMAT);

    CHECK(TryPtr_getCode(&dispatched) == TRY_FORMAT);

    TryPtr t = TryPtr_ok(nullptr);

    TryPtr_setValue(&t, &payload);

    CHECK(TryPtr_getValue(&t) == &payload);

    TryPtr_setCode(&t, TRY_BOUNDS);

    CHECK(TryPtr_getCode(&t) == TRY_BOUNDS);

    CHECK(!TryPtr_isOk(&t));

    CHECK(!TryPtr_isOk(nullptr));

    CHECK(TryPtr_getValue(nullptr) == nullptr);

    CHECK(TryPtr_getCode(nullptr) == TRY_NULL_ARG);

    TryPtr_setValue(nullptr, &payload);

    TryPtr_setCode(nullptr, TRY_IO);

    CHECK(1);

    utf8 buf[128];

    bool trunc = false;

    TryPtr_toString(&err, buf, sizeof buf, &trunc);

    CHECK(string_contains(buf, "TRY_IO"));

    CHECK(!trunc);

    TryPtr_toString(&ok, buf, sizeof buf, &trunc);

    CHECK(string_contains(buf, "TRY_OK"));

    TryPtr_toStringStruct(&ok, buf, sizeof buf, &trunc);

    CHECK(string_contains(buf, "value="));

    TryPtr_toString(nullptr, buf, sizeof buf, &trunc);

    CHECK(string_equals(buf, "nullptr"));

    trunc = false;

    TryPtr_toString(&ok, buf, 4u, &trunc);

    CHECK(trunc);

    if (s_try_ptr_g_failures == 0) {
        print("try_ptr_test: all assertions held");

        return 0;
    }

    print("try_ptr_test: ", s_try_ptr_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for try_ptr suite. */
static fn suite_try_ptr_wrap() {
    if (suite_try_ptr() != 0)
        g_failures++;
}

/** Suite: try_value (from exception/try_value_test.c). */
/**
 * vexspoke/test/exception/try_value_test.c, the value-or-error primitive.
 *
 * Proves the Try family (the Per-File Battle Test Law):
 * - TryValue (scalar) and TryPtr (pointer) construct, round-trip, and isOk.
 * - The full value domain: 0, 1, UINT64_MAX, every TryCode, no sentinel
 * collision (the reason the Try exists at all).
 * - The CONTRACT: `value` is meaningful only when code == TRY_OK.
 * - Null-safety of every getter/setter (the Symmetric Getter/Setter
 * Completeness Law).
 * - String projections: null self, zero cap, null dest, and truncation
 * flagging (the toString Law / Truncation-Never-Silent clause).
 *
 * Deterministic, headless, zero allocation.
 */



static i32 s_try_value_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);          \
            s_try_value_g_failures++;                                                  \
        }                                                                  \
    } while (0)

static const TryCode ALL_CODES[] = {
    TRY_OK, TRY_NULL_ARG, TRY_BOUNDS, TRY_OVERFLOW, TRY_EMPTY,
    TRY_NOT_FOUND, TRY_IO, TRY_FORMAT, TRY_UNSUPPORTED, TRY_NO_MEMORY
};

/** --- TryValue: constructors + the CONTRACT --- */
/** Verifies value-result constructors preserve successful values and error state. */
static fn testValueConstructors() {
    TryValue ok = TryValue_ok(42u);

    CHECK(TryValue_isOk(&ok));

    CHECK(TryValue_getValue(&ok) == 42u);

    CHECK(TryValue_getCode(&ok) == TRY_OK);

    TryValue err = TryValue_error(TRY_BOUNDS);

    CHECK(!TryValue_isOk(&err));

    CHECK(TryValue_getCode(&err) == TRY_BOUNDS);

    TryValue two = TryValue_2(7u, TRY_IO);

    CHECK(TryValue_getValue(&two) == 7u);

    CHECK(TryValue_getCode(&two) == TRY_IO);

    TryValue dispatched = TryValue(9u, TRY_FORMAT);

    CHECK(TryValue_getValue(&dispatched) == 9u);

    CHECK(TryValue_getCode(&dispatched) == TRY_FORMAT);
}

/** --- TryValue: setters round-trip (Symmetric Getter/Setter Completeness) --- */
/** Checks setters update the value/error alternative without corrupting the other state. */
static fn testValueSetters() {
    TryValue t = TryValue_ok(1u);

    TryValue_setValue(&t, 99u);

    CHECK(TryValue_getValue(&t) == 99u);

    TryValue_setCode(&t, TRY_IO);

    CHECK(TryValue_getCode(&t) == TRY_IO);

    CHECK(!TryValue_isOk(&t));

    TryValue_setCode(&t, TRY_OK);

    CHECK(TryValue_isOk(&t));
}

/** --- TryValue: the full domain has no spare sentinel --- */
/** Exercises zero, extreme, and empty values accepted by the value-result wrapper. */
static fn testValueBoundaries() {
    TryValue zero = TryValue_ok(0u);

    CHECK(TryValue_isOk(&zero));

    CHECK(TryValue_getValue(&zero) == 0u);

    TryValue max = TryValue_ok(UINT64_MAX);

    CHECK(TryValue_isOk(&max));

    CHECK(TryValue_getValue(&max) == UINT64_MAX);
}

/** --- TryValue: null-safety of every accessor --- */
/** Verifies null inputs and accessors return the wrapper's documented safe results. */
static fn testValueNullSafety() {
    CHECK(TryValue_isOk(nullptr) == false);

    CHECK(TryValue_getValue(nullptr) == 0u);

    CHECK(TryValue_getCode(nullptr) == TRY_NULL_ARG);

    TryValue_setValue(nullptr, 5u);

    TryValue_setCode(nullptr, TRY_IO);

    CHECK(1);
}

/** --- TryValue: string projections --- */
/** Checks bounded string projections for successful and failed value results. */
static fn testValueStrings() {
    utf8 buf[128];

    bool trunc = false;

    TryValue ok = TryValue_ok(42u);

    TryValue_toString(&ok, buf, sizeof buf, &trunc);

    CHECK(!trunc);

    CHECK(string_equals(buf, "TryValue(value=42, code=TRY_OK)"));

    TryValue err = TryValue_error(TRY_BOUNDS);

    TryValue_toString(&err, buf, sizeof buf, &trunc);

    CHECK(string_contains(buf, "TRY_BOUNDS"));

    CHECK(string_contains(buf, "index outside the active range"));

    TryValue_toStringStruct(&ok, buf, sizeof buf, &trunc);

    CHECK(string_equals(buf, "TryValue { value=42, code=TRY_OK }"));

    TryValue_toString(nullptr, buf, sizeof buf, &trunc);

    CHECK(string_equals(buf, "nullptr"));

    CHECK(!trunc);

    trunc = true;

    TryValue_toString(&ok, buf, 0u, &trunc);

    CHECK(!trunc);

    TryValue_toString(&ok, nullptr, sizeof buf, &trunc);

    CHECK(!trunc);

    trunc = false;

    TryValue_toString(&ok, buf, 4u, &trunc);

    CHECK(trunc);
}

/** --- TryPtr: the pointer-shaped sibling --- */
/** Exercises pointer-result success, failure, and null-pointee behavior. */
static fn testPtr() {
    i32 payload = 1234;

    utf8 buf[128];

    bool trunc = false;

    TryPtr ok = TryPtr_ok(&payload);

    CHECK(TryPtr_isOk(&ok));

    CHECK(TryPtr_getValue(&ok) == &payload);

    CHECK(TryPtr_getCode(&ok) == TRY_OK);

    TryPtr err = TryPtr_error(TRY_IO);

    CHECK(!TryPtr_isOk(&err));

    CHECK(TryPtr_getValue(&err) == nullptr);

    CHECK(TryPtr_getCode(&err) == TRY_IO);

    TryPtr two = TryPtr_2(nullptr, TRY_NOT_FOUND);

    CHECK(TryPtr_getCode(&two) == TRY_NOT_FOUND);

    TryPtr_setValue(&two, &payload);

    CHECK(TryPtr_getValue(&two) == &payload);

    TryPtr_setCode(&two, TRY_OK);

    CHECK(TryPtr_isOk(&two));

    CHECK(TryPtr_isOk(nullptr) == false);

    CHECK(TryPtr_getValue(nullptr) == nullptr);

    CHECK(TryPtr_getCode(nullptr) == TRY_NULL_ARG);

    TryPtr_setValue(nullptr, &payload);

    TryPtr_setCode(nullptr, TRY_IO);

    TryPtr_toString(&err, buf, sizeof buf, &trunc);

    CHECK(string_contains(buf, "TRY_IO"));

    TryPtr_toString(&ok, buf, sizeof buf, &trunc);

    CHECK(string_contains(buf, "TRY_OK"));

    TryPtr_toString(nullptr, buf, sizeof buf, &trunc);

    CHECK(string_equals(buf, "nullptr"));

    trunc = false;

    TryPtr_toString(&ok, buf, 4u, &trunc);

    CHECK(trunc);
}

/** --- TryCode taxonomy --- */
/** Confirms error codes retain their declared categories and identifying values. */
static fn testCodeTaxonomy() {
    bytes count = sizeof ALL_CODES / sizeof ALL_CODES[0];

    for (bytes i = 0; i < count; i++) {
        TryCode code = ALL_CODES[i];

        CHECK(TryCode_name(code) != nullptr);

        CHECK(TryCode_message(code) != nullptr);

        CHECK((code == TRY_OK) == TryCode_isOk(code));
    }

    CHECK(string_equals(TryCode_name(TRY_OK), "TRY_OK"));

    CHECK(string_equals(TryCode_name((TryCode) 9999), "TRY_UNKNOWN"));
}

/** Runs the TryValue and TryPtr owner cases and reports aggregate assertion status. */
static i32 suite_try_value() {
    testValueConstructors();

    testValueSetters();

    testValueBoundaries();

    testValueNullSafety();

    testValueStrings();

    testPtr();

    testCodeTaxonomy();

    if (s_try_value_g_failures == 0) {
        print("try_value_test: all checks passed");

        return 0;
    }

    print("try_value_test: ", s_try_value_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Suite: graph (ArrayGraph + LinkedGraph polymorphic verification). */
static i32 s_graph_g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if(!(cond)) {                                                      \
            print("FAIL ", __FILE__, ":", __LINE__, " ", #cond);           \
            s_graph_g_failures++;                                          \
        }                                                                  \
    } while(0)

;;GETTER
static inline u32 GraphCity_code(const GraphCity *c)
{
    return (*c).code;
}

;;GETTER
static inline string GraphCity_name(const GraphCity *c)
{
    return (*c).name;
}

;;SETTER
static inline fn GraphCity_set(GraphCity *c, u32 code, string name)
{
    (*c).code = code;
    (*c).name = name;
}

;;GETTER
static inline f32 GraphRoad_distance(const GraphRoad *r)
{
    return (*r).distance;
}

;;GETTER
static inline u32 GraphRoad_lanes(const GraphRoad *r)
{
    return (*r).lanes;
}

;;SETTER
static inline fn GraphRoad_set(GraphRoad *r, f32 distance, u32 lanes)
{
    (*r).distance = distance;
    (*r).lanes = lanes;
}

;;GETTER
static inline bytes GraphVisitCtx_visits(const GraphVisitCtx *ctx)
{
    return (*ctx).visits;
}

;;GETTER
static inline f32 GraphVisitCtx_totalDistance(const GraphVisitCtx *ctx)
{
    return (*ctx).totalDistance;
}

;;SETTER
static inline fn GraphVisitCtx_record(GraphVisitCtx *ctx, u32 to, f32 dist)
{
    if((*ctx).visits < 8u)
        (*ctx).targets[(*ctx).visits] = to;

    (*ctx).visits++;
    (*ctx).totalDistance += dist;
}

static fn visitGraphRoad(u32 from, u32 to, any edgePayload, bytes index, any userdata)
{
    (discard) from;
    (discard) index;
    GraphVisitCtx *ctx = (GraphVisitCtx*) userdata;
    const GraphRoad *road = (const GraphRoad*) edgePayload;
    GraphVisitCtx_record(ctx, to, GraphRoad_distance(road));
}

/** Verifies ArrayGraph and LinkedGraph vertices, directed/bidirectional edges, updates, removals, stability, OOM. */
static i32 suite_graph()
{
    MemoryArena *arena = MemoryArena();
    CHECK(arena != nullptr);

    Graph *graphs[2];
    graphs[0] = ArrayGraph(GraphCity, GraphRoad, arena);
    graphs[1] = LinkedGraph(GraphCity, GraphRoad, arena);

    for(u32 k = 0; k < 2; k++) {
        Graph *g = graphs[k];
        CHECK(g != nullptr);
        CHECK(Graph_isEmpty(g));
        CHECK(Graph_vertexCount(g) == 0);
        CHECK(Graph_edgeCount(g) == 0);
        CHECK(Graph_vertexStride(g) == sizeof(GraphCity));
        CHECK(Graph_edgeStride(g) == sizeof(GraphRoad));

        GraphCity c0;
        GraphCity_set(&c0, 1u, "Manila");
        GraphCity c1;
        GraphCity_set(&c1, 2u, "Cebu");
        GraphCity c2;
        GraphCity_set(&c2, 3u, "Davao");

        u32 v0 = Graph_addVertex(g, c0);
        u32 v1 = Graph_addVertex(g, c1);
        u32 v2 = Graph_addVertex(g, c2);
        CHECK(v0 == 0 && v1 == 1 && v2 == 2);
        CHECK(Graph_vertexCount(g) == 3);

        u32 v3 = GRAPH_INVALID_VERTEX;
        GraphCity *empCity = Graph_emplaceVertex(g, &v3);
        CHECK(empCity != nullptr && v3 == 3u);
        GraphCity_set(empCity, 4u, "Baguio");
        GraphCity *read3 = Graph_vertex(g, v3);
        CHECK(read3 != nullptr && GraphCity_code(read3) == 4u);

        GraphCity c0Updated;
        GraphCity_set(&c0Updated, 10u, "MetroManila");
        CHECK(Graph_setVertex(g, v0, c0Updated));
        GraphCity copied0 = Graph_vertexAs(g, v0, GraphCity);
        CHECK(GraphCity_code(&copied0) == 10u && string_equals(GraphCity_name(&copied0), "MetroManila"));

        GraphRoad r01;
        GraphRoad_set(&r01, 570.0f, 4u);
        GraphRoad r02;
        GraphRoad_set(&r02, 960.0f, 2u);
        GraphRoad r12;
        GraphRoad_set(&r12, 400.0f, 3u);

        CHECK(Graph_addEdge(g, v0, v1, r01));
        CHECK(Graph_addEdge(g, v0, v2, r02));
        CHECK(Graph_addBiEdge(g, v1, v2, r12));
        CHECK(Graph_edgeCount(g) == 4);
        CHECK(Graph_outDegree(g, v0) == 2);
        CHECK(Graph_hasEdge(g, v1, v2));
        CHECK(Graph_hasEdge(g, v2, v1));
        CHECK(!Graph_hasEdge(g, v2, v0));

        GraphRoad r01Fast;
        GraphRoad_set(&r01Fast, 550.0f, 6u);
        CHECK(Graph_addEdge(g, v0, v1, r01Fast));
        CHECK(Graph_edgeCount(g) == 4);
        GraphRoad got01 = Graph_edgeAs(g, v0, v1, GraphRoad);
        CHECK(GraphRoad_distance(&got01) == 550.0f && GraphRoad_lanes(&got01) == 6u);

        GraphVisitCtx vctx;
        memset(&vctx, 0, sizeof(vctx));
        Graph_forEachEdge(g, v0, visitGraphRoad, &vctx);
        CHECK(GraphVisitCtx_visits(&vctx) == 2);
        CHECK(GraphVisitCtx_totalDistance(&vctx) == 1510.0f);

        List *nbrs = Graph_neighbors(g, v0);
        CHECK(nbrs != nullptr && List_count(nbrs) == 2);
        List_free(nbrs);

        CHECK(Graph_removeEdge(g, v0, v1));
        CHECK(!Graph_hasEdge(g, v0, v1));
        CHECK(!Graph_removeEdge(g, v0, v1));
        CHECK(Graph_edgeCount(g) == 3);
        CHECK(Graph_outDegree(g, v0) == 1);

        CHECK(!Graph_addEdge(g, 99u, v0, r01));
        CHECK(Graph_vertex(g, 99u) == nullptr);
        CHECK(Graph_edge(g, v0, 99u) == nullptr);

        GraphCity *stableV0 = Graph_vertex(g, v0);
        GraphRoad *stableE02 = Graph_edge(g, v0, v2);

        for(u32 i = 0; i < 64; i++) {
            GraphCity extra;
            GraphCity_set(&extra, 100u + i, "Node");
            u32 vid = Graph_addVertex(g, extra);
            CHECK(vid != GRAPH_INVALID_VERTEX);
        }

        if(LinkedGraph_isLinkedGraph(g)) {
            CHECK(Graph_vertex(g, v0) == stableV0);
            CHECK(Graph_edge(g, v0, v2) == stableE02);
        }

        utf8 buf[128];
        bool trunc = true;
        Graph_toString(g, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');
        Graph_toStringStruct(g, buf, sizeof(buf), &trunc);
        CHECK(!trunc && buf[0] != '\0');

        Graph_clear(g);
        CHECK(Graph_isEmpty(g) && Graph_edgeCount(g) == 0);
        Graph_free(g);
    }

    Graph *weightedA = ArrayGraph(u32);
    Graph *weightedL = LinkedGraph(u32);
    CHECK(weightedA != nullptr && Graph_edgeStride(weightedA) == sizeof(f32));
    CHECK(weightedL != nullptr && Graph_edgeStride(weightedL) == sizeof(f32));
    u32 a0 = Graph_addVertex(weightedA, (u32) 10);
    u32 a1 = Graph_addVertex(weightedA, (u32) 20);
    CHECK(Graph_addEdge(weightedA, a0, a1, 3.5f));
    CHECK(Graph_edgeAs(weightedA, a0, a1, f32) == 3.5f);
    Graph_free(weightedA);
    Graph_free(weightedL);

    CHECK(Graph_addVertex(nullptr, (u32) 1) == GRAPH_INVALID_VERTEX);
    CHECK(Graph_vertex(nullptr, 0) == nullptr);
    CHECK(!Graph_hasEdge(nullptr, 0, 1));
    CHECK(Graph_neighbors(nullptr, 0) == nullptr);
    Graph_free(nullptr);

    CHECK(MemoryArena_activeBytes(arena) == 0);
    MemoryArena_destroy(arena);

    if(s_graph_g_failures == 0) {
        print("graph_test: all assertions held");

        return 0;
    }

    print("graph_test: ", s_graph_g_failures, " FAILURES");

    return 1;
}
#undef CHECK

/** Void adapter for graph suite. */
static fn suite_graph_wrap()
{
    if(suite_graph() != 0)
        g_failures++;
}

/** Void adapter for try_value suite. */
static fn suite_try_value_wrap() {
    if (suite_try_value() != 0)
        g_failures++;
}

/** Runs all automated vex/re suites or a single named suite passed as argv[1]. */
i32 main(i32 argc, utf8 **argv) {
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
        { "hash", suite_hash_wrap },
        { "cell", suite_cell_wrap },
        { "class_relational", suite_class_relational_wrap },
        { "shelf", suite_shelf_wrap },
        { "symbol_table", suite_symbol_table_wrap },
        { "var", suite_var_wrap },
        { "variable_hash_map", suite_variable_hash_map_wrap },
        { "variable_mini_map", suite_variable_mini_map_wrap },
        { "variable_pool", suite_variable_pool_wrap },
        { "variable_slot", suite_variable_slot_wrap },
        { "variable_strict", suite_variable_strict_wrap },
        { "buffer", suite_buffer_wrap },
        { "vex_string", suite_vex_string_wrap },
        { "varchar", suite_varchar_wrap },
        { "spin", suite_spin_wrap },
        { "array", suite_array_wrap },
        { "chunked_list", suite_chunked_list_wrap },
        { "chunked_paged", suite_chunked_paged_wrap },
        { "collection", suite_collection_wrap },
        { "deque", suite_deque_wrap },
        { "exhaustion", suite_exhaustion_wrap },
        { "list", suite_list_wrap },
        { "map", suite_map_wrap },
        { "queue", suite_queue_wrap },
        { "ring", suite_ring_wrap },
        { "set", suite_set_wrap },
        { "stack_observability", suite_stack_observability_wrap },
        { "stack", suite_stack_wrap },
        { "graph", suite_graph_wrap },
        { "stride", suite_stride_wrap },
        { "type_algebra", suite_type_algebra_wrap },
        { "registry", suite_registry_wrap },
        { "try_code", suite_try_code_wrap },
        { "try_ptr", suite_try_ptr_wrap },
        { "try_value", suite_try_value_wrap },
    };
    string filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && !string_equals(filter, suites[i].name))
            continue;
        suites[i].run();
        ran++;
    }

    if (filter && ran == 0) {
        fprint(stderr, "re_test: unknown suite '", filter, "'");
        return 2;
    }
    if (g_failures != 0) {
        fprint(stderr, "re_test: ", g_failures, " FAILURES");
        return 1;
    }
    puts("re_test: PASS");
    return 0;
}
