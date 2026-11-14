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

static fn enqueue(FileEventWatcher *watcher, const char *path, FileEventType type) {
    if (!watcher || !path)
        return;
    if (!(type & (*watcher).mask))
        return;
    pthread_mutex_lock(&(*watcher).mutex);
    if ((*watcher).queueCount == (*watcher).queueCapacity) {
        bytes newCap = (*watcher).queueCapacity ? (*watcher).queueCapacity * 2 : 16;
        QueuedEvent *grown = (*watcher).queue
            ? (QueuedEvent*) Memory_realloc((*watcher).queue, newCap * sizeof(QueuedEvent))
            : (QueuedEvent*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, newCap * sizeof(QueuedEvent));
        if (!grown) {
            pthread_mutex_unlock(&(*watcher).mutex);
            return;
        }
        (*watcher).queue = grown;
        (*watcher).queueCapacity = newCap;
    }
    (*watcher).queue[(*watcher).queueCount].path = (char*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, strlen(path)+1);
    if ((*watcher).queue[(*watcher).queueCount].path) strcpy((*watcher).queue[(*watcher).queueCount].path, path);
    (*watcher).queue[(*watcher).queueCount].type = type;
    (*watcher).queueCount++;
    pthread_mutex_unlock(&(*watcher).mutex);
}

#if defined(VEX_PLATFORM_APPLE)
static fn fsCallback(ConstFSEventStreamRef streamRef, any clientCallBackInfo,
                       bytes numEvents, any eventPaths,
                       const FSEventStreamEventFlags eventFlags[],
                       const FSEventStreamEventId eventIds[]) {
    (fn) streamRef;
    (fn) eventIds;
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
    if (watcher) memset(watcher, 0, sizeof(FileEventWatcher));
    if (!watcher)
        return nullptr;
    (*watcher).path = (char*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, strlen(path)+1);
    if ((*watcher).path) strcpy((*watcher).path, path);
    if (!(*watcher).path) {
        Memory_free(watcher);
        return nullptr;
    }
    (*watcher).mask = mask ? mask : FILE_EVENT_ALL;
    (*watcher).callback = callback;
    (*watcher).userdata = userdata;
    pthread_mutex_init(&(*watcher).mutex, nullptr);
    (*watcher).running = false;
    return watcher;
}

bool FileEventWatcher_start(FileEventWatcher *watcher) {
    if (!watcher || (*watcher).running)
        return false;

#if defined(VEX_PLATFORM_APPLE)
    FSEventStreamContext context = { 0, watcher, nullptr, nullptr, nullptr };
    (*watcher).stream = FSEventStreamCreate(nullptr, fsCallback, &context,
                                         (CFArrayRef) CFArrayCreate(nullptr,
                                             (const_any *) &(*watcher).path, 1,
                                             &kCFTypeArrayCallBacks),
                                         kFSEventStreamEventIdSinceNow,
                                         0.25,
                                         kFSEventStreamCreateFlagFileEvents);
    if (!(*watcher).stream)
        return false;
    (*watcher).dispatchQueue = dispatch_queue_create("vex.file_event", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue((*watcher).stream, (*watcher).dispatchQueue);
    if (!FSEventStreamStart((*watcher).stream)) {
        FSEventStreamInvalidate((*watcher).stream);
        FSEventStreamRelease((*watcher).stream);
        (*watcher).stream = nullptr;
        return false;
    }
    (*watcher).running = true;
    return true;
#else
    THROW("FileEventWatcher: platform not supported");
    return false;
#endif
}

fn FileEventWatcher_stop(FileEventWatcher *watcher) {
    if (!watcher || !(*watcher).running)
        return;
#if defined(VEX_PLATFORM_APPLE)
    if ((*watcher).stream) {
        FSEventStreamStop((*watcher).stream);
        FSEventStreamInvalidate((*watcher).stream);
        FSEventStreamRelease((*watcher).stream);
        (*watcher).stream = nullptr;
    }
    if ((*watcher).dispatchQueue) {
        dispatch_release((*watcher).dispatchQueue);
        (*watcher).dispatchQueue = nullptr;
    }
#endif
    (*watcher).running = false;
}

fn FileEventWatcher_pump(FileEventWatcher *watcher) {
    if (!watcher)
        return;
    pthread_mutex_lock(&(*watcher).mutex);
    bytes count = (*watcher).queueCount;
    QueuedEvent *events = (*watcher).queue;
    (*watcher).queue = nullptr;
    (*watcher).queueCount = 0;
    (*watcher).queueCapacity = 0;
    pthread_mutex_unlock(&(*watcher).mutex);

    for (bytes i = 0; i < count; i++) {
        if (events[i].path && (*watcher).callback)
            (*watcher).callback(events[i].path, events[i].type, (*watcher).userdata);
        Memory_free(events[i].path);
    }
    Memory_free(events);
}

fn FileEventWatcher_destroy(FileEventWatcher *watcher) {
    if (!watcher)
        return;
    FileEventWatcher_stop(watcher);
    FileEventWatcher_pump(watcher);
    pthread_mutex_destroy(&(*watcher).mutex);
    Memory_free((*watcher).path);
    Memory_free((*watcher).queue);
    Memory_free(watcher);
}
