#include "io/file.h"
// Production R2 I/O owner: Relational Engine; existing File ABI preserved.

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/annotation.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: File
 * ============================================================================
 * stdio-backed file handle ported from io/File.java: opens paths under a
 * mode-flag contract (read/write/append/create/truncate), tracks size and
 * cursor position, and exposes symmetric getters for every state field. The
 * File struct is arena-allocated through Memory_alloc(TYPE_FILE_SINGLETON)
 * and freed by File_close, which also closes the OS handle first (detach
 * before free). Lives at R2 as a leaf I/O behavior consumed by VexHome and
 * the R3/R4 layers above.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: File (io/file.c)
 * ============================================================================
 * the File class, ported from io/File.java.
 *
 * STRUCT FIELDS (Mirroring io/file.h):
 * ----------------------------------------------------------------------------
 *   File {
 *     char name[FILE_PATH_MAX]; // field/entry name
 *     FILE *handle; // OS file handle
 *     int64_t size; // file cursor/size state
 *     int64_t position; // file cursor/size state
 *     uint32_t mode; // open mode flags
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - File_open(path, mode)
 *   - File_close(f)
 *   - File_read(f, dest, max_len)
 *   - File_write(f, src, len)
 *   - File_seek(f, position)
 *   - File_refreshSize(f)
 *   - File_flush(f)
 *   - File_handle(f)
 *   - File_name(f)
 *   - File_size(f)
 *   - File_pos(f)
 *   - File_mode(f)
 *   - File_eof(f)
 *   - File_exists(path)
 *   - File_mkdirs(path)
 *   - File_delete(path)
 *
 * Getters:
 *   - File_isDirectory(path)
 * ============================================================================
 */


// file.c — File port (Legacy: io/File.java). stdio-backed file handle.

/** Translate File mode flags to stdio fopen modes, preserving existing bytes unless truncation is requested. */
static FILE *map_open(const char *path, uint32_t mode) {
    bool read = (mode & FILE_MODE_READ) != 0;
    bool write = (mode & (FILE_MODE_WRITE | FILE_MODE_APPEND)) != 0;
    bool append = (mode & FILE_MODE_APPEND) != 0;
    bool create = (mode & FILE_MODE_CREATE) != 0;
    bool truncate = (mode & FILE_MODE_TRUNCATE) != 0;

    if (!write)
        return fopen(path, "rb");

    if (append)
        return fopen(path, read ? "a+b.sh" : "ab");

    if (truncate)
        return fopen(path, read ? "w+b.sh" : "wb");

    // Write without truncate/append: preserve existing content.
    if (create) {
        FILE *f = fopen(path, read ? "r+b.sh" : "rb+");
        if (f)
            return f;
        return fopen(path, read ? "w+b.sh" : "wb");
    }

    // Plain write: file must already exist.
    return fopen(path, read ? "r+b.sh" : "rb+");
}

/** Open a path using the requested mode flags and create its File in the engine arena. */
File *File_open(const char *path, uint32_t mode) {
    if (!path)
        return nullptr;
    if ((mode & FILE_MODE_CREATE) != 0) {
        // Create missing parent directories (rough: parent of last '/').
        const char *slash = strrchr(path, '/');
        if (slash && slash != path) {
            char parent[FILE_PATH_MAX];
            size_t len = (size_t)(slash - path);
            if (len < FILE_PATH_MAX) {
                memcpy(parent, path, len);
                parent[len] = '\0';
                File_mkdirs(parent);
            }
        }
    }

    FILE *handle = map_open(path, mode);
    if (!handle)
        return nullptr;

    File *f = Memory_alloc(TYPE_FILE_SINGLETON, sizeof(File));
    if (!f) {
        fclose(handle);
        return nullptr;
    }
    strncpy((*f).name, path, FILE_PATH_MAX - 1);
    (*f).name[FILE_PATH_MAX - 1] = '\0';
    (*f).handle = handle;
    (*f).mode = mode;

    bool append = (mode & FILE_MODE_APPEND) != 0;

    fseek(handle, 0, SEEK_END);
    long sz = ftell(handle);
    if (sz < 0)
        sz = 0;
    (*f).size = (int64_t)sz;
    if (append) {
        (*f).position = (int64_t)sz;
        fseek(handle, 0, SEEK_END);
    } else {
        (*f).position = 0;
        fseek(handle, 0, SEEK_SET);
    }
    return f;
}

/** Close the OS handle and release the arena-owned File object. */
bool File_close(File *f) {
    if (!f)
        return false;
    if ((*f).handle)
        fclose((*f).handle);
    (*f).handle = nullptr;
    Memory_free(f);
    return true;
}

/** Read up to max_len bytes at the current cursor, returning bytes read or -1 on error. */
int64_t File_read(File *f, void *dest, int64_t max_len) {
    if (!f || !(*f).handle || !dest)
        return -1;
    if (max_len < 0)
        return -1;
    if (max_len == 0)
        return 0;
    size_t n = fread(dest, 1, (size_t) max_len, (*f).handle);
    if (n == 0 && ferror((*f).handle))
        return -1;
    (*f).position += (int64_t) n;
    return (int64_t) n;
}

/** Write up to len bytes at the current cursor and update tracked position and size. */
int64_t File_write(File *f, const void *src, int64_t len) {
    if (!f || !(*f).handle || !src)
        return -1;
    if (len < 0)
        return -1;
    if (len == 0)
        return 0;
    size_t n = fwrite(src, 1, (size_t) len, (*f).handle);
    if (n == 0 && ferror((*f).handle))
        return -1;
    (*f).position += (int64_t) n;
    if ((*f).position > (*f).size)
        (*f).size = (*f).position;
    return (int64_t)n;
}

/** Seek to an absolute nonnegative byte offset and update the tracked cursor on success. */
bool File_seek(File *f, int64_t position) {
    if (!f || !(*f).handle || position < 0)
        return false;
    if (fseek((*f).handle, position, SEEK_SET) != 0)
        return false;
    (*f).position = position;
    return true;
}

/** Refresh the tracked file size from the open handle, returning -1 on failure. */
int64_t File_refreshSize(File *f) {
    if (!f || !(*f).handle)
        return -1;
    fseek((*f).handle, 0, SEEK_END);
    long sz = ftell((*f).handle);
    if (sz < 0)
        return -1;
    (*f).size = (int64_t)sz;
    return (*f).size;
}

/** Flush buffered output for an open file handle. */
bool File_flush(File *f) {
    if (!f || !(*f).handle)
        return false;
    return fflush((*f).handle) == 0;
}

/** Return the stdio handle borrowed from the File object. */
FILE *File_handle(const File *f) {
    return (*f).handle;
}

/** Return the File object's stored path. */
const char *File_name(const File *f) {
    return (*f).name;
}

/** Return the object's tracked file size. */
int64_t File_size(const File *f) {
    return (*f).size;
}

/** Return the object's tracked cursor position. */
int64_t File_pos(const File *f) {
    return (*f).position;
}

/** Return the mode flags supplied when the File was opened. */
uint32_t File_mode(const File *f) {
    return (*f).mode;
}

/** Report whether the tracked cursor has reached or passed the tracked file size. */
bool File_eof(const File *f) {
    return (*f).position >= (*f).size;
}

/** Return whether stat succeeds for the supplied filesystem path. */
bool File_exists(const char *path) {
    if (!path)
        return false;
    struct stat st;
    return stat(path, &st) == 0;
}

/** Return whether the path exists and identifies a directory. */
bool File_isDirectory(const char *path) {
    if (!path)
        return false;
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

/** Create each missing path component with mode 0755, accepting existing directories. */
bool File_mkdirs(const char *path) {
    if (!path)
        return false;
    if (File_exists(path))
        return true;
    char buf[FILE_PATH_MAX];
    size_t len = strlen(path);
    if (len >= FILE_PATH_MAX)
        return false;
    memcpy(buf, path, len + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST)
                return false;
            *p = '/';
        }
    }
    return mkdir(buf, 0755) == 0 || errno == EEXIST;
}

/** Remove a filesystem path using the C remove operation. */
bool File_delete(const char *path) {
    if (!path)
        return false;
    return remove(path) == 0;
}
