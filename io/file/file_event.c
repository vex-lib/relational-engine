#include "vex.h"
#include "io/file/file.h"

#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "nio/mem.h"
#include "type/re_type.h"
#include "lang/platform.h"
#include "debug/throw.h"

#if defined(VEX_PLATFORM_APPLE)
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#endif

;;DEFINITION
#define TYPE_FILE_EVENT_WATCHER Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 2u)
/**
 * ============================================================================
 * DEFINITION: FileEventWatcher
 * ============================================================================
 * Cross-platform file event watcher with configurable watch properties
 * (latency, debounce, recursion, directory/hidden/pattern/extension filters)
 * and rich event metadata (relative path, monotonic timestamp, wall-clock
 * epoch seconds, file size). macOS uses FSEvents delivered on a GCD queue and
 * drained on the caller's thread via pump().
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: FileEventWatcher (io/file/file_event.c)
 * ============================================================================
 * Owns one directory watch scope, an isolated MemoryArena for filter lists and
 * coalesced event queue buffers, a mutex-guarded queue, and a caller-thread
 * pump. Memory_alloc is confined strictly to FileEventWatcher_init.
 * ============================================================================
 */

typedef struct QueuedEvent {
    char *path;
    FileEventType type;
    bool isDir;
    u64 timestampNs;
    i64 epochSeconds;
    i64 fileSize;
} QueuedEvent;

struct FileEventWatcher {
    MemoryArena *arena;
    char *path;
    bytes pathLen;
    FileEventType mask;
    FileEventCallback legacyCallback;
    FileWatchCallback watchCallback;
    any userdata;

    u32 latencyMs;
    u32 debounceMs;
    bool recursive;
    bool emitDirs;
    bool ignoreHidden;
    char **ignorePatterns;
    bytes ignoreCount;
    char **includeExtensions;
    bytes includeCount;

    pthread_mutex_t mutex;
    QueuedEvent *queue;
    bytes queueCount;
    bytes queueCapacity;

#if defined(VEX_PLATFORM_APPLE)
    FSEventStreamRef stream;
    dispatch_queue_t dispatchQueue;
#endif

    bool running;
};

