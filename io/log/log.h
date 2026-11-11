#ifndef IO_LOG_LOG_H
#define IO_LOG_LOG_H

#include <pthread.h>
#include <stdatomic.h>
#include "vex.h"

#include "io/file/file.h"

// io/log/log.h — the LogKind taxonomy, ported from io/LogKind.java.
//
// Numeric event kinds for Log. Payload slots are documented per kind;
// coordinates are stored as fixed-point (value * 1000) so parser output stays
// readable.

#define LOG_KIND_RENDER_PRODUCE 1    // v0=slot, v1=drawCount
#define LOG_KIND_RENDER_PRESENT 2    // v0=slot, v1=presentCount
#define LOG_KIND_RENDER_DROPPED 3    // v0=slot, v1=reason (0=skipped, 1=_out-of-date)

#define LOG_KIND_KEY_DOWN   10       // v0=keyCode, v1=exactNanos
#define LOG_KIND_KEY_REPEAT 11       // v0=keyCode, v1=exactNanos
#define LOG_KIND_KEY_UP     12       // v0=keyCode, v1=exactNanos

#define LOG_KIND_MOUSE_DOWN 20       // v0=button, v1=x*1000, v2=y*1000
#define LOG_KIND_MOUSE_UP   21       // v0=button, v1=x*1000, v2=y*1000
#define LOG_KIND_MOUSE_MOVE 22       // v0=0, v1=x*1000, v2=y*1000

#define LOG_KIND_TOUCH_DOWN    30    // v0=touchId, v1=x*1000, v2=y*1000, v3=pressure*1000
#define LOG_KIND_TOUCH_UP      31    // v0=touchId, v1=x*1000, v2=y*1000
#define LOG_KIND_TOUCH_MOVE    32    // v0=touchId, v1=x*1000, v2=y*1000, v3=pressure*1000
#define LOG_KIND_TOUCH_CANCEL  33    // v0=touchId

// io/log/log.h — the Log class, ported from io/Log.java.
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
#define TYPE_LOG_SINGLETON Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 4u)

;;CLASSNAME("Log")
typedef struct Log {
    u8 *arena;            // head/tail + slot arena, carved once
    bytes slot_count;
    bytes slot_mask;
    bytes slot_size;
    FileWriter writer;
    pthread_t thread;
    bool thread_started;
    atomic_bool running;
    bool enabled;              // built with a real sink
    bool active;               // runtime gate; false makes append a no-op
    _Atomic u64 appended;
    _Atomic u64 dropped;
    u64 written;          // writer thread only
    u64 flush_point;      // writer thread only
    char path[FILE_PATH_MAX];
} Log;

bool Log_init(Log *log, const char *path, bytes slot_count);

void Log_shutdown(Log *log);

bool Log_isEnabled(const Log *log);
bool Log_isActive(const Log *log);
void Log_setActive(Log *log, bool on);
const char *Log_path(const Log *log);

u64 Log_appended(const Log *log);
u64 Log_dropped(const Log *log);
u64 Log_written(const Log *log);

void Log_append(Log *log, int kind, int64_t v0, int64_t v1, int64_t v2,
                int64_t v3, int64_t v4);

void Log_appendKind(Log *log, int kind);

// io/log/log.h — the LogParser class, ported from io/LogParser.java.
//
// Reads and formats binary logs produced by Log.
//
// File layout: 12-byte header ("ANTILOG" + version + recordSize) followed by
// big-endian 52-byte records: kind(4) + tsNanos(8) + v0..v4(40).

#define LOGPARSER_RECORD_BYTES 52
#define LOGPARSER_HEADER_BYTES 12

// Per-record callback. userdata is whatever was passed to LogParser_parse.
typedef void (*LogRecordFn)(void *userdata, int kind, int64_t ts,
                            int64_t v0, int64_t v1, int64_t v2,
                            int64_t v3, int64_t v4);

bool LogParser_isLogFile(const char *path);

int64_t LogParser_count(const char *path);

int64_t LogParser_parse(const char *path, LogRecordFn handler, void *userdata);

int LogParser_formatRecord(char *out, bytes out_cap, int kind, int64_t ts,
                           int64_t base_ts, const char *name,
                           int64_t v0, int64_t v1, int64_t v2,
                           int64_t v3, int64_t v4);

const char *LogParser_kindName(int kind);

#endif
