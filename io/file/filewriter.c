#include "vex.h"
#include "io/file/file.h"
// Production R2 I/O owner: Relational Engine; existing FileWriter ABI preserved.

#include <string.h>

#include "lang/annotation.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Filewriter
 * ============================================================================
 * The FileWriter class, ported from io/FileWriter.java. Buffered binary file
 * writer: fopen with a full-buffer staging buffer, optional parent-directory
 * creation via File_mkdirs, and flush/close lifecycle. bytes_written tracks
 * the flushed byte count for progress reporting. The FILE handle and open
 * flag live on the caller-owned FileWriter struct; close nulls the handle so
 * d64-close is a no-op.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Filewriter (io/filewriter.c)
 * ============================================================================
 * the FileWriter class, ported from io/FileWriter.java.
 *
 * STRUCT FIELDS (Mirroring io/file/file.h):
 * ----------------------------------------------------------------------------
 *   FileWriter {
 *     FILE *out; // instance state
 *     bool open; // instance state
 *     u64 bytes_written; // writer-side flushed count
 *     char buffer[FILEWRITER_BUFFER_SIZE]; // staging buffer
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - FileWriter_open(w, path)
 *   - FileWriter_write(w, data, len)
 *   - FileWriter_flush(w)
 *   - FileWriter_close(w)
 *   - FileWriter_bytesWritten(w)
 * ============================================================================
 */


// filewriter.c — FileWriter port (Legacy: io/FileWriter.java).

;;GETTER
static inline bool FileWriter_openField(const FileWriter *w) { return (*w).open; }
;;SETTER
static inline fn FileWriter_setOpen(FileWriter *w, bool open) { (*w).open = open; }
;;GETTER
static inline FILE *FileWriter_out(const FileWriter *w) { return (*w).out; }
;;SETTER
static inline fn FileWriter_setOut(FileWriter *w, FILE *out) { (*w).out = out; }
;;GETTER
static inline char *FileWriter_bufferMut(FileWriter *w) { return (*w).buffer; }
;;GETTER
static inline u64 FileWriter_bytesWrittenField(const FileWriter *w) { return (*w).bytes_written; }
;;SETTER
static inline fn FileWriter_setBytesWritten(FileWriter *w, u64 bw) { (*w).bytes_written = bw; }

/** Open a binary output path, creating missing parent directories and enabling full buffering. */
bool FileWriter_open(FileWriter *w, const char *path) {
    if (!w || !path) 
        return false;

    FileWriter_setOpen(w, false);

    FileWriter_setBytesWritten(w, 0);

    const char *slash = strrchr(path, '/');

    if (slash && slash != path) {
        bytes len = (bytes)(slash - path);

        char parent[FILE_PATH_MAX];

        if (len >= FILE_PATH_MAX) 
            return false;

        memcpy(parent, path, len);

        parent[len] = '\0';

        if (!File_mkdirs(parent)) 
            return false;
    }

    FILE *out = fopen(path, "wb");
    FileWriter_setOut(w, out);

    if (!out) 
        return false;

    setvbuf(out, FileWriter_bufferMut(w), _IOFBF, sizeof(((FileWriter) {0}).buffer));

    FileWriter_setOpen(w, true);

    return true;
}

/** Write one complete byte span when open, adding its length to the flushed-count field on success. */
fn FileWriter_write(FileWriter *w, const u8 *data, bytes len) {
    if (!FileWriter_openField(w) || len == 0) 
        return;

    if (fwrite(data, 1, len, FileWriter_out(w)) == len) 
        FileWriter_setBytesWritten(w, FileWriter_bytesWrittenField(w) + len);
}

/** Flush the stdio buffer when the writer is open. */
fn FileWriter_flush(FileWriter *w) {
    if (FileWriter_openField(w)) 
        fflush(FileWriter_out(w));
}

/** Flush and close an open writer, then clear its handle and open flag. */
fn FileWriter_close(FileWriter *w) {
    if (!FileWriter_openField(w)) 
        return;

    FILE *out = FileWriter_out(w);
    fflush(out);

    fclose(out);

    FileWriter_setOut(w, nullptr);

    FileWriter_setOpen(w, false);
}

/** Return the writer's tracked byte count. */
u64 FileWriter_bytesWritten(const FileWriter *w) {
    return FileWriter_bytesWrittenField(w);
}
