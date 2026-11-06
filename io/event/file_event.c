#include "re/io/event/file_event.h"

#include <pthread.h>
#include <string.h>

#include "re/nio/mem.h"
#include "re/type/type.h"
#include "util/platform.h"
#include "util/throw.h"

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

typedef struct QueuedEvent {
    char *path;
    FileEventType type;
} QueuedEvent;

struct FileEventWatcher {
    char *path;
    FileEventType mask;
    FileEventCallback callback;
    void *userdata;

    pthread_mutex_t mutex;
    QueuedEvent *queue;
    size_t queueCount;
    size_t queueCapacity;

#if defined(VEX_PLATFORM_APPLE)
    FSEventStreamRef stream;
    dispatch_queue_t dispatchQueue;
#endif

    bool running;
};

static void enqueue(FileEventWatcher *watcher, const char *path, FileEventType type) {
    if (!watcher || !path)
        return;
    if (!(type & (*watcher).mask))
        return;
    pthread_mutex_lock(&(*watcher).mutex);
    if ((*watcher).queueCount == (*watcher).queueCapacity) {
        size_t newCap = (*watcher).queueCapacity ? (*watcher).queueCapacity * 2 : 16;
        QueuedEvent *grown = (QueuedEvent*) Memory_alloc(TYPE_FILE_EVENT_WATCHER, newCap * sizeof(QueuedEvent));
        if (grown && (*watcher).queue) memcpy(grown, (*watcher).queue, (*watcher).queueCount * sizeof(QueuedEvent));
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
static void fsCallback(ConstFSEventStreamRef streamRef, void *clientCallBackInfo,
                       size_t numEvents, void *eventPaths,
                       const FSEventStreamEventFlags eventFlags[],
                       const FSEventStreamEventId eventIds[]) {
    (void) streamRef;
    (void) eventIds;
    FileEventWatcher *watcher = (FileEventWatcher*) clientCallBackInfo;
    char **paths = (char**) eventPaths;
    for (size_t i = 0; i < numEvents; i++) {
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
                                          FileEventCallback callback, void *userdata) {
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
                                             (const void**) &(*watcher).path, 1,
                                             &kCFTypeArrayCallBacks),
                                         kFSEventStreamEventIdSinceNow,
                                         0.25, // latency seconds
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

void FileEventWatcher_stop(FileEventWatcher *watcher) {
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

void FileEventWatcher_pump(FileEventWatcher *watcher) {
    if (!watcher)
        return;
    pthread_mutex_lock(&(*watcher).mutex);
    size_t count = (*watcher).queueCount;
    QueuedEvent *events = (*watcher).queue;
    (*watcher).queue = nullptr;
    (*watcher).queueCount = 0;
    (*watcher).queueCapacity = 0;
    pthread_mutex_unlock(&(*watcher).mutex);

    for (size_t i = 0; i < count; i++) {
        if (events[i].path && (*watcher).callback)
            (*watcher).callback(events[i].path, events[i].type, (*watcher).userdata);
        Memory_free(events[i].path);
    }
    Memory_free(events);
}

void FileEventWatcher_destroy(FileEventWatcher *watcher) {
    if (!watcher)
        return;
    FileEventWatcher_stop(watcher);
    // drain any remaining
    FileEventWatcher_pump(watcher);
    pthread_mutex_destroy(&(*watcher).mutex);
    Memory_free((*watcher).path);
    Memory_free((*watcher).queue);
    Memory_free(watcher);
}