;;GETTER
static inline MemoryArena *FileEventWatcher_arena(const FileEventWatcher *w) { return (*w).arena; }
;;SETTER
static inline fn FileEventWatcher_setArena(FileEventWatcher *w, MemoryArena *a) { (*w).arena = a; }
;;GETTER
static inline char *FileEventWatcher_pathRaw(const FileEventWatcher *w) { return (*w).path; }
;;SETTER
static inline fn FileEventWatcher_setPath(FileEventWatcher *w, char *p) { (*w).path = p; }
;;GETTER
static inline bytes FileEventWatcher_pathLen(const FileEventWatcher *w) { return (*w).pathLen; }
;;SETTER
static inline fn FileEventWatcher_setPathLen(FileEventWatcher *w, bytes n) { (*w).pathLen = n; }
;;GETTER
static inline FileEventType FileEventWatcher_maskRaw(const FileEventWatcher *w) { return (*w).mask; }
;;SETTER
static inline fn FileEventWatcher_setMask(FileEventWatcher *w, FileEventType m) { (*w).mask = m; }
;;GETTER
static inline FileEventCallback FileEventWatcher_legacyCb(const FileEventWatcher *w) { return (*w).legacyCallback; }
;;SETTER
static inline fn FileEventWatcher_setLegacyCb(FileEventWatcher *w, FileEventCallback cb) { (*w).legacyCallback = cb; }
;;GETTER
static inline FileWatchCallback FileEventWatcher_watchCb(const FileEventWatcher *w) { return (*w).watchCallback; }
;;SETTER
static inline fn FileEventWatcher_setWatchCb(FileEventWatcher *w, FileWatchCallback cb) { (*w).watchCallback = cb; }
;;GETTER
static inline any FileEventWatcher_userdata(const FileEventWatcher *w) { return (*w).userdata; }
;;SETTER
static inline fn FileEventWatcher_setUserdata(FileEventWatcher *w, any ud) { (*w).userdata = ud; }
;;GETTER
static inline u32 FileEventWatcher_latencyMs(const FileEventWatcher *w) { return (*w).latencyMs; }
;;SETTER
static inline fn FileEventWatcher_setLatencyMs(FileEventWatcher *w, u32 v) { (*w).latencyMs = v; }
;;GETTER
static inline u32 FileEventWatcher_debounceMs(const FileEventWatcher *w) { return (*w).debounceMs; }
;;SETTER
static inline fn FileEventWatcher_setDebounceMs(FileEventWatcher *w, u32 v) { (*w).debounceMs = v; }
;;GETTER
static inline bool FileEventWatcher_recursive(const FileEventWatcher *w) { return (*w).recursive; }
;;SETTER
static inline fn FileEventWatcher_setRecursive(FileEventWatcher *w, bool v) { (*w).recursive = v; }
;;GETTER
static inline bool FileEventWatcher_emitDirs(const FileEventWatcher *w) { return (*w).emitDirs; }
;;SETTER
static inline fn FileEventWatcher_setEmitDirs(FileEventWatcher *w, bool v) { (*w).emitDirs = v; }
;;GETTER
static inline bool FileEventWatcher_ignoreHidden(const FileEventWatcher *w) { return (*w).ignoreHidden; }
;;SETTER
static inline fn FileEventWatcher_setIgnoreHidden(FileEventWatcher *w, bool v) { (*w).ignoreHidden = v; }
;;GETTER
static inline char **FileEventWatcher_ignorePatterns(const FileEventWatcher *w) { return (*w).ignorePatterns; }
;;SETTER
static inline fn FileEventWatcher_setIgnorePatterns(FileEventWatcher *w, char **p) { (*w).ignorePatterns = p; }
;;GETTER
static inline bytes FileEventWatcher_ignoreCount(const FileEventWatcher *w) { return (*w).ignoreCount; }
;;SETTER
static inline fn FileEventWatcher_setIgnoreCount(FileEventWatcher *w, bytes n) { (*w).ignoreCount = n; }
;;GETTER
static inline char **FileEventWatcher_includeExts(const FileEventWatcher *w) { return (*w).includeExtensions; }
;;SETTER
static inline fn FileEventWatcher_setIncludeExts(FileEventWatcher *w, char **p) { (*w).includeExtensions = p; }
;;GETTER
static inline bytes FileEventWatcher_includeCount(const FileEventWatcher *w) { return (*w).includeCount; }
;;SETTER
static inline fn FileEventWatcher_setIncludeCount(FileEventWatcher *w, bytes n) { (*w).includeCount = n; }
;;GETTER
static inline pthread_mutex_t *FileEventWatcher_mutexMut(FileEventWatcher *w) { return &(*w).mutex; }
;;GETTER
static inline QueuedEvent *FileEventWatcher_queue(const FileEventWatcher *w) { return (*w).queue; }
;;SETTER
static inline fn FileEventWatcher_setQueue(FileEventWatcher *w, QueuedEvent *q) { (*w).queue = q; }
;;GETTER
static inline bytes FileEventWatcher_queueCount(const FileEventWatcher *w) { return (*w).queueCount; }
;;SETTER
static inline fn FileEventWatcher_setQueueCount(FileEventWatcher *w, bytes c) { (*w).queueCount = c; }
;;GETTER
static inline bytes FileEventWatcher_queueCapacity(const FileEventWatcher *w) { return (*w).queueCapacity; }
;;SETTER
static inline fn FileEventWatcher_setQueueCapacity(FileEventWatcher *w, bytes c) { (*w).queueCapacity = c; }
;;GETTER
static inline bool FileEventWatcher_runningRaw(const FileEventWatcher *w) { return (*w).running; }
;;SETTER
static inline fn FileEventWatcher_setRunning(FileEventWatcher *w, bool r) { (*w).running = r; }
#if defined(VEX_PLATFORM_APPLE)
;;GETTER
static inline FSEventStreamRef FileEventWatcher_stream(const FileEventWatcher *w) { return (*w).stream; }
;;SETTER
static inline fn FileEventWatcher_setStream(FileEventWatcher *w, FSEventStreamRef s) { (*w).stream = s; }
;;GETTER
static inline dispatch_queue_t FileEventWatcher_dispatchQueue(const FileEventWatcher *w) { return (*w).dispatchQueue; }
;;SETTER
static inline fn FileEventWatcher_setDispatchQueue(FileEventWatcher *w, dispatch_queue_t q) { (*w).dispatchQueue = q; }
#endif

