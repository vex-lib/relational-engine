#include "vex.h"
#include "io/log/log.h"
// Production R2 I/O owner: Relational Engine; existing LogParser ABI preserved.

#include <stdio.h>
#include <string.h>


;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Logparser
 * ============================================================================
 * Read-only parser for the engine's binary log format: a 7-byte magic header
 * plus version and fixed LOGPARSER_RECORD_BYTES record size, followed by
 * fixed-size records of big-endian kind/timestamp/value fields. It streams
 * records through a caller-supplied LogRecordFn callback, counts records,
 * and formats one record into a caller buffer as "<ms> <name> <v0..v4>".
 * The parser owns no state beyond a stack header/record buffer and never
 * allocates on the parse path.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Logparser (io/logparser.c)
 * ============================================================================
 * the LogParser class, ported from io/LogParser.java.
 *
 * STRUCT FIELDS: none — procedural (operates on binary log files on disk (read-only))
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - LogParser_count(path)
 *   - LogParser_parse(path, handler, userdata)
 *   - LogParser_formatRecord(out, out_cap, kind, ts, base_ts, name, v0, v1, v2, v3, v4)
 *   - LogParser_kindName(kind)
 *
 * Getters:
 *   - LogParser_isLogFile(path)
 * ============================================================================
 */


// logparser.c — LogParser port (Legacy: io/LogParser.java).

static const u8 LOGPARSER_MAGIC[7] = { 0x41, 0x4E, 0x54, 0x49, 0x4C, 0x4F, 0x47 };

/** Decode a 32-bit big-endian integer from four input bytes. */
static u32 read_be32(const u8 *b) {
    return ((u32)b[0] << 24) | ((u32)b[1] << 16)
        | ((u32)b[2] << 8) | (u32)b[3];
}

/** Decode a 64-bit big-endian integer from eight input bytes. */
static int64_t read_be64(const u8 *b) {
    u64 v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | (u64)b[i];
    return (int64_t)v;
}

/** Validate the format magic, version, and expected fixed record size. */
static bool is_log_header(const u8 *h) {
    for (int i = 0; i < 7; i++)
        if (h[i] != LOGPARSER_MAGIC[i])
            return false;
    return h[7] == 0x01 && read_be32(h + 8) == LOGPARSER_RECORD_BYTES;
}

/** Open a path and determine whether its leading bytes form a supported log header. */
bool LogParser_isLogFile(const char *path) {
    File *f = File_open(path, FILE_MODE_READ);
    if (!f)
        return false;
    u8 h[LOGPARSER_HEADER_BYTES];
    int64_t n = File_read(f, h, LOGPARSER_HEADER_BYTES);
    bool ok = n == LOGPARSER_HEADER_BYTES && is_log_header(h);
    File_close(f);
    return ok;
}

/** Return the number of complete records in a valid log, or -1 when its header cannot be read. */
int64_t LogParser_count(const char *path) {
    File *f = File_open(path, FILE_MODE_READ);
    if (!f)
        return -1;
    int64_t size = File_size(f);
    if (size < LOGPARSER_HEADER_BYTES) {
        File_close(f);
        return -1;
    }
    u8 h[LOGPARSER_HEADER_BYTES];
    if (File_read(f, h, LOGPARSER_HEADER_BYTES) != LOGPARSER_HEADER_BYTES
        || !is_log_header(h)) {
        File_close(f);
        return -1;
    }
    File_close(f);
    return (size - LOGPARSER_HEADER_BYTES) / LOGPARSER_RECORD_BYTES;
}

/** Stream complete decoded records to the callback and return the number delivered. */
int64_t LogParser_parse(const char *path, LogRecordFn handler, any userdata) {
    if (!handler)
        return -1;
    File *f = File_open(path, FILE_MODE_READ);
    if (!f)
        return -1;
    u8 h[LOGPARSER_HEADER_BYTES];
    if (File_read(f, h, LOGPARSER_HEADER_BYTES) != LOGPARSER_HEADER_BYTES
        || !is_log_header(h)) {
        File_close(f);
        return -1;
    }
    u8 rec[LOGPARSER_RECORD_BYTES];
    int64_t count = 0;
    while (File_read(f, rec, LOGPARSER_RECORD_BYTES) == LOGPARSER_RECORD_BYTES) {
        handler(userdata,
                (int)read_be32(rec),
                read_be64(rec + 4),
                read_be64(rec + 12),
                read_be64(rec + 20),
                read_be64(rec + 28),
                read_be64(rec + 36),
                read_be64(rec + 44));
        count++;
    }
    File_close(f);
    return count;
}

/** Format a record's relative timestamp, event name, and five values into the caller buffer. */
int LogParser_formatRecord(char *out, bytes out_cap, int kind, int64_t ts,
                           int64_t base_ts, const char *name,
                           int64_t v0, int64_t v1, int64_t v2,
                           int64_t v3, int64_t v4) {
    d64 ms = base_ts <= 0 ? 0.0 : (d64)(ts - base_ts) / 1000000.0;
    int64_t rounded = (int64_t)(ms * 1000.0 + (ms >= 0 ? 0.5 : -0.5));
    int64_t whole = rounded / 1000;
    int64_t frac = rounded % 1000;
    if (frac < 0)
        frac = -frac;

    int n = snprintf(out, out_cap, "%lld.%03lld ms  %s  %lld  %lld  %lld  %lld  %lld",
                     (long long)whole, (long long)frac,
                     name ? name : LogParser_kindName(kind),
                     (long long)v0, (long long)v1, (long long)v2,
                     (long long)v3, (long long)v4);
    if (n < 0 || (bytes)n >= out_cap)
        return -1;
    return n;
}

/** Return the registered event name or a static kind#N fallback string. */
const char *LogParser_kindName(int kind) {
    switch (kind) {
        case 1:  return "produce";
        case 2:  return "present";
        case 3:  return "drop";
        case 10: return "keyDown";
        case 11: return "keyRepeat";
        case 12: return "keyUp";
        case 20: return "mouseDown";
        case 21: return "mouseUp";
        case 22: return "mouseMove";
        case 30: return "touchDown";
        case 31: return "touchUp";
        case 32: return "touchMove";
        case 33: return "touchCancel";
        default: {
            static char fallback[32];
            snprintf(fallback, sizeof(fallback), "kind#%d", kind);
            return fallback;
        }
    }
}
