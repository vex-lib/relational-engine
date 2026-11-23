#include "vex.h"
#include "io/log/log.h"
/** Production R2 I/O owner: Relational Engine; existing Log ABI preserved. */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nio/mem.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Log
 * ============================================================================
 * MPSC event ring + writer daemon, ported from io/Log.java. Producers append
 * fixed-size records into a lock-free head/tail slot arena; a single writer
 * thread drains slots to a FileWriter sink in big-endian format behind a
 * magic header. The ring is carved once at init (zero steady-state
 * allocation), and the active flag makes append a no-op when the sink is
 * down. Counters (appended/dropped/written) are atomic where producers touch
 * them; the writer thread owns written/flush_point exclusively.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Log (io/log.c)
 * ============================================================================
 * the Log class, ported from io/Log.java.
 *
 * STRUCT FIELDS (Mirroring io/log.h):
 * ----------------------------------------------------------------------------
 *   Log {
 *     u8 *arena; // head/tail + slot arena, carved once
 *     bytes slot_count; // live entry count
 *     bytes slot_mask; // capacity mask for indexing
 *     bytes slot_size; // slot/ring sizing state
 *     FileWriter writer; // file sink writer
 *     thread thread; // background thread handle
 *     bool thread_started; // background thread handle
 *     atomic_bool running; // run-state flag
 *     bool enabled; // built with a real sink
 *     bool active; // runtime gate; false makes append a no-op
 *     _Atomic u64 appended; // appended record counter
 *     _Atomic u64 dropped; // dropped record counter
 *     u64 written; // writer thread only
 *     u64 flush_point; // writer thread only
 *     utf8 path[FILE_PATH_MAX]; // backing file path
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Log_init(log, path, slot_count)
 *
 * Core Functions:
 *   - Log_shutdown(log)
 *   - Log_path(log)
 *   - Log_appended(log)
 *   - Log_dropped(log)
 *   - Log_written(log)
 *   - Log_append(log, kind, v0, v1, v2, v3, v4)
 *   - Log_appendKind(log, kind)
 *
 * Setters:
 *   - Log_setActive(log, on)
 *
 * Getters:
 *   - Log_isEnabled(log)
 *   - Log_isActive(log)
 * ============================================================================
 */


/** log.c, Log port (Legacy: io/Log.java). MPSC event ring + writer daemon. */

;;GETTER
static inline FileWriter *Log_writerMut(Log *log)
{
    return &(*log).writer;
}
;;GETTER
static inline u8 *Log_arena(const Log *log)
{
    return (*log).arena;
}
;;SETTER
static inline fn Log_setArena(Log *log, u8 *arena)
{
    (*log).arena = arena;
}
;;GETTER
static inline bytes Log_slotCount(const Log *log)
{
    return (*log).slot_count;
}
;;SETTER
static inline fn Log_setSlotCount(Log *log, bytes count)
{
    (*log).slot_count = count;
}
;;GETTER
static inline bytes Log_slotMask(const Log *log)
{
    return (*log).slot_mask;
}
;;SETTER
static inline fn Log_setSlotMask(Log *log, bytes mask)
{
    (*log).slot_mask = mask;
}
;;GETTER
static inline bytes Log_slotSize(const Log *log)
{
    return (*log).slot_size;
}
;;SETTER
static inline fn Log_setSlotSize(Log *log, bytes size)
{
    (*log).slot_size = size;
}
;;GETTER
static inline utf8 *Log_pathMut(Log *log)
{
    return (*log).path;
}
;;GETTER
static inline bool Log_enabled(const Log *log)
{
    return (*log).enabled;
}
;;SETTER
static inline fn Log_setEnabled(Log *log, bool enabled)
{
    (*log).enabled = enabled;
}
;;GETTER
static inline bool Log_active(const Log *log)
{
    return (*log).active;
}
;;SETTER
static inline fn Log_setActiveField(Log *log, bool active)
{
    (*log).active = active;
}
;;GETTER
static inline bool Log_loadRunning(const Log *log)
{
    return atomic_load_explicit(&(*log).running, memory_order_acquire);
}
;;GETTER
static inline fn Log_initRunning(Log *log, bool val)
{
    atomic_init(&(*log).running, val);
}
;;GETTER
static inline fn Log_storeRunning(Log *log, bool val)
{
    atomic_store(&(*log).running, val);
}
;;GETTER
static inline fn Log_initAppended(Log *log, u64 val)
{
    atomic_init(&(*log).appended, val);
}
;;GETTER
static inline fn Log_fetchAddAppended(Log *log)
{
    atomic_fetch_add_explicit(&(*log).appended, 1, memory_order_relaxed);
}
;;GETTER
static inline fn Log_initDropped(Log *log, u64 val)
{
    atomic_init(&(*log).dropped, val);
}
;;GETTER
static inline fn Log_fetchAddDropped(Log *log)
{
    atomic_fetch_add_explicit(&(*log).dropped, 1, memory_order_relaxed);
}
;;SETTER
static inline fn Log_setWritten(Log *log, u64 val)
{
    (*log).written = val;
}
;;GETTER
static inline u64 Log_flushPoint(const Log *log)
{
    return (*log).flush_point;
}
;;SETTER
static inline fn Log_setFlushPoint(Log *log, u64 val)
{
    (*log).flush_point = val;
}
#if defined(VEX_PLATFORM_WINDOWS)
;;SETTER
static inline fn Log_setThread(Log *log, thread worker)
{
    (*log).worker = worker;
}
#else
;;GETTER
static inline thread *Log_threadMut(Log *log)
{
    return &(*log).worker;
}
#endif
;;GETTER
static inline thread Log_thread(const Log *log)
{
    return (*log).worker;
}
;;GETTER
static inline bool Log_threadStarted(const Log *log)
{
    return (*log).thread_started;
}
;;SETTER
static inline fn Log_setThreadStarted(Log *log, bool val)
{
    (*log).thread_started = val;
}

static const u8 LOG_MAGIC[7] = { 0x41, 0x4E, 0x54, 0x49, 0x4C, 0x4F, 0x47 };

/** Return monotonic time as nanoseconds for event timestamps. */
static i64 monotonic_nanos() {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (i64) ts.tv_sec * 1000000000LL + (i64) ts.tv_nsec;
}

/** Encode a 32-bit value in big-endian byte order at the requested offset. */
static fn put_be32(u8 *b, bytes off, u32 v) {
    b[off] = (u8)(v >> 24);

    b[off + 1] = (u8)(v >> 16);

    b[off + 2] = (u8)(v >> 8);

    b[off + 3] = (u8)v;
}

/** Encode a 64-bit value in big-endian byte order at the requested offset. */
static fn put_be64(u8 *b, bytes off, i64 v) {
    for (i32 i = 0; i < 8; i++) 
        b[off + (bytes)i] = (u8)((u64)v >> (56 - 8 * i));
}

/** Write the binary log magic, version, and fixed record-size header. */
static bool write_header(Log *log) {
    u8 header[LOG_HEADER_BYTES];

    memcpy(header, LOG_MAGIC, 7);

    header[7] = 0x01;

    put_be32(header, 8, LOG_RECORD_BYTES);

    FileWriter_write(Log_writerMut(log), header, LOG_HEADER_BYTES);

    return true;
}

/** Atomically reserve the next producer slot, returning -1 when the ring is full. */
static i64 claim_slot(Log *log) {
    u8 *arena = Log_arena(log);
    _Atomic i64 *head = (_Atomic i64*) (arena + LOG_HEAD_OFF);

    _Atomic i64 *tail = (_Atomic i64*) (arena + LOG_TAIL_OFF);

    i64 h;

    i64 t;
    i64 cap = (i64) Log_slotCount(log);

    do {
        h = atomic_load_explicit(head, memory_order_acquire);

        t = atomic_load_explicit(tail, memory_order_acquire);

        if (h - t >= cap) 
            return -1;
    } while (!atomic_compare_exchange_strong_explicit(head, &h, h + 1,
                                                      memory_order_acq_rel,
                                                      memory_order_acquire));
    return h;
}

/** Convert one native ring slot to the on-disk big-endian record representation. */
static fn write_slot(Log *log, const u8 *slot) {
    u8 rec[LOG_RECORD_BYTES];

    u32 kind;

    i64 ts;

    memcpy(&kind, slot, 4);

    memcpy(&ts, slot + 8, 8);

    put_be32(rec, 0, kind);

    put_be64(rec, 4, ts);

    for (i32 i = 0; i < 5; i++) {
        i64 v;

        memcpy(&v, slot + 16 + (bytes)i * 8, 8);

        put_be64(rec, 12 + (bytes)i * 8, v);
    }

    FileWriter_write(Log_writerMut(log), rec, LOG_RECORD_BYTES);
}

/** Drain consecutive published records from the ring to the file writer. */
static u64 drain_and_write(Log *log) {
    u8 *arena = Log_arena(log);
    _Atomic i64 *head = (_Atomic i64*) (arena + LOG_HEAD_OFF);

    _Atomic i64 *tail = (_Atomic i64*) (arena + LOG_TAIL_OFF);

    i64 h = atomic_load_explicit(head, memory_order_acquire);

    i64 t = atomic_load_explicit(tail, memory_order_acquire);

    u64 wrote = 0;
    bytes mask = Log_slotMask(log);
    bytes slotSize = Log_slotSize(log);

    while (t < h) {
        bytes slot_idx = (bytes)t & mask;

        u8 *slot = arena + LOG_SLOT_BASE + slot_idx * slotSize;

        _Atomic i32 *valid = (_Atomic i32*) (slot + 4);

        if (atomic_load_explicit(valid, memory_order_acquire) != 1) 
            break;

        write_slot(log, slot);

        atomic_store_explicit(valid, 0, memory_order_release);

        t++;

        atomic_store_explicit(tail, t, memory_order_release);

        wrote++;
    }

    if (wrote > 0) 
        Log_setWritten(log, Log_written(log) + wrote);

    return wrote;
}

/** Writer thread loop: drain records, periodically flush, then drain once on stop. */
static any writer_main(any arg) {
    Log *log = (Log*) arg;

    u64 idle = 0;

    while (Log_loadRunning(log)) {
        u64 wrote = drain_and_write(log);

        if (wrote > 0) {
            idle = 0;

            u64 written = Log_written(log);
            if (written - Log_flushPoint(log) >= LOG_FLUSH_EVERY) {
                Log_setFlushPoint(log, written);

                FileWriter_flush(Log_writerMut(log));
            }

            continue;
        }

        idle++;

        if (idle >= LOG_IDLE_FLUSH_TICKS) {
            idle = 0;

            Log_setFlushPoint(log, Log_written(log));

            FileWriter_flush(Log_writerMut(log));
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = LOG_IDLE_POLL_NANOS };

        nanosleep(&ts, nullptr);
    }

    drain_and_write(log);

    FileWriter_flush(Log_writerMut(log));

    return nullptr;
}

/** Initialize the bounded record ring, file sink, counters, and writer thread. */
bool Log_init(Log *log, string path, bytes slot_count) {
    if (!log || !path) 
        return false;

    memset(log, 0, sizeof(*log));

    bytes sc = 1;

    while (sc < slot_count) 
        sc <<= 1;

    Log_setSlotCount(log, sc);

    Log_setSlotMask(log, sc - 1);

    Log_setSlotSize(log, LOG_SLOT_SIZE);

    utf8 *lpath = Log_pathMut(log);
    strncpy(lpath, path, FILE_PATH_MAX - 1);

    lpath[FILE_PATH_MAX - 1] = '\0';

    if (!FileWriter_open(Log_writerMut(log), path)) 
        return false;

    write_header(log);

    bytes arenaBytes = LOG_SLOT_BASE + sc * LOG_SLOT_SIZE;

    u8 *arena = (u8*) Memory_alloc(TYPE_LOG_SINGLETON, Re_arena(), arenaBytes);
    Log_setArena(log, arena);

    if (!arena) {
        FileWriter_close(Log_writerMut(log));

        return false;
    }

    memset(arena, 0, arenaBytes);

    Log_setEnabled(log, true);

    Log_setActiveField(log, true);

    Log_initRunning(log, true);

    Log_initAppended(log, 0);

    Log_initDropped(log, 0);

    Log_setWritten(log, 0);

    Log_setFlushPoint(log, 0);

    i32 threadRc = 0;
#if defined(VEX_PLATFORM_WINDOWS)
    pthread_t winTid = 0;
    threadRc = pthread_create(&winTid, nullptr, writer_main, log);
    if(threadRc == 0)
        Log_setThread(log, (thread) (bytes) winTid);
#else
    threadRc = pthread_create(Log_threadMut(log), nullptr, writer_main, log);
#endif
    if(threadRc != 0) {
        Log_storeRunning(log, false);

        Memory_free(arena);

        Log_setArena(log, nullptr);

        FileWriter_close(Log_writerMut(log));

        Log_setEnabled(log, false);

        return false;
    }

    Log_setThreadStarted(log, true);

    return true;
}

;;DECONSTRUCT
/**
 * Stops and joins the writer thread, drains remaining records, and releases
 * the ring arena and file sink.
 * @param log log instance to shut down
 */
fn Log_shutdown(Log *log) {
    if (!log) 
        return;

    Log_storeRunning(log, false);

    if (Log_threadStarted(log)) {
#if defined(VEX_PLATFORM_WINDOWS)
        pthread_join((pthread_t) (bytes) Log_thread(log), nullptr);
#else
        pthread_join(Log_thread(log), nullptr);
#endif

        Log_setThreadStarted(log, false);
    }

    u8 *arena = Log_arena(log);
    if (arena) {
        drain_and_write(log);

        Memory_free(arena);

        Log_setArena(log, nullptr);
    }

    FileWriter_close(Log_writerMut(log));

    Log_setEnabled(log, false);
}

/** Return whether initialization established a usable log sink. */
bool Log_isEnabled(const Log *log) {
    return Log_enabled(log);
}

/** Return the runtime append gate state. */
bool Log_isActive(const Log *log) {
    return Log_active(log);
}

/** Change the append gate when the log sink is enabled. */
fn Log_setActive(Log *log, bool on) {
    if (Log_enabled(log)) 
        Log_setActiveField(log, on);
}

/** Return the path stored at initialization. */
string Log_path(const Log *log) {
    return (*log).path;
}

/** Return the atomic count of records successfully published by producers. */
u64 Log_appended(const Log *log) {
    return atomic_load_explicit(&(*log).appended, memory_order_relaxed);
}

/** Return the atomic count of records dropped because the ring was full. */
u64 Log_dropped(const Log *log) {
    return atomic_load_explicit(&(*log).dropped, memory_order_relaxed);
}

/** Return the writer-owned count of records written to the sink. */
u64 Log_written(const Log *log) {
    return (*log).written;
}

/** Publish a timestamped event and five values, or increment dropped when the ring has no slot. */
fn Log_append(Log *log, i32 kind, i64 v0, i64 v1, i64 v2,
                i64 v3, i64 v4) {
    if (!log || !Log_enabled(log) || !Log_active(log)) 
        return;
    u8 *arena = Log_arena(log);
    if (!arena)
        return;

    i64 index = claim_slot(log);

    if (index < 0) {
        Log_fetchAddDropped(log);

        return;
    }

    bytes slot_idx = (bytes)index & Log_slotMask(log);

    u8 *slot = arena + LOG_SLOT_BASE + slot_idx * Log_slotSize(log);

    u32 k = (u32)kind;

    memcpy(slot, &k, 4);

    i64 ts = monotonic_nanos();

    memcpy(slot + 8, &ts, 8);

    memcpy(slot + 16, &v0, 8);

    memcpy(slot + 24, &v1, 8);

    memcpy(slot + 32, &v2, 8);

    memcpy(slot + 40, &v3, 8);

    memcpy(slot + 48, &v4, 8);

    _Atomic i32 *valid = (_Atomic i32*) (slot + 4);

    atomic_store_explicit(valid, 1, memory_order_release);

    Log_fetchAddAppended(log);
}

/** Append an event kind with all five value fields set to zero. */
fn Log_appendKind(Log *log, i32 kind) {
    Log_append(log, kind, 0, 0, 0, 0, 0);
}