;;GETTER
static inline u32 Props_latencyMs(const FileWatchProps *p) { return (*p).latencyMs; }
;;GETTER
static inline u32 Props_debounceMs(const FileWatchProps *p) { return (*p).debounceMs; }
;;GETTER
static inline bool Props_recursive(const FileWatchProps *p) { return (*p).recursive; }
;;GETTER
static inline bool Props_emitDirs(const FileWatchProps *p) { return (*p).emitDirs; }
;;GETTER
static inline bool Props_ignoreHidden(const FileWatchProps *p) { return (*p).ignoreHidden; }
;;GETTER
static inline const char *const *Props_ignorePatterns(const FileWatchProps *p) { return (*p).ignorePatterns; }
;;GETTER
static inline bytes Props_ignoreCount(const FileWatchProps *p) { return (*p).ignoreCount; }
;;GETTER
static inline const char *const *Props_includeExts(const FileWatchProps *p) { return (*p).includeExtensions; }
;;GETTER
static inline bytes Props_includeCount(const FileWatchProps *p) { return (*p).includeCount; }

/** Reads monotonic nanoseconds for event timestamps and debounce windows. */
static u64 watchNowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64) ts.tv_sec * 1000000000ULL + (u64) ts.tv_nsec;
}

/** Reads current file byte size via stat, or -1 if absent or unreadable. */
static i64 watchFileSize(const char *path) {
    if (!path)
        return -1;
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (i64) st.st_size;
}

/** Compares two NUL-terminated strings for exact byte equality. */
static bool watchStrEq(const char *a, const char *b) {
    if (!a || !b)
        return false;
    bytes la = strlen(a);
    bytes lb = strlen(b);
    if (la != lb)
        return false;
    return la == 0u || memcmp(a, b, la) == 0;
}

/** Returns true if str begins with the first prefixLen bytes of prefix. */
static bool watchStartsWith(const char *str, const char *prefix, bytes prefixLen) {
    if (!str || !prefix)
        return false;
    if (strlen(str) < prefixLen)
        return false;
    return prefixLen == 0u || memcmp(str, prefix, prefixLen) == 0;
}

/** Scans str for the first occurrence of target byte, or returns nullptr. */
static const char *watchFindChar(const char *str, char target) {
    if (!str)
        return nullptr;
    for (const char *p = str; *p != '\0'; p++)
        if (*p == target)
            return p;
    return nullptr;
}

/** Returns true if needle occurs as a contiguous substring inside haystack. */
static bool watchContains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0])
        return false;
    bytes hlen = strlen(haystack);
    bytes nlen = strlen(needle);
    if (nlen > hlen)
        return false;
    for (bytes i = 0; i <= hlen - nlen; i++)
        if (memcmp(haystack + i, needle, nlen) == 0)
            return true;
    return false;
}

/** Allocates an owned copy of a NUL-terminated string inside the watcher's arena. */
static char *watchDupStr(MemoryArena *arena, const char *src) {
    if (!arena || !src)
        return nullptr;
    bytes len = strlen(src);
    char *copy = (char*) MemoryArena_alloc(arena, TYPE_FILE_EVENT_WATCHER, len + 1u);
    if (!copy)
        return nullptr;
    memcpy(copy, src, len + 1u);
    return copy;
}

/** Releases an owned string list allocated inside the watcher's arena. */
static fn watchFreeStrList(MemoryArena *arena, char **list, bytes count) {
    if (!arena || !list)
        return;
    for (bytes i = 0; i < count; i++)
        MemoryArena_free(arena, list[i]);
    MemoryArena_free(arena, list);
}

/** Deep-copies a caller string list into the watcher's arena; returns false on OOM. */
static bool watchCopyStrList(MemoryArena *arena, const char *const *src, bytes count,
                             char ***outList, bytes *outCount) {
    *outList = nullptr;
    *outCount = 0;
    if (!src || count == 0)
        return true;
    char **list = (char**) MemoryArena_alloc(arena, TYPE_FILE_EVENT_WATCHER, count * sizeof(char*));
    if (!list)
        return false;
    bytes kept = 0;
    for (bytes i = 0; i < count; i++) {
        if (!src[i] || !src[i][0])
            continue;
        list[kept] = watchDupStr(arena, src[i]);
        if (!list[kept]) {
            watchFreeStrList(arena, list, kept);
            return false;
        }
        kept++;
    }
    if (kept == 0) {
        MemoryArena_free(arena, list);
        return true;
    }
    *outList = list;
    *outCount = kept;
    return true;
}

