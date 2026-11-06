#ifndef IO_EVENT_FILE_EVENT_H
#define IO_EVENT_FILE_EVENT_H

// re/io/event/file_event.h — cross-platform file event watcher.
//
// Watches a directory tree and delivers creation/modification/deletion/rename
// events to a caller-supplied callback. macOS uses FSEvents (GCD-backed);
// other platforms are stubs that report unsupported until implemented. The
// watcher queues events and delivers them on the caller's thread via pump(),
// so callbacks never run on the OS watcher thread — lock-safe for hotcwap.

#include <stdbool.h>
#include <stddef.h>

#include "util/annotation.h"
#include "util/types.h"

typedef enum FileEventType {
    FILE_EVENT_NONE     = 0,
    FILE_EVENT_CREATED  = 1 << 0,
    FILE_EVENT_MODIFIED = 1 << 1,
    FILE_EVENT_DELETED  = 1 << 2,
    FILE_EVENT_RENAMED  = 1 << 3,
    FILE_EVENT_ALL      = 15
} FileEventType;

typedef void (*FileEventCallback)(const char *path, FileEventType type, void *userdata);

typedef struct FileEventWatcher FileEventWatcher;

// Create a watcher for `path` (a directory). `mask` selects which events to
// deliver; `callback` is invoked during pump() on the caller's thread.
FileEventWatcher *FileEventWatcher_create(const char *path, FileEventType mask,
                                          FileEventCallback callback, void *userdata);

// Start the OS-level watch. Returns false if the platform is unsupported or
// the watch could not be started.
bool FileEventWatcher_start(FileEventWatcher *watcher);

// Stop the OS-level watch and join its thread. Safe to call when not started.
void FileEventWatcher_stop(FileEventWatcher *watcher);

// Drain queued events and invoke callbacks on the caller's thread.
void FileEventWatcher_pump(FileEventWatcher *watcher);

// Destroy the watcher. Implicitly stops it first.
void FileEventWatcher_destroy(FileEventWatcher *watcher);

#endif
