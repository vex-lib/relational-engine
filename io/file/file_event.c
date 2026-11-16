#include "vex.h"
#include "io/file/file.h"

#include <pthread.h>
#include <string.h>

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
 * Cross-platform file event watcher. macOS uses FSEvents delivered on a GCD
 * queue; the events are queued and drained by pump() on the caller's thread so
 * callbacks never run on the OS watcher thread. Other platforms are stubs.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: FileEventWatcher (io/file/file.c)
 * ============================================================================
 * Cross-platform file event watcher: one FileEventWatcher owner per watched
 * directory, a mutex-guarded queue of borrowed path copies, and a caller-fed
 * pump. macOS delivers FSEvents on a GCD queue; the watcher only enqueues and
 * the caller's pump() invokes callbacks, so no OS thread enters engine code.
 * Other platforms compile the same contract as an explicit unsupported start.
 *
 * STRUCT FIELDS:
 * ----------------------------------------------------------------------------
 *   FileEventWatcher {
 *     char *path;                  // watched directory, owned copy
 *     FileEventType mask;          // delivered event bits
 *     FileEventCallback callback;  // caller callback, never invoked on the OS thread
 *     any userdata;              // callback context, caller-owned
 *     pthread_mutex_t mutex;       // guards the queue and its growth
 *     QueuedEvent *queue;          // owned pending events (path copy + type)
 *     bytes queueCount;           // pending events
 *     bytes queueCapacity;        // allocated queue slots
 *     FSEventStreamRef stream;     // Apple-only OS stream
 *     dispatch_queue_t dispatchQueue; // Apple-only delivery queue
 *     bool running;                // OS watch established
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Lifecycle:
 *   - FileEventWatcher_create(path, mask, callback, userdata)
 *   - FileEventWatcher_start(watcher)
 *   - FileEventWatcher_stop(watcher)
 *   - FileEventWatcher_destroy(watcher)
 * Delivery:
 *   - FileEventWatcher_pump(watcher)
 * ============================================================================
 */

typedef struct QueuedEvent {
    char *path;
    FileEventType type;
} QueuedEvent;

struct FileEventWatcher {
    char *path;
    FileEventType mask;
    FileEventCallback callback;
    any userdata;

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
static inline char *FileEventWatcher_path(const FileEventWatcher *w) { return (*w).path; }
;;GETTER
static inline char **FileEventWatcher_pathPtr(FileEventWatcher *w) { return &(*w).path; }
;;SETTER
static inline fn FileEventWatcher_setPath(FileEventWatcher *w, char *p) { (*w).path = p; }
;;GETTER
static inline FileEventType FileEventWatcher_mask(const FileEventWatcher *w) { return (*w).mask; }
;;SETTER
static inline fn FileEventWatcher_setMask(FileEventWatcher *w, FileEventType m) { (*w).mask = m; }
;;GETTER
static inline FileEventCallback FileEventWatcher_callback(const FileEventWatcher *w) { return (*w).callback; }
;;SETTER
static inline fn FileEventWatcher_setCallback(FileEventWatcher *w, FileEventCallback cb) { (*w).callback = cb; }
;;GETTER
static inline any FileEventWatcher_userdata(const FileEventWatcher *w) { return (*w).userdata; }
;;SETTER
static inline fn FileEventWatcher_setUserdata(FileEventWatcher *w, any ud) { (*w).userdata = ud; }
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
static inline bool FileEventWatcher_running(const FileEventWatcher *w) { return (*w).running; }
;;SETTER
static inline fn FileEventWatcher_setRunning(FileEventWatcher *w, bool r) { (*w).running = r; }
#if defined(VEX_PLATFORM_APPLE)
;;GETTER
static inline FSEventStreamRef FileEventWatcher_stream(const FileEventWatcher *w) { return (*w).stream; }
;;SETTER
static inline fn FileEventWatcher_setStream(FileEventWatcher *w, FSEventStreamRef s) { (*w).stream = s; }
;;GETTER
static inline dispatch_queue_t FileEventWatcher_dispatchQueue(const FileEventWatcher *w) {
    return (*w).dispatchQueue;
}
;;SETTER
static inline fn FileEventWatcher_setDispatchQueue(FileEventWatcher *w, dispatch_queue_t q) {
    (*w).dispatchQueue = q;
}
#endif

static fn enqueue(FileEventWatcher *watcher, const char *path, FileEventType type) {
    if (!watcher || !path) 
        return;

    if (!(type & FileEventWatcher_mask(watcher))) 
        return;

    pthread_mutex_t *mtx = FileEventWatcher_mutexMut(watcher);
    pthread_mutex_lock(mtx);

    bytes qcount = FileEventWatcher_queueCount(watcher);
    bytes qcap = FileEventWatcher_queueCapacity(watcher);
    QueuedEvent *queue = FileEventWatcher_queue(watcher);
    if (qcount == qcap) {
        bytes newCap = qcap ? qcap * 2 : 16;

        QueuedEvent *grown = queue
            ? (QueuedEvent*) Memory_realloc(queue, newCap * sizeof(QueuedEvent))
            : (QueuedEvent*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, newCap * sizeof(QueuedEvent));
        if (!grown) {
            pthread_mutex_unlock(mtx);

            return;
        }

        queue = grown;
        FileEventWatcher_setQueue(watcher, queue);

        FileEventWatcher_setQueueCapacity(watcher, newCap);
    }

    queue[qcount].path = (char*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, strlen(path)+1);

    if (queue[qcount].path) 
       strcpy(queue[qcount].path, path);

    queue[qcount].type = type;

    FileEventWatcher_setQueueCount(watcher, qcount + 1u);

    pthread_mutex_unlock(mtx);
}

#if defined(VEX_PLATFORM_APPLE)
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

        enqueue(watcher, paths[i], type);
    }
}

