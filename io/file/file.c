#include "io/file/file.h"
// Production R2 I/O owner: Relational Engine; existing File ABI preserved.

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "nio/mem.h"
#include "type/re_type.h"
#include "vex.h"

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
 * STRUCT FIELDS (Mirroring io/file/file.h):
 * ----------------------------------------------------------------------------
 *   File {
 *     char name[FILE_PATH_MAX]; // field/entry name
 *     FILE *handle; // OS file handle
 *     int64_t size; // file cursor/size state
 *     int64_t position; // file cursor/size state
 *     u32 mode; // open mode flags
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
static FILE *map_open(const char *path, u32 mode) {
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

    if (create) {
        FILE *f = fopen(path, read ? "r+b.sh" : "rb+");
        if (f)
            return f;
        return fopen(path, read ? "w+b.sh" : "wb");
    }

    return fopen(path, read ? "r+b.sh" : "rb+");
}

/** Open a path using the requested mode flags and create its File in the engine arena. */
File *File_open(const char *path, u32 mode) {
    if (!path)
        return nullptr;
    if ((mode & FILE_MODE_CREATE) != 0) {
        const char *slash = strrchr(path, '/');
        if (slash && slash != path) {
            char parent[FILE_PATH_MAX];
            bytes len = (bytes)(slash - path);
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
int64_t File_read(File *f, any dest, int64_t max_len) {
    if (!f || !(*f).handle || !dest)
        return -1;
    if (max_len < 0)
        return -1;
    if (max_len == 0)
        return 0;
    bytes n = fread(dest, 1, (bytes) max_len, (*f).handle);
    if (n == 0 && ferror((*f).handle))
        return -1;
    (*f).position += (int64_t) n;
    return (int64_t) n;
}

/** Write up to len bytes at the current cursor and update tracked position and size. */
int64_t File_write(File *f, const_any src, int64_t len) {
    if (!f || !(*f).handle || !src)
        return -1;
    if (len < 0)
        return -1;
    if (len == 0)
        return 0;
    bytes n = fwrite(src, 1, (bytes) len, (*f).handle);
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
u32 File_mode(const File *f) {
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
    bytes len = strlen(path);
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

// ============================================================================
// FilePtr — non-owning view into file bytes
// ============================================================================

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: FilePtr
 * ============================================================================
 * Non-owning view into file bytes: base pointer + offset + length. Borrows
 * its base from a mapped region owned elsewhere (FixedMappedFile,
 * GrowableMappedFile, or any mmap'd backing). Zero steady-state allocation;
 * passed and returned by value. Never freed by the borrower — the region's
 * owner releases it.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: FilePtr (io/file/file.c)
 * ============================================================================
 * File borrow view over mapped file bytes.
 *
 * STRUCT FIELDS (Mirroring io/file/file.h):
 * ----------------------------------------------------------------------------
 *   FilePtr {
 *     u8  *base;    // borrowed mapped-region base (;;WHAT("u8*")), never freed here
 *     u64 offset;   // byte offset into the region
 *     u64 length;   // byte length of this view
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - FilePtr_from(any base, u64 offset, u64 length)
 *
 * Accessors:
 *   - FilePtr_data(FilePtr self) -> u8 *
 *   - FilePtr_length(FilePtr self) -> u64
 *   - FilePtr_offset(FilePtr self) -> u64
 *   - FilePtr_isEmpty(FilePtr self) -> bool
 * ============================================================================
 */

FilePtr FilePtr_from(any base, u64 offset, u64 length) {
    FilePtr self;

    self.base = (u8*) base;
    self.offset = offset;
    self.length = length;

    return self;
}

u8 *FilePtr_data(FilePtr self) {
    if (self.length == 0)
        return nullptr;

    return self.base + self.offset;
}

u64 FilePtr_length(FilePtr self) {
    return self.length;
}

u64 FilePtr_offset(FilePtr self) {
    return self.offset;
}

bool FilePtr_isEmpty(FilePtr self) {
    return self.length == 0 || self.base == nullptr;
}

// ============================================================================
// FIndex — key hash → (offset, length) file index
// ============================================================================

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: FIndex
 * ============================================================================
 * A flat, arena-backed open-addressed index from a 64-bit key hash to a
 * (byte offset, byte length) record. Slots are 32 bytes each; tombstones keep
 * probing correct across removals; growth rehashes live slots into a larger
 * table and releases the old one. No per-entry allocation, no generation pool,
 * no hidden second heap.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: FIndex (io/file/file.c)
 * ============================================================================
 * File key→offset index.
 *
 * STRUCT FIELDS (Mirroring io/file/file.h):
 * ----------------------------------------------------------------------------
 *   FIndexSlot {
 *     u64 keyHash;   // hashed key
 *     u64 offset;    // byte offset into the indexed file
 *     u64 length;    // byte length of the record at that offset
 *     u8  state;     // empty / occupied / tombstone
 *     u8  padding[7];
 *   }
 *
 *   FIndex {
 *     FIndexSlot *slots;
 *     bytes capacity;
 *     bytes count;
 *     bytes used;
 *     MemoryArena *arena;
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - FIndex_create(initialCapacity)
 *   - FIndex_createArena(initialCapacity, arena)
 *
 * Core Functions:
 *   - FIndex_shutdown(FIndex *)
 *   - FIndex_find(FIndex *, u64, u64 *, u64 *) -> bool
 *   - FIndex_insert(FIndex *, u64, u64, u64) -> bool
 *   - FIndex_remove(FIndex *, u64)
 *   - FIndex_count(const FIndex *) -> u64
 *   - FIndex_live(const FIndex *) -> bool
 * ============================================================================
 */

typedef enum FIndexSlotState {
    FINDEX_SLOT_EMPTY = 0,
    FINDEX_SLOT_OCCUPIED = 1,
    FINDEX_SLOT_TOMBSTONE = 2
} FIndexSlotState;

struct FIndexSlot {
    u64 keyHash;
    u64 offset;
    u64 length;
    u8 state;
    u8 padding[7];
};

_Static_assert(sizeof(FIndexSlot) == 32, "FIndexSlot must be one 32-byte slot");

/** Round a requested slot count up to a power-of-two table capacity. */
static bytes findexCapacity(bytes requested) {
    bytes capacity = 8;
    while (capacity < requested) {
        if (capacity > SIZE_MAX / 2)
            return 0;
        capacity <<= 1;
    }
    return capacity;
}

/** Allocate one table from the index's arena, or the default arena. */
static any findexAlloc(FIndex *self, bytes bytesCount) {
    if ((*self).arena)
        return MemoryArena_alloc((*self).arena, TYPE_FINDEX_SINGLETON, bytesCount);
    return Memory_alloc(TYPE_FINDEX_SINGLETON, bytesCount);
}

/** Release a table through the same owner that allocated it. */
static fn findexRelease(FIndex *self, any ptr) {
    if (!ptr)
        return;
    if ((*self).arena)
        MemoryArena_free((*self).arena, ptr);
    else
        Memory_free(ptr);
}

/** Home slot for a key in a power-of-two table. */
static bytes findexProbe(u64 keyHash, bytes capacity) {
    return (bytes) keyHash & (capacity - 1);
}

/** Find a key, or the first reusable slot when it is absent. */
static bytes findexFindSlot(const FIndex *self, u64 keyHash, bool *outFound) {
    bytes mask = (*self).capacity - 1;
    bytes index = findexProbe(keyHash, (*self).capacity);
    bytes firstTombstone = SIZE_MAX;

    for (bytes probes = 0; probes < (*self).capacity; probes++) {
        const FIndexSlot *slot = &(*self).slots[index];
        if ((*slot).state == FINDEX_SLOT_EMPTY) {
            *outFound = false;
            return firstTombstone != SIZE_MAX ? firstTombstone : index;
        }
        if ((*slot).state == FINDEX_SLOT_TOMBSTONE) {
            if (firstTombstone == SIZE_MAX)
                firstTombstone = index;
        } else if ((*slot).keyHash == keyHash) {
            *outFound = true;
            return index;
        }
        index = (index + 1) & mask;
    }

    *outFound = false;
    return firstTombstone;
}

/** Allocate a larger table, rehash live slots, and release the old table. */
static bool findexGrow(FIndex *self, bytes minCapacity) {
    bytes capacity = (*self).capacity ? (*self).capacity : 8;
    while (capacity < minCapacity) {
        if (capacity > SIZE_MAX / 2)
            return false;
        capacity <<= 1;
    }
    if (capacity > SIZE_MAX / sizeof(FIndexSlot))
        return false;

    FIndexSlot *slots = findexAlloc(self, capacity * sizeof(FIndexSlot));
    if (!slots)
        return false;
    memset(slots, 0, capacity * sizeof(FIndexSlot));

    bytes mask = capacity - 1;
    for (bytes i = 0; i < (*self).capacity; i++) {
        FIndexSlot *old = &(*self).slots[i];
        if ((*old).state != FINDEX_SLOT_OCCUPIED)
            continue;
        bytes index = findexProbe((*old).keyHash, capacity);
        while (slots[index].state == FINDEX_SLOT_OCCUPIED)
            index = (index + 1) & mask;
        slots[index] = (*old);
    }

    findexRelease(self, (*self).slots);
    (*self).slots = slots;
    (*self).capacity = capacity;
    (*self).used = (*self).count;
    return true;
}

FIndex FIndex_create(bytes initialCapacity) {
    return FIndex_createArena(initialCapacity, nullptr);
}

FIndex FIndex_createArena(bytes initialCapacity, MemoryArena *arena) {
    FIndex self = { .slots = nullptr, .capacity = 0, .count = 0, .used = 0, .arena = arena };
    if (initialCapacity == 0)
        return self;

    bytes capacity = findexCapacity(initialCapacity);
    if (capacity == 0 || capacity > SIZE_MAX / sizeof(FIndexSlot))
        return self;

    FIndexSlot *slots = findexAlloc(&self, capacity * sizeof(FIndexSlot));
    if (!slots)
        return self;
    memset(slots, 0, capacity * sizeof(FIndexSlot));
    self.slots = slots;
    self.capacity = capacity;
    return self;
}

fn FIndex_shutdown(FIndex *self) {
    if (!self)
        return;
    findexRelease(self, (*self).slots);
    (*self).slots = nullptr;
    (*self).capacity = 0;
    (*self).count = 0;
    (*self).used = 0;
    (*self).arena = nullptr;
}

bool FIndex_find(FIndex *self, u64 keyHash, u64 *outOffset, u64 *outLength) {
    if (!self || !FIndex_live(self))
        return false;

    bool found = false;
    bytes index = findexFindSlot(self, keyHash, &found);
    if (!found || index == SIZE_MAX)
        return false;

    const FIndexSlot *slot = &(*self).slots[index];
    if (outOffset)
        *outOffset = (*slot).offset;
    if (outLength)
        *outLength = (*slot).length;
    return true;
}

bool FIndex_insert(FIndex *self, u64 keyHash, u64 offset, u64 length) {
    if (!self)
        return false;

    if ((*self).capacity == 0 || (*self).used + 1 > ((*self).capacity * 7) / 10) {
        if (!findexGrow(self, (*self).capacity ? (*self).capacity * 2 : 8) &&
            (*self).used >= (*self).capacity)
            return false;
    }

    bool found = false;
    bytes index = findexFindSlot(self, keyHash, &found);
    if (index == SIZE_MAX)
        return false;

    FIndexSlot *slot = &(*self).slots[index];
    if (found) {
        (*slot).offset = offset;
        (*slot).length = length;
        return true;
    }

    if ((*slot).state == FINDEX_SLOT_EMPTY)
        (*self).used++;
    (*slot).keyHash = keyHash;
    (*slot).offset = offset;
    (*slot).length = length;
    (*slot).state = FINDEX_SLOT_OCCUPIED;
    (*self).count++;
    return true;
}

fn FIndex_remove(FIndex *self, u64 keyHash) {
    if (!self || !FIndex_live(self))
        return;

    bool found = false;
    bytes index = findexFindSlot(self, keyHash, &found);
    if (!found || index == SIZE_MAX)
        return;

    (*self).slots[index].state = FINDEX_SLOT_TOMBSTONE;
    if ((*self).count > 0)
        (*self).count--;
    if ((*self).count == 0) {
        memset((*self).slots, 0, (*self).capacity * sizeof(FIndexSlot));
        (*self).used = 0;
    }
}

u64 FIndex_count(const FIndex *self) {
    return self ? (*self).count : 0;
}

bool FIndex_live(const FIndex *self) {
    return self && (*self).slots && (*self).capacity > 0;
}
