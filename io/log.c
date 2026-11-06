#include "io/log.h"
// Production R2 I/O owner: Relational Engine; existing Log ABI preserved.

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "io/vexhome.h"
#include "util/annotation.h"

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
 *     uint8_t *arena; // head/tail + slot arena, carved once
 *     size_t slot_count; // live entry count
 *     size_t slot_mask; // capacity mask for indexing
 *     size_t slot_size; // slot/ring sizing state
 *     FileWriter writer; // file sink writer
 *     pthread_t thread; // background thread handle
 *     bool thread_started; // background thread handle
 *     atomic_bool running; // run-state flag
 *     bool enabled; // built with a real sink
 *     bool active; // runtime gate; false makes append a no-op
 *     _Atomic uint64_t appended; // appended record counter
 *     _Atomic uint64_t dropped; // dropped record counter
 *     uint64_t written; // writer thread only
 *     uint64_t flush_point; // writer thread only
 *     char path[FILE_PATH_MAX]; // backing file path
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Log_init(log, path, slot_count)
 *
 * Core Functions:
 *   - Log_initDefault(log)
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


// log.c — Log port (Legacy: io/Log.java). MPSC event ring + writer daemon.

static const uint8_t LOG_MAGIC[7] = { 0x41, 0x4E, 0x54, 0x49, 0x4C, 0x4F, 0x47 };

/** Return monotonic time as nanoseconds for event timestamps. */
static int64_t monotonic_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000L + (int64_t)ts.tv_nsec;
}

/** Encode a 32-bit value in big-endian byte order at the requested offset. */
static void put_be32(uint8_t *b, size_t off, uint32_t v) {
    b[off] = (uint8_t)(v >> 24);
    b[off + 1] = (uint8_t)(v >> 16);
    b[off + 2] = (uint8_t)(v >> 8);
    b[off + 3] = (uint8_t)v;
}

/** Encode a 64-bit value in big-endian byte order at the requested offset. */
static void put_be64(uint8_t *b, size_t off, int64_t v) {
    for (int i = 0; i < 8; i++)
        b[off + (size_t)i] = (uint8_t)((uint64_t)v >> (56 - 8 * i));
}

/** Write the binary log magic, version, and fixed record-size header. */
static bool write_header(Log *log) {
    uint8_t header[LOG_HEADER_BYTES];
    memcpy(header, LOG_MAGIC, 7);
    header[7] = 0x01;
    put_be32(header, 8, LOG_RECORD_BYTES);
    FileWriter_write(&(*log).writer, header, LOG_HEADER_BYTES);
    return true;
}

/** Atomically reserve the next producer slot, returning -1 when the ring is full. */
static int64_t claim_slot(Log *log) {
    _Atomic int64_t *head = (_Atomic int64_t*) ((*log).arena + LOG_HEAD_OFF);
    _Atomic int64_t *tail = (_Atomic int64_t*) ((*log).arena + LOG_TAIL_OFF);
    int64_t h;
    int64_t t;
    do {
        h = atomic_load_explicit(head, memory_order_acquire);
        t = atomic_load_explicit(tail, memory_order_acquire);
        if (h - t >= (int64_t)(*log).slot_count)
            return -1;
    } while (!atomic_compare_exchange_strong_explicit(head, &h, h + 1,
                                                      memory_order_acq_rel,
                                                      memory_order_acquire));
    return h;
}

/** Convert one native ring slot to the on-disk big-endian record representation. */
static void write_slot(Log *log, const uint8_t *slot) {
    uint8_t rec[LOG_RECORD_BYTES];
    uint32_t kind;
    int64_t ts;
    memcpy(&kind, slot, 4);
    memcpy(&ts, slot + 8, 8);
    put_be32(rec, 0, kind);
    put_be64(rec, 4, ts);
    for (int i = 0; i < 5; i++) {
        int64_t v;
        memcpy(&v, slot + 16 + (size_t)i * 8, 8);
        put_be64(rec, 12 + (size_t)i * 8, v);
    }
    FileWriter_write(&(*log).writer, rec, LOG_RECORD_BYTES);
}

/** Drain consecutive published records from the ring to the file writer. */
static uint64_t drain_and_write(Log *log) {
    _Atomic int64_t *head = (_Atomic int64_t*) ((*log).arena + LOG_HEAD_OFF);
    _Atomic int64_t *tail = (_Atomic int64_t*) ((*log).arena + LOG_TAIL_OFF);
    int64_t h = atomic_load_explicit(head, memory_order_acquire);
    int64_t t = atomic_load_explicit(tail, memory_order_acquire);
    uint64_t wrote = 0;
    while (t < h) {
        size_t slot_idx = (size_t)t & (*log).slot_mask;
        uint8_t *slot = (*log).arena + LOG_SLOT_BASE + slot_idx * (*log).slot_size;
        _Atomic int32_t *valid = (_Atomic int32_t*) (slot + 4);
        if (atomic_load_explicit(valid, memory_order_acquire) != 1)
            break;
        write_slot(log, slot);
        atomic_store_explicit(valid, 0, memory_order_release);
        t++;
        atomic_store_explicit(tail, t, memory_order_release);
        wrote++;
    }
    if (wrote > 0)
        (*log).written += wrote;
    return wrote;
}

