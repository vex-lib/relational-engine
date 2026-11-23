#include "vex.h"
#include "io/file/file.h"
/** Production R2 I/O owner: Relational Engine; existing File ABI preserved. */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#if defined(VEX_PLATFORM_WINDOWS)
#include <direct.h>
#endif

#include "nio/mem.h"
#include "type/re_type.h"

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
 *     utf8 name[FILE_PATH_MAX]; // field/entry name
 *     FILE *handle; // OS file handle
 *     i64 size; // file cursor/size state
 *     i64 position; // file cursor/size state
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


/** file.c, File port (Legacy: io/File.java). stdio-backed file handle. */

/** Translate File mode flags to stdio fopen modes, preserving existing bytes unless truncation is requested. */
static FILE *map_open(string path, u32 mode) {
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

;;GETTER
static utf8 *File_nameMut(File *f) {
    return (*f).name;
}

;;SETTER
static fn File_setHandle(File *f, FILE *handle) {
    (*f).handle = handle;
}

;;SETTER
static fn File_setSize(File *f, i64 size) {
    (*f).size = size;
}

;;SETTER
static fn File_setPosition(File *f, i64 position) {
    (*f).position = position;
}

;;SETTER
static fn File_setMode(File *f, u32 mode) {
    (*f).mode = mode;
}

;;CONSTRUCTOR
/**
 * Opens a path using the requested mode flags and allocates its File in the engine arena.
 * @param path filesystem path to open
 * @param mode FILE_MODE_* bitmask
 * @return newly allocated File handle, or nullptr on failure
 */
File *File_open(string path, u32 mode) {
    if (!path) 
        return nullptr;

    if ((mode & FILE_MODE_CREATE) != 0) {
        string slash = strrchr(path, '/');

        if (slash && slash != path) {
            utf8 parent[FILE_PATH_MAX];

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

    File *f = Memory_alloc(TYPE_FILE_SINGLETON, Re_arena(), sizeof(File));

    if (!f) {
        fclose(handle);

        return nullptr;
    }

    utf8 *nameBuf = File_nameMut(f);

    strncpy(nameBuf, path, FILE_PATH_MAX - 1);

    nameBuf[FILE_PATH_MAX - 1] = '\0';

    File_setHandle(f, handle);

    File_setMode(f, mode);

    bool append = (mode & FILE_MODE_APPEND) != 0;

    fseek(handle, 0, SEEK_END);

    i64 sz = ftell(handle);

    if (sz < 0) 
        sz = 0;

    File_setSize(f, (i64)sz);

    if (append) {
        File_setPosition(f, (i64)sz);

        fseek(handle, 0, SEEK_END);
    } else {
        File_setPosition(f, 0);

        fseek(handle, 0, SEEK_SET);
    }

    return f;
}

;;DECONSTRUCT
/**
 * Closes the OS handle and releases the arena-owned File object.
 * @param f File instance to close
 * @return true when f was non-null and closed
 */
bool File_close(File *f) {
    if (!f) 
        return false;

    FILE *handle = File_handle(f);

    if (handle) 
        fclose(handle);

    File_setHandle(f, nullptr);

    Memory_free(f);

    return true;
}

/** Read up to max_len bytes at the current cursor, returning bytes read or -1 on error. */
i64 File_read(File *f, any dest, i64 max_len) {
    if (!f || !File_handle(f) || !dest) 
        return -1;

    if (max_len < 0) 
        return -1;

    if (max_len == 0) 
        return 0;

    FILE *handle = File_handle(f);

    bytes n = fread(dest, 1, (bytes) max_len, handle);

    if (n == 0 && ferror(handle)) 
        return -1;

    File_setPosition(f, File_pos(f) + (i64) n);

    return (i64) n;
}

/** Write up to len bytes at the current cursor and update tracked position and size. */
i64 File_write(File *f, const_any src, i64 len) {
    if (!f || !File_handle(f) || !src) 
        return -1;

    if (len < 0) 
        return -1;

    if (len == 0) 
        return 0;

    FILE *handle = File_handle(f);

    bytes n = fwrite(src, 1, (bytes) len, handle);

    if (n == 0 && ferror(handle)) 
        return -1;

    i64 pos = File_pos(f) + (i64) n;

    File_setPosition(f, pos);

    if (pos > File_size(f)) 
        File_setSize(f, pos);

    return (i64)n;
}

/** Seek to an absolute nonnegative byte offset and update the tracked cursor on success. */
bool File_seek(File *f, i64 position) {
    if (!f || !File_handle(f) || position < 0) 
        return false;

    if (fseek(File_handle(f), position, SEEK_SET) != 0) 
        return false;

    File_setPosition(f, position);

    return true;
}

/** Refresh the tracked file size from the open handle, returning -1 on failure. */
i64 File_refreshSize(File *f) {
    if (!f || !File_handle(f)) 
        return -1;

    FILE *handle = File_handle(f);

    fseek(handle, 0, SEEK_END);

    i64 sz = ftell(handle);

    if (sz < 0) 
        return -1;

    File_setSize(f, (i64)sz);

    return File_size(f);
}

/** Flush buffered output for an open file handle. */
bool File_flush(File *f) {
    if (!f || !File_handle(f)) 
        return false;

    return fflush(File_handle(f)) == 0;
}

/** Return the stdio handle borrowed from the File object. */
;;GETTER
FILE *File_handle(const File *f) {
    return (*f).handle;
}

/** Return the File object's stored path. */
;;GETTER
string File_name(const File *f) {
    return (*f).name;
}

/** Return the object's tracked file size. */
;;GETTER
i64 File_size(const File *f) {
    return (*f).size;
}

/** Return the object's tracked cursor position. */
;;GETTER
i64 File_pos(const File *f) {
    return (*f).position;
}

/** Return the mode flags supplied when the File was opened. */
;;GETTER
u32 File_mode(const File *f) {
    return (*f).mode;
}

/** Report whether the tracked cursor has reached or passed the tracked file size. */
bool File_eof(const File *f) {
    return File_pos(f) >= File_size(f);
}

/** Return whether stat succeeds for the supplied filesystem path. */
bool File_exists(string path) {
    if (!path) 
        return false;

    struct stat st;

    return stat(path, &st) == 0;
}

/** Return whether the path exists and identifies a directory. */
bool File_isDirectory(string path) {
    if (!path) 
        return false;

    struct stat st;

    if (stat(path, &st) != 0) 
        return false;

    return S_ISDIR(st.st_mode);
}

/**
 * Creates a single directory path component using the active platform API.
 * @param path NUL-terminated directory path to create
 * @return zero on success or non-zero on failure with errno set
 */
static i32 makeDirectory(string path)
{
#if defined(VEX_PLATFORM_WINDOWS)
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

/**
 * Create each missing path component with mode 0755, accepting existing directories.
 * @param path NUL-terminated directory path to create recursively
 * @return true when the target directory exists after the call
 */
bool File_mkdirs(string path)
{
    if(!path)
        return false;

    if(File_exists(path))
        return true;

    utf8 buf[FILE_PATH_MAX];

    bytes len = strlen(path);

    if(len >= FILE_PATH_MAX)
        return false;

    memcpy(buf, path, len + 1);

    for(utf8 *p = buf + 1; *p; p++) {
        bool isSep = (*p == '/');
#if defined(VEX_PLATFORM_WINDOWS)
        if(*p == '\\')
            isSep = true;
#endif
        if(isSep) {
            utf8 saved = *p;
            *p = '\0';

            if(makeDirectory(buf) != 0 && errno != EEXIST)
                return false;

            *p = saved;
        }
    }

    return makeDirectory(buf) == 0 || errno == EEXIST;
}

/** Remove a filesystem path using the C remove operation. */
bool File_delete(string path) {
    if (!path) 
        return false;

    return remove(path) == 0;
}

/**
 * ============================================================================
 * FilePtr, non-owning view into file bytes
 * ============================================================================
 */

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: FilePtr
 * ============================================================================
 * Non-owning view into file bytes: base pointer + offset + length. Borrows
 * its base from a mapped region owned elsewhere (FixedMappedFile,
 * GrowableMappedFile, or any mmap'd backing). Zero steady-state allocation;
 * passed and returned by value. Never freed by the borrower, the region's
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
 *   - FilePtr_data(FilePtr self)  to  u8 *
 *   - FilePtr_length(FilePtr self)  to  u64
 *   - FilePtr_offset(FilePtr self)  to  u64
 *   - FilePtr_isEmpty(FilePtr self)  to  bool
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

/**
 * ============================================================================
 * FIndex, key hash → (offset, length) file index
 * ============================================================================
 */

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
 *   - FIndex(initialCapacity)
 *   - FIndex(initialCapacity, arena)
 *
 * Core Functions:
 *   - FIndex_shutdown(FIndex *)
 *   - FIndex_find(FIndex *, u64, u64 *, u64 *)  to  bool
 *   - FIndex_insert(FIndex *, u64, u64, u64)  to  bool
 *   - FIndex_remove(FIndex *, u64)
 *   - FIndex_count(const FIndex *)  to  u64
 *   - FIndex_live(const FIndex *)  to  bool
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

;;GETTER
static FIndexSlot *FIndex_slots(const FIndex *self) {
    return (*self).slots;
}

;;GETTER
static bytes FIndex_capacity(const FIndex *self) {
    return (*self).capacity;
}

;;GETTER
static bytes FIndex_countRaw(const FIndex *self) {
    return (*self).count;
}

;;GETTER
static bytes FIndex_used(const FIndex *self) {
    return (*self).used;
}

;;GETTER
static MemoryArena *FIndex_arena(const FIndex *self) {
    return (*self).arena;
}

;;SETTER
static fn FIndex_setSlots(FIndex *self, FIndexSlot *slots) {
    (*self).slots = slots;
}

;;SETTER
static fn FIndex_setCapacity(FIndex *self, bytes capacity) {
    (*self).capacity = capacity;
}

;;SETTER
static fn FIndex_setCount(FIndex *self, bytes count) {
    (*self).count = count;
}

;;SETTER
static fn FIndex_setUsed(FIndex *self, bytes used) {
    (*self).used = used;
}

;;SETTER
static fn FIndex_setArena(FIndex *self, MemoryArena *arena) {
    (*self).arena = arena;
}

;;GETTER
static u64 FIndexSlot_keyHash(const FIndexSlot *slot) {
    return (*slot).keyHash;
}

;;GETTER
static u64 FIndexSlot_offset(const FIndexSlot *slot) {
    return (*slot).offset;
}

;;GETTER
static u64 FIndexSlot_length(const FIndexSlot *slot) {
    return (*slot).length;
}

;;GETTER
static u8 FIndexSlot_state(const FIndexSlot *slot) {
    return (*slot).state;
}

;;SETTER
static fn FIndexSlot_setKeyHash(FIndexSlot *slot, u64 keyHash) {
    (*slot).keyHash = keyHash;
}

;;SETTER
static fn FIndexSlot_setOffset(FIndexSlot *slot, u64 offset) {
    (*slot).offset = offset;
}

;;SETTER
static fn FIndexSlot_setLength(FIndexSlot *slot, u64 length) {
    (*slot).length = length;
}

;;SETTER
static fn FIndexSlot_setState(FIndexSlot *slot, u8 state) {
    (*slot).state = state;
}

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
    MemoryArena *arena = FIndex_arena(self);

    if (arena) 
        return MemoryArena_alloc(arena, TYPE_FINDEX_SINGLETON, bytesCount);

    return Memory_alloc(TYPE_FINDEX_SINGLETON, Re_arena(), bytesCount);
}

;;DECONSTRUCT
/**
 * Releases a slot table through the same arena owner that allocated it.
 * @param self owning FIndex instance
 * @param ptr  slot array pointer to release
 */
static fn findexRelease(FIndex *self, any ptr) {
    if (!ptr) 
        return;

    MemoryArena *arena = FIndex_arena(self);

    if (arena) 
        MemoryArena_free(arena, ptr);
    else 
        Memory_free(ptr);
}

/** Home slot for a key in a power-of-two table. */
static bytes findexProbe(u64 keyHash, bytes capacity) {
    return (bytes) keyHash & (capacity - 1);
}

/** Find a key, or the first reusable slot when it is absent. */
static bytes findexFindSlot(const FIndex *self, u64 keyHash, bool *outFound) {
    bytes capacity = FIndex_capacity(self);

    bytes mask = capacity - 1;

    bytes index = findexProbe(keyHash, capacity);

    bytes firstTombstone = SIZE_MAX;

    const FIndexSlot *slots = FIndex_slots(self);

    for (bytes probes = 0; probes < capacity; probes++) {
        const FIndexSlot *slot = &slots[index];

        u8 state = FIndexSlot_state(slot);

        if (state == FINDEX_SLOT_EMPTY) {
            *outFound = false;

            return firstTombstone != SIZE_MAX ? firstTombstone : index;
        }

        if (state == FINDEX_SLOT_TOMBSTONE) {
            if (firstTombstone == SIZE_MAX) 
                firstTombstone = index;
        } else if (FIndexSlot_keyHash(slot) == keyHash) {
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
    bytes oldCapacity = FIndex_capacity(self);

    bytes capacity = oldCapacity ? oldCapacity : 8;

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

    FIndexSlot *oldSlots = FIndex_slots(self);

    for (bytes i = 0; i < oldCapacity; i++) {
        FIndexSlot *old = &oldSlots[i];

        if (FIndexSlot_state(old) != FINDEX_SLOT_OCCUPIED) 
            continue;

        bytes index = findexProbe(FIndexSlot_keyHash(old), capacity);

        while (slots[index].state == FINDEX_SLOT_OCCUPIED) 
            index = (index + 1) & mask;

        slots[index] = (*old);
    }

    findexRelease(self, oldSlots);

    FIndex_setSlots(self, slots);

    FIndex_setCapacity(self, capacity);

    FIndex_setUsed(self, FIndex_countRaw(self));

    return true;
}

;;CONSTRUCTOR
/**
 * Constructs an FIndex backed by the default Relational Engine arena.
 * @param initialCapacity minimum initial slot count (0 creates an empty index)
 * @return initialized FIndex value
 */
FIndex FIndex_1(bytes initialCapacity) {
    return FIndex_2(initialCapacity, nullptr);
}

;;CONSTRUCTOR
/**
 * Constructs an FIndex backed by the specified arena (or Re_arena() when nullptr).
 * @param initialCapacity minimum initial slot count (0 creates an empty index)
 * @param arena           backing MemoryArena, or nullptr for Re_arena()
 * @return initialized FIndex value
 */
FIndex FIndex_2(bytes initialCapacity, MemoryArena *arena) {
    FIndex self;
    memset(&self, 0, sizeof(self));
    self.arena = arena;

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

    findexRelease(self, FIndex_slots(self));

    FIndex_setSlots(self, nullptr);

    FIndex_setCapacity(self, 0);

    FIndex_setCount(self, 0);

    FIndex_setUsed(self, 0);

    FIndex_setArena(self, nullptr);
}

bool FIndex_find(FIndex *self, u64 keyHash, u64 *outOffset, u64 *outLength) {
    if (!self || !FIndex_live(self)) 
        return false;

    bool found = false;

    bytes index = findexFindSlot(self, keyHash, &found);

    if (!found || index == SIZE_MAX) 
        return false;

    const FIndexSlot *slot = &FIndex_slots(self)[index];

    if (outOffset) 
        *outOffset = FIndexSlot_offset(slot);

    if (outLength) 
        *outLength = FIndexSlot_length(slot);

    return true;
}

bool FIndex_insert(FIndex *self, u64 keyHash, u64 offset, u64 length) {
    if (!self) 
        return false;

    bytes capacity = FIndex_capacity(self);

    if (capacity == 0 || FIndex_used(self) + 1 > (capacity * 7) / 10) {
        if (!findexGrow(self, capacity ? capacity * 2 : 8) &&
            FIndex_used(self) >= FIndex_capacity(self))
            return false;
    }

    bool found = false;

    bytes index = findexFindSlot(self, keyHash, &found);

    if (index == SIZE_MAX) 
        return false;

    FIndexSlot *slot = &FIndex_slots(self)[index];

    if (found) {
        FIndexSlot_setOffset(slot, offset);

        FIndexSlot_setLength(slot, length);

        return true;
    }

    if (FIndexSlot_state(slot) == FINDEX_SLOT_EMPTY) 
        FIndex_setUsed(self, FIndex_used(self) + 1);

    FIndexSlot_setKeyHash(slot, keyHash);

    FIndexSlot_setOffset(slot, offset);

    FIndexSlot_setLength(slot, length);

    FIndexSlot_setState(slot, FINDEX_SLOT_OCCUPIED);

    FIndex_setCount(self, FIndex_countRaw(self) + 1);

    return true;
}

fn FIndex_remove(FIndex *self, u64 keyHash) {
    if (!self || !FIndex_live(self)) 
        return;

    bool found = false;

    bytes index = findexFindSlot(self, keyHash, &found);

    if (!found || index == SIZE_MAX) 
        return;

    FIndexSlot_setState(&FIndex_slots(self)[index], FINDEX_SLOT_TOMBSTONE);

    bytes count = FIndex_countRaw(self);

    if (count > 0) {
        count--;

        FIndex_setCount(self, count);
    }

    if (count == 0) {
        memset(FIndex_slots(self), 0, FIndex_capacity(self) * sizeof(FIndexSlot));

        FIndex_setUsed(self, 0);
    }
}

u64 FIndex_count(const FIndex *self) {
    return self ? FIndex_countRaw(self) : 0;
}

bool FIndex_live(const FIndex *self) {
    return self && FIndex_slots(self) && FIndex_capacity(self) > 0;
}
