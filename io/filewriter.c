#include "io/filewriter.h"
// Production R2 I/O owner: Relational Engine; existing FileWriter ABI preserved.

#include <string.h>

#include "io/file.h"
#include "annotation/annotation.h"

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
 * double-close is a no-op.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Filewriter (io/filewriter.c)
 * ============================================================================
 * the FileWriter class, ported from io/FileWriter.java.
 *
 * STRUCT FIELDS (Mirroring io/filewriter.h):
 * ----------------------------------------------------------------------------
 *   FileWriter {
 *     FILE *out; // instance state
 *     bool open; // instance state
 *     uint64_t bytes_written; // writer-side flushed count
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

/** Open a binary output path, creating missing parent directories and enabling full buffering. */
bool FileWriter_open(FileWriter *w, const char *path) {
    if (!w || !path)
        return false;
    (*w).open = false;
    (*w).bytes_written = 0;

    const char *slash = strrchr(path, '/');
    if (slash && slash != path) {
        size_t len = (size_t)(slash - path);
        char parent[FILE_PATH_MAX];
        if (len >= FILE_PATH_MAX)
            return false;
        memcpy(parent, path, len);
        parent[len] = '\0';
        if (!File_mkdirs(parent))
            return false;
    }

    (*w).out = fopen(path, "wb");
    if (!(*w).out)
        return false;
    setvbuf((*w).out, (*w).buffer, _IOFBF, sizeof((*w).buffer));
    (*w).open = true;
    return true;
}

/** Write one complete byte span when open, adding its length to the flushed-count field on success. */
void FileWriter_write(FileWriter *w, const uint8_t *data, size_t len) {
    if (!(*w).open || len == 0)
        return;
    if (fwrite(data, 1, len, (*w).out) == len)
        (*w).bytes_written += len;
}

/** Flush the stdio buffer when the writer is open. */
void FileWriter_flush(FileWriter *w) {
    if ((*w).open)
        fflush((*w).out);
}

/** Flush and close an open writer, then clear its handle and open flag. */
void FileWriter_close(FileWriter *w) {
    if (!(*w).open)
        return;
    fflush((*w).out);
    fclose((*w).out);
    (*w).out = nullptr;
    (*w).open = false;
}

/** Return the writer's tracked byte count. */
uint64_t FileWriter_bytesWritten(const FileWriter *w) {
    return (*w).bytes_written;
}
