#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#include "object.h"
#include "path.h"
#include <stdbool.h>

typedef void (*EventCallback)(const char* path, int event);

typedef struct FileWatcher FileWatcher;
struct FileWatcher {
    Object base;
    int inotifyFd;
    int watchFd;
    EventCallback callback;

    bool (*watch)(FileWatcher* self, Path* target);
    void (*onEvent)(FileWatcher* self, EventCallback cb);
    void (*poll)(FileWatcher* self);
    void (*stop)(FileWatcher* self);
};

FileWatcher* new_FileWatcher(void);

#endif // FILE_WATCHER_H
