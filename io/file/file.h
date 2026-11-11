#ifndef IO_FILE_FILE_H
#define IO_FILE_FILE_H

#include "vex.h"
#include <stdio.h>

#include "lang/annotation.h"
#include "lang/primitive.h"
#include "nio/mem.h"
#include "type/type.h"

#define TYPE_FILE_SINGLETON       Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 3u)
#define TYPE_FILEPTR_SINGLETON    Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 10u)
#define TYPE_FINDEX_SINGLETON     Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 11u)

// io/file/file.h — the File class and file-view / file-index types, ported from io/File.java.
//
// A self-describing memory block wrapping a stdio stream: name, handle, cached
// size, read/write cursor, and mode. Reads and writes move Bytes straight
// between the file and caller-owned memory. Mode flags mirror the legacy
// ForeignMemory FILE_MODE_* set. Creates parent directories when CREATE is set.
//
// FilePtr is a non-owning view into file bytes (base + offset + length). It
// borrows its base from a mapped region and never allocates or frees.
// FIndex maps a 64-bit key hash to a (offset, length) record in a file over
// arena-backed engine memory. Both live under the Relational Memory Law.

#define FILE_MODE_READ     0x01u
#define FILE_MODE_WRITE    0x02u
#define FILE_MODE_APPEND   0x04u
#define FILE_MODE_CREATE   0x08u
#define FILE_MODE_TRUNCATE 0x10u

#define FILE_PATH_MAX 1024

;;CLASSNAME("File")
typedef struct File {
    char name[FILE_PATH_MAX];
    FILE *handle;
    int64_t size;
    int64_t position;
    u32 mode;
} File;

File *File_open(const char *path, u32 mode);

bool File_close(File *f);

int64_t File_read(File *f, void *dest, int64_t max_len);

int64_t File_write(File *f, const void *src, int64_t len);

bool File_seek(File *f, int64_t position);

int64_t File_refreshSize(File *f);

bool File_flush(File *f);

FILE *File_handle(const File *f);
const char *File_name(const File *f);
int64_t File_size(const File *f);
int64_t File_pos(const File *f);
u32 File_mode(const File *f);
bool File_eof(const File *f);

bool File_exists(const char *path);
bool File_isDirectory(const char *path);
bool File_mkdirs(const char *path);
bool File_delete(const char *path);

// --- FilePtr: non-owning view into file bytes -------------------------------

;;CLASSNAME("FilePtr")
typedef struct FilePtr {
    u8  *base;     // ;;WHAT("u8*") borrowed mapped-region base, never freed here
    u64 offset;    // byte offset into the region
    u64 length;    // byte length of this view
} FilePtr;

FilePtr FilePtr_from(void *base, u64 offset, u64 length);
u8      *FilePtr_data(FilePtr self);
u64      FilePtr_length(FilePtr self);
u64      FilePtr_offset(FilePtr self);
bool     FilePtr_isEmpty(FilePtr self);

// --- FIndex: key hash → (offset, length) file index -------------------------

typedef struct FIndexSlot FIndexSlot;

;;CLASSNAME("FIndex")
typedef struct FIndex {
    FIndexSlot *slots;
    bytes capacity;
    bytes count;
    bytes used;
    MemoryArena *arena;
} FIndex;

FIndex FIndex_create(bytes initialCapacity);
FIndex FIndex_createArena(bytes initialCapacity, MemoryArena *arena);
void     FIndex_shutdown(FIndex *self);
bool     FIndex_find(FIndex *self, u64 keyHash, u64 *outOffset, u64 *outLength);
bool     FIndex_insert(FIndex *self, u64 keyHash, u64 offset, u64 length);
void     FIndex_remove(FIndex *self, u64 keyHash);
u64      FIndex_count(const FIndex *self);
bool     FIndex_live(const FIndex *self);

// --- FileWriter: buffered binary writer -------------------------------------

// io/file/file.h — the FileWriter class, ported from io/FileWriter.java.
//
// Buffered binary file writer used by Log on the writer thread. A 64 KB
// stdio buffer coalesces records into large writes.

#define FILEWRITER_BUFFER_SIZE (1 << 16)

;;CLASSNAME("FileWriter")
typedef struct FileWriter {
    FILE *out;
    bool open;
    u64 bytes_written;
    char buffer[FILEWRITER_BUFFER_SIZE];
} FileWriter;

bool FileWriter_open(FileWriter *w, const char *path);

void FileWriter_write(FileWriter *w, const u8 *data, bytes len);

void FileWriter_flush(FileWriter *w);
void FileWriter_close(FileWriter *w);

u64 FileWriter_bytesWritten(const FileWriter *w);

// --- FileEventWatcher: cross-platform file event watcher ---------------------

// io/file/file.h — cross-platform file event watcher.
//
// Watches a directory tree and delivers creation/modification/deletion/rename
// events to a caller-supplied callback. macOS uses FSEvents (GCD-backed);
// other platforms are stubs that report unsupported until implemented. The
// watcher queues events and delivers them on the caller's thread via pump(),
// so callbacks never run on the OS watcher thread — lock-safe for hotcwap.

typedef enum FileEventType {
    FILE_EVENT_NONE     = 0,
    FILE_EVENT_CREATED  = 1 << 0,
    FILE_EVENT_MODIFIED = 1 << 1,
    FILE_EVENT_DELETED  = 1 << 2,
    FILE_EVENT_RENAMED  = 1 << 3,
    FILE_EVENT_ALL      = 15
} FileEventType;

typedef void (*FileEventCallback)(const char *path, FileEventType type, void *userdata);

;;CLASSNAME("FileEventWatcher")
typedef struct FileEventWatcher FileEventWatcher;

FileEventWatcher *FileEventWatcher_create(const char *path, FileEventType mask,
                                          FileEventCallback callback, void *userdata);

bool FileEventWatcher_start(FileEventWatcher *watcher);

void FileEventWatcher_stop(FileEventWatcher *watcher);

void FileEventWatcher_pump(FileEventWatcher *watcher);

void FileEventWatcher_destroy(FileEventWatcher *watcher);


void HotFileSys_init(void);
void HotFileSys_shutdown(void);
void HotFileSys_pumpEvents(void);

#endif