/** Returns the relative slice of path under the watched root, or path itself if outside. */
static const char *watchRelativePath(const FileEventWatcher *watcher, const char *path) {
    const char *root = FileEventWatcher_pathRaw(watcher);
    bytes rootLen = FileEventWatcher_pathLen(watcher);
    if (!root || rootLen == 0 || !path)
        return path ? path : "";
    if (!watchStartsWith(path, root, rootLen))
        return path;
    const char *rest = path + rootLen;
    while (*rest == '/')
        rest++;
    return *rest ? rest : ".";
}

/** Returns true if any path component in rel begins with a dot. */
static bool watchHasHiddenSegment(const char *rel) {
    const char *p = rel;
    while (*p) {
        if (*p == '.' && !(p[1] == '\0' || p[1] == '/'))
            return true;
        const char *slash = watchFindChar(p, '/');
        if (!slash)
            break;
        p = slash + 1;
    }
    return false;
}

/** Returns true if str ends with suffix. */
static bool watchEndsWith(const char *str, const char *suffix) {
    bytes slen = strlen(str);
    bytes plen = strlen(suffix);
    if (plen > slen)
        return false;
    return memcmp(str + slen - plen, suffix, plen) == 0;
}

/** Returns true if pattern matches a segment, suffix, or substring of rel. */
static bool watchPatternMatches(const char *rel, const char *pattern) {
    if (!rel || !pattern || !pattern[0])
        return false;
    if (pattern[0] == '*' && pattern[1] != '\0')
        return watchEndsWith(rel, pattern + 1);
    return watchContains(rel, pattern);
}

/** Evaluates watch properties against a candidate path and directory flag. */
static bool watchMatchesLocked(const FileEventWatcher *watcher, const char *path, bool isDir) {
    if (!watcher || !path || !path[0])
        return false;
    if (isDir && !FileEventWatcher_emitDirs(watcher))
        return false;
    const char *rel = watchRelativePath(watcher, path);
    if (!FileEventWatcher_recursive(watcher) && watchFindChar(rel, '/') != nullptr)
        return false;
    if (FileEventWatcher_ignoreHidden(watcher) && watchHasHiddenSegment(rel))
        return false;
    char **ignores = FileEventWatcher_ignorePatterns(watcher);
    bytes ignoreCount = FileEventWatcher_ignoreCount(watcher);
    for (bytes i = 0; i < ignoreCount; i++)
        if (watchPatternMatches(rel, ignores[i]))
            return false;
    char **includes = FileEventWatcher_includeExts(watcher);
    bytes includeCount = FileEventWatcher_includeCount(watcher);
    if (includeCount > 0 && !isDir) {
        bool matched = false;
        for (bytes i = 0; i < includeCount; i++)
            if (watchEndsWith(rel, includes[i])) {
                matched = true;
                break;
            }
        if (!matched)
            return false;
    }
    return true;
}

