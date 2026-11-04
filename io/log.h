#ifndef IO_LOG_H
#define IO_LOG_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "io/file.h"
#include "io/filewriter.h"

// io/log.h — the Log class, ported from io/Log.java.
//
// Zero-allocation off-heap binary logger.
//
// Hot path (Log_append): CAS-claims a 64-byte slot in a fixed ring, writes the
// record, release-publishes via an atomic valid flag, and bumps a counter. No
// allocations, no locks, no syscalls on the producing thread.
//
// A writer thread drains the ring and writes raw 52-byte big-endian records to
// a file through FileWriter; all formatting happens on read via LogParser.

#define LOG_HEAD_OFF 0
#define LOG_TAIL_OFF 8
#define LOG_SLOT_BASE 16
#define LOG_SLOT_SIZE 64
#define LOG_RECORD_BYTES 52
#define LOG_HEADER_BYTES 12
#define LOG_DEFAULT_SLOT_COUNT (1 << 14)
#define LOG_FLUSH_EVERY 4096
#define LOG_IDLE_FLUSH_TICKS 1000
#define LOG_IDLE_POLL_NANOS 100000

typedef struct Log {
    uint8_t *arena;            // head/tail + slot arena, carved once
    size_t slot_count;
    size_t slot_mask;
    size_t slot_size;
    FileWriter writer;
    pthread_t thread;
    bool thread_started;
    atomic_bool running;
    bool enabled;              // built with a real sink
    bool active;               // runtime gate; false makes append a no-op
    _Atomic uint64_t appended;
    _Atomic uint64_t dropped;
    uint64_t written;          // writer thread only
    uint64_t flush_point;      // writer thread only
    char path[FILE_PATH_MAX];
} Log;

// Open the logger at path (truncating) with the given slot count (rounded up
// to a power of two). Spawns the writer thread. Returns false on failure.
bool Log_init(Log *log, const char *path, size_t slot_count);

// Log to the default sink (see VexHome_defaultLogPath()).
bool Log_initDefault(Log *log);

// Stop the writer, drain remaining records, close the file, free the ring.
// Idempotent.
void Log_shutdown(Log *log);

bool Log_isEnabled(const Log *log);
bool Log_isActive(const Log *log);
void Log_setActive(Log *log, bool on);
const char *Log_path(const Log *log);

uint64_t Log_appended(const Log *log);
uint64_t Log_dropped(const Log *log);
uint64_t Log_written(const Log *log);

// Append a record with up to five payload slots. No-op when disabled/inactive.
void Log_append(Log *log, int kind, int64_t v0, int64_t v1, int64_t v2,
                int64_t v3, int64_t v4);

// Append a kind-only record (all payload slots zero).
void Log_appendKind(Log *log, int kind);

#endif