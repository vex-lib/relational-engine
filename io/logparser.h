#ifndef IO_LOGPARSER_H
#define IO_LOGPARSER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// io/logparser.h — the LogParser class, ported from io/LogParser.java.
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

// True if the file starts with a valid ANTI log header.
bool LogParser_isLogFile(const char *path);

// Number of records in the file, or -1 if it is not a valid log file.
int64_t LogParser_count(const char *path);

// Stream every record to handler. Returns the record count, or -1 on failure.
int64_t LogParser_parse(const char *path, LogRecordFn handler, void *userdata);

// Format one record relative to base_ts, e.g.
//   "12.345 ms  produce  3  12  0  0  0"
// Returns the number of chars written (excluding NUL), or -1 on truncation.
int LogParser_formatRecord(char *out, size_t out_cap, int kind, int64_t ts,
                           int64_t base_ts, const char *name,
                           int64_t v0, int64_t v1, int64_t v2,
                           int64_t v3, int64_t v4);

// Display name for a LogKind (legacy Log.name). Falls back to "kind#N".
const char *LogParser_kindName(int kind);

#endif