/** Enqueues or coalesces a filtered file event under the watcher mutex. */
static bool watchEnqueue(FileEventWatcher *watcher, const char *path, FileEventType type, bool isDir) {
    if (!watcher || !path)
        return false;
    if (!(type & FileEventWatcher_maskRaw(watcher)))
        return false;

    pthread_mutex_t *mtx = FileEventWatcher_mutexMut(watcher);
    pthread_mutex_lock(mtx);

    if (!watchMatchesLocked(watcher, path, isDir)) {
        pthread_mutex_unlock(mtx);
        return false;
    }

    MemoryArena *arena = FileEventWatcher_arena(watcher);
    u64 nowNs = watchNowNs();
    i64 epochSec = (i64) time(nullptr);
    i64 sizeBytes = (type & FILE_EVENT_DELETED) ? -1 : watchFileSize(path);
    u64 debounceNs = (u64) FileEventWatcher_debounceMs(watcher) * 1000000ULL;

    bytes qcount = FileEventWatcher_queueCount(watcher);
    QueuedEvent *queue = FileEventWatcher_queue(watcher);
    if (debounceNs > 0) {
        for (bytes i = qcount; i > 0; i--) {
            bytes idx = i - 1u;
            if (queue[idx].path && watchStrEq(queue[idx].path, path) &&
                nowNs >= queue[idx].timestampNs && (nowNs - queue[idx].timestampNs) <= debounceNs) {
                queue[idx].type |= type;
                queue[idx].isDir = isDir;
                queue[idx].timestampNs = nowNs;
                queue[idx].epochSeconds = epochSec;
                queue[idx].fileSize = sizeBytes;
                pthread_mutex_unlock(mtx);
                return true;
            }
        }
    }

    bytes qcap = FileEventWatcher_queueCapacity(watcher);
    if (qcount == qcap) {
        bytes newCap = qcap ? qcap * 2u : 16u;
        QueuedEvent *grown = queue
            ? (QueuedEvent*) MemoryArena_realloc(arena, queue, newCap * sizeof(QueuedEvent))
            : (QueuedEvent*) MemoryArena_alloc(arena, TYPE_FILE_EVENT_WATCHER, newCap * sizeof(QueuedEvent));
        if (!grown) {
            pthread_mutex_unlock(mtx);
            return false;
        }
        queue = grown;
        FileEventWatcher_setQueue(watcher, queue);
        FileEventWatcher_setQueueCapacity(watcher, newCap);
    }

    char *copy = watchDupStr(arena, path);
    if (!copy) {
        pthread_mutex_unlock(mtx);
        return false;
    }

    queue[qcount].path = copy;
    queue[qcount].type = type;
    queue[qcount].isDir = isDir;
    queue[qcount].timestampNs = nowNs;
    queue[qcount].epochSeconds = epochSec;
    queue[qcount].fileSize = sizeBytes;
    FileEventWatcher_setQueueCount(watcher, qcount + 1u);

    pthread_mutex_unlock(mtx);
    return true;
}

#if defined(VEX_PLATFORM_APPLE)
/** Translates macOS FSEvents flags into FileEventType and enqueues filtered records. */
static fn fsCallback(ConstFSEventStreamRef streamRef, any clientCallBackInfo,
                     bytes numEvents, any eventPaths,
                     const FSEventStreamEventFlags eventFlags[],
                     const FSEventStreamEventId eventIds[]) {
    (discard) streamRef;
    (discard) eventIds;
    FileEventWatcher *watcher = (FileEventWatcher*) clientCallBackInfo;
    char **paths = (char**) eventPaths;

    for (bytes i = 0; i < numEvents; i++) {
        FileEventType type = FILE_EVENT_NONE;
        FSEventStreamEventFlags flags = eventFlags[i];
        bool isDir = (flags & kFSEventStreamEventFlagItemIsDir) != 0;

        if (flags & kFSEventStreamEventFlagItemCreated)
            type |= FILE_EVENT_CREATED;
        if (flags & kFSEventStreamEventFlagItemModified)
            type |= FILE_EVENT_MODIFIED;
        if (flags & kFSEventStreamEventFlagItemRemoved)
            type |= FILE_EVENT_DELETED;
        if (flags & kFSEventStreamEventFlagItemRenamed)
            type |= FILE_EVENT_RENAMED;
        if (type == FILE_EVENT_NONE)
            type = FILE_EVENT_MODIFIED;

        (discard) watchEnqueue(watcher, paths[i], type, isDir);
    }
}
#endif

/** Returns default watch properties suitable for source-tree watching. */
FileWatchProps FileWatchProps_defaults() {
    FileWatchProps p;
    memset(&p, 0, sizeof(p));
    p.latencyMs = 100u;
    p.debounceMs = 50u;
    p.recursive = true;
    p.emitDirs = false;
    p.ignoreHidden = true;
    return p;
}

