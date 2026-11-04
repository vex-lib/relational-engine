#ifndef IO_FILE_H
#define IO_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// io/file.h — the File class, ported from io/File.java.
//
// A self-describing memory block wrapping a stdio stream: name, handle, cached
// size, read/write cursor, and mode. Reads and writes move Bytes straight
// between the file and caller-owned memory. Mode flags mirror the legacy
// ForeignMemory FILE_MODE_* set. Creates parent directories when CREATE is set.

#define FILE_MODE_READ     0x01u
#define FILE_MODE_WRITE    0x02u
#define FILE_MODE_APPEND   0x04u
#define FILE_MODE_CREATE   0x08u
#define FILE_MODE_TRUNCATE 0x10u

#define FILE_PATH_MAX 1024

typedef struct File {
    char name[FILE_PATH_MAX];
    FILE *handle;
    int64_t size;
    int64_t position;
    uint32_t mode;
} File;

// Open a path with the given mode flags and return a File block, or nullptr on
// failure. Creates missing parent directories when CREATE is set.
File *File_open(const char *path, uint32_t mode);

// Close the underlying stream and free the File block. Returns false when the
// pointer is nullptr. The pointer is invalid after the call.
bool File_close(File *f);

// Read up to max_len Bytes into caller-owned memory at dest. Returns Bytes
// read (advancing the cursor), or -1 on error.
int64_t File_read(File *f, void *dest, int64_t max_len);

// Write len Bytes from caller-owned memory at src. Returns Bytes written
// (advancing the cursor and growing the cached size), or -1 on error.
int64_t File_write(File *f, const void *src, int64_t len);

// Position the file for the next read/write. Returns true on success.
bool File_seek(File *f, int64_t position);

// Refresh the cached size from the underlying file. Returns the size, or -1.
int64_t File_refreshSize(File *f);

bool File_flush(File *f);

// Accessors.
FILE *File_handle(const File *f);
const char *File_name(const File *f);
int64_t File_size(const File *f);
int64_t File_pos(const File *f);
uint32_t File_mode(const File *f);
bool File_eof(const File *f);

// Path-level static ops.
bool File_exists(const char *path);
bool File_isDirectory(const char *path);
bool File_mkdirs(const char *path);
bool File_delete(const char *path);

#endif