/** Writer thread loop: drain records, periodically flush, then drain once on stop. */
static void *writer_main(void *arg) {
    Log *log = (Log*) arg;
    uint64_t idle = 0;
    while (atomic_load_explicit(&(*log).running, memory_order_acquire)) {
        uint64_t wrote = drain_and_write(log);
        if (wrote > 0) {
            idle = 0;
            if ((*log).written - (*log).flush_point >= LOG_FLUSH_EVERY) {
                (*log).flush_point = (*log).written;
                FileWriter_flush(&(*log).writer);
            }
            continue;
        }
        idle++;
        if (idle >= LOG_IDLE_FLUSH_TICKS) {
            idle = 0;
            (*log).flush_point = (*log).written;
            FileWriter_flush(&(*log).writer);
        }
        struct timespec ts = { .tv_sec = 0, .tv_nsec = LOG_IDLE_POLL_NANOS };
        nanosleep(&ts, nullptr);
    }
    drain_and_write(log);
    FileWriter_flush(&(*log).writer);
    return nullptr;
}

/** Initialize the bounded record ring, file sink, counters, and writer thread. */
bool Log_init(Log *log, const char *path, size_t slot_count) {
    if (!log || !path)
        return false;

    memset(log, 0, sizeof(*log));
    (*log).slot_count = 1;
    while ((*log).slot_count < slot_count)
        (*log).slot_count <<= 1;
    (*log).slot_mask = (*log).slot_count - 1;
    (*log).slot_size = LOG_SLOT_SIZE;

    strncpy((*log).path, path, FILE_PATH_MAX - 1);
    (*log).path[FILE_PATH_MAX - 1] = '\0';

    if (!FileWriter_open(&(*log).writer, path))
        return false;
    write_header(log);

    (*log).arena = (uint8_t*) calloc(LOG_SLOT_BASE + (*log).slot_count * (*log).slot_size, 1);
    if (!(*log).arena) {
        FileWriter_close(&(*log).writer);
        return false;
    }

    (*log).enabled = true;
    (*log).active = true;
    atomic_init(&(*log).running, true);
    atomic_init(&(*log).appended, 0);
    atomic_init(&(*log).dropped, 0);
    (*log).written = 0;
    (*log).flush_point = 0;

    if (pthread_create(&(*log).thread, nullptr, writer_main, log) != 0) {
        atomic_store(&(*log).running, false);
        free((*log).arena);
        (*log).arena = nullptr;
        FileWriter_close(&(*log).writer);
        (*log).enabled = false;
        return false;
    }
    (*log).thread_started = true;
    return true;
}

/** Initialize logging at VexHome's default log path and default ring capacity. */
bool Log_initDefault(Log *log) {
    return Log_init(log, VexHome_defaultLogPath(), LOG_DEFAULT_SLOT_COUNT);
}

/** Stop and join the writer, drain remaining records, and release its ring and file sink. */
void Log_shutdown(Log *log) {
    if (!log)
        return;
    atomic_store(&(*log).running, false);
    if ((*log).thread_started) {
        pthread_join((*log).thread, nullptr);
        (*log).thread_started = false;
    }
    if ((*log).arena) {
        drain_and_write(log);
        free((*log).arena);
        (*log).arena = nullptr;
    }
    FileWriter_close(&(*log).writer);
    (*log).enabled = false;
}

/** Return whether initialization established a usable log sink. */
bool Log_isEnabled(const Log *log) {
    return (*log).enabled;
}

/** Return the runtime append gate state. */
bool Log_isActive(const Log *log) {
    return (*log).active;
}

/** Change the append gate when the log sink is enabled. */
void Log_setActive(Log *log, bool on) {
    if ((*log).enabled)
        (*log).active = on;
}

/** Return the path stored at initialization. */
const char *Log_path(const Log *log) {
    return (*log).path;
}

/** Return the atomic count of records successfully published by producers. */
uint64_t Log_appended(const Log *log) {
    return atomic_load_explicit(&(*log).appended, memory_order_relaxed);
}

/** Return the atomic count of records dropped because the ring was full. */
uint64_t Log_dropped(const Log *log) {
    return atomic_load_explicit(&(*log).dropped, memory_order_relaxed);
}

/** Return the writer-owned count of records written to the sink. */
uint64_t Log_written(const Log *log) {
    return (*log).written;
}

/** Publish a timestamped event and five values, or increment dropped when the ring has no slot. */
void Log_append(Log *log, int kind, int64_t v0, int64_t v1, int64_t v2,
                int64_t v3, int64_t v4) {
    if (!log || !(*log).enabled || !(*log).active || !(*log).arena)
        return;
    int64_t index = claim_slot(log);
    if (index < 0) {
        atomic_fetch_add_explicit(&(*log).dropped, 1, memory_order_relaxed);
        return;
    }
    size_t slot_idx = (size_t)index & (*log).slot_mask;
    uint8_t *slot = (*log).arena + LOG_SLOT_BASE + slot_idx * (*log).slot_size;

    uint32_t k = (uint32_t)kind;
    memcpy(slot, &k, 4);
    int64_t ts = monotonic_nanos();
    memcpy(slot + 8, &ts, 8);
    memcpy(slot + 16, &v0, 8);
    memcpy(slot + 24, &v1, 8);
    memcpy(slot + 32, &v2, 8);
    memcpy(slot + 40, &v3, 8);
    memcpy(slot + 48, &v4, 8);

    _Atomic int32_t *valid = (_Atomic int32_t*) (slot + 4);
    atomic_store_explicit(valid, 1, memory_order_release);
    atomic_fetch_add_explicit(&(*log).appended, 1, memory_order_relaxed);
}

/** Append an event kind with all five value fields set to zero. */
void Log_appendKind(Log *log, int kind) {
    Log_append(log, kind, 0, 0, 0, 0, 0);
}