/** Clamps and applies watch properties under the watcher mutex. */
static bool watchApplyProps(FileEventWatcher *watcher, const FileWatchProps *props) {
    FileWatchProps eff = props ? *props : FileWatchProps_defaults();
    u32 latency = Props_latencyMs(&eff);
    if (latency == 0u)
        latency = 100u;
    if (latency < 10u)
        latency = 10u;
    if (latency > 5000u)
        latency = 5000u;

    MemoryArena *arena = FileEventWatcher_arena(watcher);
    char **newIgnores = nullptr;
    bytes newIgnoreCount = 0;
    if (!watchCopyStrList(arena, Props_ignorePatterns(&eff), Props_ignoreCount(&eff), &newIgnores, &newIgnoreCount))
        return false;

    char **newIncludes = nullptr;
    bytes newIncludeCount = 0;
    if (!watchCopyStrList(arena, Props_includeExts(&eff), Props_includeCount(&eff), &newIncludes, &newIncludeCount)) {
        watchFreeStrList(arena, newIgnores, newIgnoreCount);
        return false;
    }

    pthread_mutex_t *mtx = FileEventWatcher_mutexMut(watcher);
    pthread_mutex_lock(mtx);
    watchFreeStrList(arena, FileEventWatcher_ignorePatterns(watcher), FileEventWatcher_ignoreCount(watcher));
    watchFreeStrList(arena, FileEventWatcher_includeExts(watcher), FileEventWatcher_includeCount(watcher));
    FileEventWatcher_setLatencyMs(watcher, latency);
    FileEventWatcher_setDebounceMs(watcher, Props_debounceMs(&eff));
    FileEventWatcher_setRecursive(watcher, Props_recursive(&eff));
    FileEventWatcher_setEmitDirs(watcher, Props_emitDirs(&eff));
    FileEventWatcher_setIgnoreHidden(watcher, Props_ignoreHidden(&eff));
    FileEventWatcher_setIgnorePatterns(watcher, newIgnores);
    FileEventWatcher_setIgnoreCount(watcher, newIgnoreCount);
    FileEventWatcher_setIncludeExts(watcher, newIncludes);
    FileEventWatcher_setIncludeCount(watcher, newIncludeCount);
    pthread_mutex_unlock(mtx);
    return true;
}

/** Constructor allocating a FileEventWatcher and its isolated arena. */
static FileEventWatcher *FileEventWatcher_init(const char *path, FileEventType mask,
                                               const FileWatchProps *props,
                                               FileEventCallback legacyCb,
                                               FileWatchCallback watchCb,
                                               any userdata) {
    if (!path || !path[0] || (!legacyCb && !watchCb))
        return nullptr;

    FileEventWatcher *watcher = (FileEventWatcher*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, sizeof(FileEventWatcher));
    if (!watcher)
        return nullptr;
    memset(watcher, 0, sizeof(FileEventWatcher));

    MemoryArena *arena = MemoryArena_create(0);
    if (!arena) {
        Memory_free(watcher);
        return nullptr;
    }
    FileEventWatcher_setArena(watcher, arena);

    char *wpath = watchDupStr(arena, path);
    if (!wpath) {
        MemoryArena_destroy(arena);
        Memory_free(watcher);
        return nullptr;
    }
    bytes len = strlen(wpath);
    while (len > 1u && wpath[len - 1u] == '/')
        wpath[--len] = '\0';

    FileEventWatcher_setPath(watcher, wpath);
    FileEventWatcher_setPathLen(watcher, len);
    FileEventWatcher_setMask(watcher, mask ? mask : FILE_EVENT_ALL);
    FileEventWatcher_setLegacyCb(watcher, legacyCb);
    FileEventWatcher_setWatchCb(watcher, watchCb);
    FileEventWatcher_setUserdata(watcher, userdata);
    pthread_mutex_init(FileEventWatcher_mutexMut(watcher), nullptr);
    FileEventWatcher_setRunning(watcher, false);

    if (!watchApplyProps(watcher, props)) {
        pthread_mutex_destroy(FileEventWatcher_mutexMut(watcher));
        MemoryArena_destroy(arena);
        Memory_free(watcher);
        return nullptr;
    }
    return watcher;
}

/** Creates a legacy path+type file watcher with default watch properties. */
FileEventWatcher *FileEventWatcher_create(const char *path, FileEventType mask,
                                          FileEventCallback callback, any userdata) {
    FileWatchProps defaults = FileWatchProps_defaults();
    defaults.emitDirs = true;
    defaults.ignoreHidden = false;
    return FileEventWatcher_init(path, mask, &defaults, callback, nullptr, userdata);
}

