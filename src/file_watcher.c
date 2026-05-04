#define _GNU_SOURCE
#include "file_watcher.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <errno.h>

#define EVENT_BUF_SIZE 4096

// ============================================================================
// 1. finalize (ARC)
// ============================================================================
static void FileWatcher_finalize(Object* obj) {
    FileWatcher* self = (FileWatcher*)obj;
    if (!self) return;

    if (self->watchFd >= 0 && self->inotifyFd >= 0) {
        inotify_rm_watch(self->inotifyFd, self->watchFd);
    }

    // 💡 의장님 지적 사항: close 후 fd 초기화 확실하고 명확하게!!
    if (self->inotifyFd >= 0) {
        close(self->inotifyFd);
        self->inotifyFd = -1;
    }
}

// ============================================================================
// 2. watch (디렉토리/파일 감시 등록)
// ============================================================================
static bool FileWatcher_watch(FileWatcher* self, Path* target) {
    if (!self || !target) return false;

    int mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_CLOSE_WRITE;
    int wd = inotify_add_watch(self->inotifyFd, target->path, mask);
    if (wd < 0) return false;

    self->watchFd = wd;
    return true;
}

// ============================================================================
// 3. onEvent (콜백 등록)
// ============================================================================
static void FileWatcher_onEvent(FileWatcher* self, EventCallback cb) {
    if (!self) return;
    self->callback = cb;
}

// ============================================================================
// 4. poll (이벤트 수신 대기 - NONBLOCK)
// ============================================================================
static void FileWatcher_poll(FileWatcher* self) {
    if (!self || self->inotifyFd < 0) return;

    char buffer[EVENT_BUF_SIZE];
    ssize_t len = read(self->inotifyFd, buffer, sizeof(buffer));
    if (len < 0) { return; }

    ssize_t i = 0;
    while (i < len) {
        struct inotify_event* ev = (struct inotify_event*)&buffer[i];
        if (self->callback) {
            self->callback(ev->len > 0 ? ev->name : NULL, ev->mask);
        }
        i += sizeof(struct inotify_event) + ev->len;
    }
}

// ============================================================================
// 5. stop (감시 중단)
// ============================================================================
static void FileWatcher_stop(FileWatcher* self) {
    if (!self || self->watchFd < 0) return;
    inotify_rm_watch(self->inotifyFd, self->watchFd);
    self->watchFd = -1;
}

// ============================================================================
// FileWatcher_Class 정의 (런타임 사이즈 누락 최종 해결!!)
// ============================================================================
const Class FileWatcher_Class = {
    .name = "FileWatcher",
    .size = sizeof(FileWatcher), // 💡 의장님 지시사항 완벽 반영!!!!
    .finalize = FileWatcher_finalize
};

// ============================================================================
// 6. 생성자 (Constructor)
// ============================================================================
FileWatcher* new_FileWatcher(void) {
    FileWatcher* self = (FileWatcher*)calloc(1, sizeof(FileWatcher));
    if (!self) return NULL;

    Object_Init((Object*)self, &FileWatcher_Class);

    self->inotifyFd = inotify_init1(IN_NONBLOCK);
    if (self->inotifyFd < 0) {
        free(self);
        return NULL;
    }

    self->watchFd = -1;
    self->callback = NULL;

    // VTable 매핑
    self->watch = FileWatcher_watch;
    self->onEvent = FileWatcher_onEvent;
    self->poll = FileWatcher_poll;
    self->stop = FileWatcher_stop;

    return self;
}