#define _GNU_SOURCE
#include "async_file.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static void get_log_path(AsyncFile* self, char* out, size_t out_len) {
    if (!self->rotate_daily) {
        strncpy(out, self->base.filePath->path, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    const char* base_path = self->base.filePath->path;
    const char* dot       = strrchr(base_path, '.');

    if (dot) {
        size_t prefix_len = dot - base_path;
        snprintf(out, out_len, "%.*s_%04d%02d%02d%s", (int)prefix_len, base_path, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, dot);
    } else {
        snprintf(out, out_len, "%s_%04d%02d%02d", base_path, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    }
}

static void* async_file_worker(void* arg) {
    AsyncFile* self        = (AsyncFile*)arg;
    FILE* fp          = NULL;
    char       cur_path[512]   = {0};
    int        current_day     = -1;
    time_t     last_check_time = 0;

    while (self->running) {
        AsyncWriteItem* item = (AsyncWriteItem*)self->queue->popWait(self->queue, 500);

        if (!item) {
            if (fp) {
                fflush(fp);
            }
            continue;
        }

        if (self->rotate_daily) {
            time_t now = time(NULL);
            if (now != last_check_time) {
                last_check_time = now;
                struct tm t;
                localtime_r(&now, &t);

                if (t.tm_mday != current_day) {
                    current_day = t.tm_mday;
                    char new_path[512] = {0};
                    get_log_path(self, new_path, sizeof(new_path));

                    if (fp) {
                        fclose(fp);
                        fp = NULL;
                    }

                    fp = fopen(new_path, "a");

                    if (fp) {
                      size_t len = strlen(cur_path);

                      if (len >= sizeof(new_path))
                        len = sizeof(new_path) - 1;

                      memcpy(new_path, cur_path, len);
                      new_path[len] = '\0';
                    }
                }
            }
        } else {
            if (!fp) {
                get_log_path(self, cur_path, sizeof(cur_path));
                fp = fopen(cur_path, "a");
            }
        }

        if (fp) {
            fwrite(item->data, 1, item->len, fp);
        }

        free(item->data);
        free(item);
    }

    AsyncWriteItem* leftover;
    while ((leftover = (AsyncWriteItem*)self->queue->pop(self->queue)) != NULL) {
        if (self->rotate_daily) {
            time_t now = time(NULL);
            if (now != last_check_time) {
                last_check_time = now;
                struct tm t;
                localtime_r(&now, &t);

                if (t.tm_mday != current_day) {
                    current_day = t.tm_mday;
                    char new_path[512] = {0};
                    get_log_path(self, new_path, sizeof(new_path));

                    if (fp) {
                        fclose(fp);
                        fp = NULL;
                    }

                    fp = fopen(new_path, "a");

                    if (fp) {
                      size_t len = strlen(cur_path);

                      if (len >= sizeof(new_path))
                          len = sizeof(new_path) - 1;

                      memcpy(new_path, cur_path, len);
                      new_path[len] = '\0';
                    }
                }
            }
        } else if (!fp) {
            get_log_path(self, cur_path, sizeof(cur_path));
            fp = fopen(cur_path, "a");
        }

        if (fp) {
            fwrite(leftover->data, 1, leftover->len, fp);
        }

        free(leftover->data);
        free(leftover);
    }

    if (fp) {
        fflush(fp);
        fclose(fp);
        fp = NULL;
    }

    return NULL;
}

static int AsyncFile_writeAsync(AsyncFile* self, const char* data, size_t len) {
    if (!self || !data || len == 0) {
        return -1;
    }

    AsyncWriteItem* item = (AsyncWriteItem*)malloc(sizeof(AsyncWriteItem));
    if (!item) {
        return -1;
    }

    item->data = (char*)malloc(len + 1);
    if (!item->data) {
        free(item);
        return -1;
    }

    memcpy(item->data, data, len);
    item->data[len] = '\0';
    item->len       = len;

    if (!self->queue->push(self->queue, item)) {
        free(item->data);
        free(item);
        return -1;
    }

    return 0;
}

static void AsyncFile_flush(AsyncFile* self) {
    if (!self) {
        return;
    }

    while (self->queue->getSize(self->queue) > 0) {
        usleep(1000);
    }
}

static void AsyncFile_start(AsyncFile* self) {
    if (!self || self->running) {
        return;
    }

    self->running = true;
    pthread_create(&self->worker, NULL, async_file_worker, self);
}

static void AsyncFile_stop(AsyncFile* self) {
    if (!self || !self->running) {
        return;
    }

    self->running = false;
    pthread_join(self->worker, NULL);
}

static void AsyncFile_finalize(Object* obj) {
    AsyncFile* self = (AsyncFile*)obj;

    // 🚀 [보안 패치] 진행 중인 비동기 작업 즉시 중단
    AsyncFile_stop(self);

    if (self->queue != NULL) {
        RELEASE((Object*)self->queue);
        self->queue = NULL;
    }

    if (self->base.fd >= 0) {
        close(self->base.fd);
        self->base.fd = -1;
    }

    if (self->base.filePath != NULL) {
        RELEASE((Object*)self->base.filePath);
        self->base.filePath = NULL;
    }
}

static const Class _asyncFileClass = {
    .name     = "AsyncFile",
    .size     = sizeof(AsyncFile),
    .finalize = AsyncFile_finalize
};

AsyncFile* new_AsyncFile(const char* path, bool rotate_daily) {
    if (!path) {
        return NULL;
    }

    AsyncFile* self = (AsyncFile*)calloc(1, sizeof(AsyncFile));
    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &_asyncFileClass);

    if (!File_Init(&self->base, path)) {
        RELEASE((Object*)self);
        return NULL;
    }

    self->queue = new_RingBuffer(4096);
    if (!self->queue) {
        RELEASE((Object*)self);
        return NULL;
    }

    self->rotate_daily = rotate_daily;
    self->running      = false;

    self->writeAsync = AsyncFile_writeAsync;
    self->flush      = AsyncFile_flush;
    self->start      = AsyncFile_start;
    self->stop       = AsyncFile_stop;

    return self;
}