/** Creates a rich-metadata file watcher configured with explicit FileWatchProps. */
FileEventWatcher *FileEventWatcher_createWithProps(const char *path, FileEventType mask,
                                                   const FileWatchProps *props,
                                                   FileWatchCallback callback, any userdata) {
    return FileEventWatcher_init(path, mask, props, nullptr, callback, userdata);
}

/** Replaces the watcher's filter and debounce properties; latency takes effect on next start. */
bool FileEventWatcher_setProps(FileEventWatcher *watcher, const FileWatchProps *props) {
    if (!watcher)
        return false;
    return watchApplyProps(watcher, props);
}

/** Returns a snapshot of the watcher's scalar watch properties and filter counts. */
FileWatchProps FileEventWatcher_getProps(const FileEventWatcher *watcher) {
    FileWatchProps out;
    memset(&out, 0, sizeof(out));
    if (!watcher)
        return out;
    out.latencyMs = FileEventWatcher_latencyMs(watcher);
    out.debounceMs = FileEventWatcher_debounceMs(watcher);
    out.recursive = FileEventWatcher_recursive(watcher);
    out.emitDirs = FileEventWatcher_emitDirs(watcher);
    out.ignoreHidden = FileEventWatcher_ignoreHidden(watcher);
    out.ignorePatterns = (const char *const *) FileEventWatcher_ignorePatterns(watcher);
    out.ignoreCount = FileEventWatcher_ignoreCount(watcher);
    out.includeExtensions = (const char *const *) FileEventWatcher_includeExts(watcher);
    out.includeCount = FileEventWatcher_includeCount(watcher);
    return out;
}

/** Returns the normalized watched root path, or nullptr if watcher is null. */
const char *FileEventWatcher_getPath(const FileEventWatcher *watcher) {
    return watcher ? FileEventWatcher_pathRaw(watcher) : nullptr;
}

/** Returns the active FileEventType bitmask, or FILE_EVENT_NONE if watcher is null. */
FileEventType FileEventWatcher_getMask(const FileEventWatcher *watcher) {
    return watcher ? FileEventWatcher_maskRaw(watcher) : FILE_EVENT_NONE;
}

/** Reports whether the underlying OS watch stream is currently running. */
bool FileEventWatcher_isRunning(const FileEventWatcher *watcher) {
    return watcher ? FileEventWatcher_runningRaw(watcher) : false;
}

/** Returns the number of queued events awaiting FileEventWatcher_pump. */
bytes FileEventWatcher_pendingCount(const FileEventWatcher *watcher) {
    if (!watcher)
        return 0;
    pthread_mutex_t *mtx = FileEventWatcher_mutexMut((FileEventWatcher*) watcher);
    pthread_mutex_lock(mtx);
    bytes count = FileEventWatcher_queueCount(watcher);
    pthread_mutex_unlock(mtx);
    return count;
}

/** Tests whether a candidate path passes the watcher's active filter properties. */
bool FileEventWatcher_matchesPath(const FileEventWatcher *watcher, const char *path, bool isDir) {
    if (!watcher)
        return false;
    pthread_mutex_t *mtx = FileEventWatcher_mutexMut((FileEventWatcher*) watcher);
    pthread_mutex_lock(mtx);
    bool ok = watchMatchesLocked(watcher, path, isDir);
    pthread_mutex_unlock(mtx);
    return ok;
}

/** Enqueues a synthetic event through the same filter, debounce, and metadata pipeline. */
bool FileEventWatcher_pushSynthetic(FileEventWatcher *watcher, const char *path,
                                    FileEventType type, bool isDir) {
    return watchEnqueue(watcher, path, type, isDir);
}