#endif

FileEventWatcher *FileEventWatcher_create(const char *path, FileEventType mask,
                                          FileEventCallback callback, any userdata) {
    if (!path || !callback) 
        return nullptr;

    FileEventWatcher *watcher = (FileEventWatcher*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, sizeof(FileEventWatcher));

    if (watcher) 
       memset(watcher, 0, sizeof(FileEventWatcher));

    if (!watcher) 
        return nullptr;

    char *wpath = (char*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, strlen(path)+1);
    FileEventWatcher_setPath(watcher, wpath);

    if (wpath) 
       strcpy(wpath, path);

    if (!wpath) {
        Memory_free(watcher);

        return nullptr;
    }

    FileEventWatcher_setMask(watcher, mask ? mask : FILE_EVENT_ALL);

    FileEventWatcher_setCallback(watcher, callback);

    FileEventWatcher_setUserdata(watcher, userdata);

    pthread_mutex_init(FileEventWatcher_mutexMut(watcher), nullptr);

    FileEventWatcher_setRunning(watcher, false);

    return watcher;
}

bool FileEventWatcher_start(FileEventWatcher *watcher) {
    if (!watcher || FileEventWatcher_running(watcher)) 
        return false;

#if defined(VEX_PLATFORM_APPLE)
    FSEventStreamContext context = { 0, watcher, nullptr, nullptr, nullptr };

    FSEventStreamRef stream = FSEventStreamCreate(nullptr, fsCallback, &context,
                                         (CFArrayRef) CFArrayCreate(nullptr,
                                             (const_any*) FileEventWatcher_pathPtr(watcher), 1,
                                             &kCFTypeArrayCallBacks),
                                         kFSEventStreamEventIdSinceNow,
                                         0.25,
                                         kFSEventStreamCreateFlagFileEvents);
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

        return false;
    }

    FileEventWatcher_setRunning(watcher, true);

    return true;
#else
    THROW("FileEventWatcher: platform not supported");

    return false;
#endif
}

fn FileEventWatcher_stop(FileEventWatcher *watcher) {
    if (!watcher || !FileEventWatcher_running(watcher)) 
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

    FileEventCallback cb = FileEventWatcher_callback(watcher);
    any ud = FileEventWatcher_userdata(watcher);
    for (bytes i = 0; i < count; i++) {
        if (events[i].path && cb) 
            cb(events[i].path, events[i].type, ud);

        Memory_free(events[i].path);
    }

    Memory_free(events);
}

fn FileEventWatcher_destroy(FileEventWatcher *watcher) {
    if (!watcher) 
        return;

    FileEventWatcher_stop(watcher);

    FileEventWatcher_pump(watcher);

    pthread_mutex_destroy(FileEventWatcher_mutexMut(watcher));

    Memory_free(FileEventWatcher_path(watcher));

    Memory_free(FileEventWatcher_queue(watcher));

    Memory_free(watcher);
}