/** Starts the platform file-event stream using the configured latencyMs property. */
bool FileEventWatcher_start(FileEventWatcher *watcher) {
    if (!watcher || FileEventWatcher_runningRaw(watcher))
        return false;

#if defined(VEX_PLATFORM_APPLE)
    CFStringRef cfPath = CFStringCreateWithCString(nullptr, FileEventWatcher_pathRaw(watcher), kCFStringEncodingUTF8);
    if (!cfPath)
        return false;
    CFArrayRef pathsToWatch = CFArrayCreate(nullptr, (const_any*) &cfPath, 1, &kCFTypeArrayCallBacks);
    CFRelease(cfPath);
    if (!pathsToWatch)
        return false;

    FSEventStreamContext context = { 0, watcher, nullptr, nullptr, nullptr };
    CFTimeInterval latency = (CFTimeInterval) FileEventWatcher_latencyMs(watcher) / 1000.0;
    FSEventStreamRef stream = FSEventStreamCreate(nullptr, fsCallback, &context,
                                                  pathsToWatch,
                                                  kFSEventStreamEventIdSinceNow,
                                                  latency,
                                                  kFSEventStreamCreateFlagFileEvents);
    CFRelease(pathsToWatch);
    FileEventWatcher_setStream(watcher, stream);
    if (!stream)
        return false;

    dispatch_queue_t dq = dispatch_queue_create("vex.file_event", DISPATCH_QUEUE_SERIAL);
    FileEventWatcher_setDispatchQueue(watcher, dq);
    FSEventStreamSetDispatchQueue(stream, dq);

    if (!FSEventStreamStart(stream)) {
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        FileEventWatcher_setStream(watcher, nullptr);
        if (dq) {
            dispatch_release(dq);
            FileEventWatcher_setDispatchQueue(watcher, nullptr);
        }
        return false;
    }

    FileEventWatcher_setRunning(watcher, true);
    return true;
#else
    THROW("FileEventWatcher: platform not supported");
    return false;
#endif
}

/** Stops and releases the platform file-event stream. */
fn FileEventWatcher_stop(FileEventWatcher *watcher) {
    if (!watcher || !FileEventWatcher_runningRaw(watcher))
        return;
#if defined(VEX_PLATFORM_APPLE)
    FSEventStreamRef stream = FileEventWatcher_stream(watcher);
    if (stream) {
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        FileEventWatcher_setStream(watcher, nullptr);
    }
    dispatch_queue_t dq = FileEventWatcher_dispatchQueue(watcher);
    if (dq) {
        dispatch_release(dq);
        FileEventWatcher_setDispatchQueue(watcher, nullptr);
    }
#endif
    FileEventWatcher_setRunning(watcher, false);
}

/** Drains all queued events on the caller's thread and invokes the registered callback. */
fn FileEventWatcher_pump(FileEventWatcher *watcher) {
    if (!watcher)
        return;

    pthread_mutex_t *mtx = FileEventWatcher_mutexMut(watcher);
    pthread_mutex_lock(mtx);
    bytes count = FileEventWatcher_queueCount(watcher);
    QueuedEvent *events = FileEventWatcher_queue(watcher);
    FileEventWatcher_setQueue(watcher, nullptr);
    FileEventWatcher_setQueueCount(watcher, 0);
    FileEventWatcher_setQueueCapacity(watcher, 0);
    pthread_mutex_unlock(mtx);

    MemoryArena *arena = FileEventWatcher_arena(watcher);
    FileEventCallback legacyCb = FileEventWatcher_legacyCb(watcher);
    FileWatchCallback watchCb = FileEventWatcher_watchCb(watcher);
    any ud = FileEventWatcher_userdata(watcher);

    for (bytes i = 0; i < count; i++) {
        if (events[i].path) {
            if (watchCb) {
                FileWatchEvent ev;
                ev.path = events[i].path;
                ev.relativePath = watchRelativePath(watcher, events[i].path);
                ev.type = events[i].type;
                ev.isDir = events[i].isDir;
                ev.timestampNs = events[i].timestampNs;
                ev.epochSeconds = events[i].epochSeconds;
                ev.fileSize = events[i].fileSize;
                watchCb(&ev, ud);
            } else if (legacyCb)
                legacyCb(events[i].path, events[i].type, ud);
        }
        MemoryArena_free(arena, events[i].path);
    }
    MemoryArena_free(arena, events);
}

/** Stops the watcher, drains pending events, and destroys its arena and struct. */
fn FileEventWatcher_destroy(FileEventWatcher *watcher) {
    if (!watcher)
        return;
    FileEventWatcher_stop(watcher);
    FileEventWatcher_pump(watcher);
    pthread_mutex_destroy(FileEventWatcher_mutexMut(watcher));
    MemoryArena_destroy(FileEventWatcher_arena(watcher));
    Memory_free(watcher);
}
