#include "async_logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <inttypes.h>

/* =========================
   내부 이벤트
   ========================= */

typedef struct {
    int level;
    uint64_t ts;
    char* message;
} AsyncLogEvent;

static uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static void AsyncLogEvent_free(AsyncLogEvent* e) {
    if (!e) return;
    free(e->message);
    free(e);
}

/* =========================
   finalize
   ========================= */
extern char* safe_strdup(const char* src, size_t max_len);
static void AsyncLogger_finalize(Object* obj) {
    AsyncLogger* self = (AsyncLogger*)obj;

    if (self->running) {
        self->stop(self);
    }

    if (self->queue) {
        RELEASE(self->queue);
    }

    if (self->inner) {
        RELEASE(self->inner);
    }

    pthread_mutex_destroy(&self->lock);
    pthread_cond_destroy(&self->cond);
}

/* =========================
   Class 정의
   ========================= */

static Class AsyncLogger_Class = {
    .name = "AsyncLogger",
    .size = sizeof(AsyncLogger),

    .toString = NULL,
    .equals   = NULL,
    .hashCode = NULL,
    .finalize = AsyncLogger_finalize
};

/* =========================
   Worker
   ========================= */

static void* worker_main(void* arg) {
    AsyncLogger* self = (AsyncLogger*)arg;
    size_t count = 0;

    while (1) {
        pthread_mutex_lock(&self->lock);

        while (self->running &&
               self->queue->getSize(self->queue) == 0) {
            pthread_cond_wait(&self->cond, &self->lock);
        }

        if (!self->running &&
            self->queue->getSize(self->queue) == 0) {
            pthread_mutex_unlock(&self->lock);
            break;
        }

        AsyncLogEvent* e =
            self->queue->dequeue(self->queue);

        pthread_mutex_unlock(&self->lock);

        if (!e) continue;

        self->inner->info(self->inner,
            "[%" PRIu64 "] %s",
            e->ts,
            e->message
        );

        AsyncLogEvent_free(e);

        count++;

        if (count >= self->batch_size) {
            if (self->inner->fileHandle) {
                fflush(self->inner->fileHandle);
            }
            count = 0;
        }
    }

    if (self->inner->fileHandle) {
        fflush(self->inner->fileHandle);
    }

    return NULL;
}

/* =========================
   Control
   ========================= */

static void AsyncLogger_start(AsyncLogger* self) {
    pthread_create(&self->worker, NULL, worker_main, self);
}

static void AsyncLogger_stop(AsyncLogger* self) {
    pthread_mutex_lock(&self->lock);
    self->running = false;
    pthread_cond_signal(&self->cond);
    pthread_mutex_unlock(&self->lock);

    pthread_join(self->worker, NULL);
}

/* =========================
   Log API
   ========================= */

void AsyncLogger_log(
    AsyncLogger* self,
    int level,
    const char* file,
    int line,
    const char* fmt,
    ...
) {
    if (!self) return;

    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    char final[1030];
    snprintf(final, sizeof(final),
        "[%s:%d] %s", file, line, buffer);

    AsyncLogEvent* e = malloc(sizeof(AsyncLogEvent));
    if (!e) return;

    e->level = level;
    e->ts = now_ms();
    e->message = safe_strdup(final, 1030);
    pthread_mutex_lock(&self->lock);

    if (self->queue->getSize(self->queue) > 10000) {
        AsyncLogEvent_free(e);
    } else {
        self->queue->enqueue(self->queue, e);
        pthread_cond_signal(&self->cond);
    }

    pthread_mutex_unlock(&self->lock);
}

/* =========================
   생성자 (최종)
   ========================= */

AsyncLogger* new_AsyncLogger(int level) {
    AsyncLogger* self = calloc(1, sizeof(AsyncLogger));
    if (!self) return NULL;

    // 🔥 핵심: Class까지 한 번에
    Object_Init((Object*)self, &AsyncLogger_Class);

    self->queue = new_Queue(1024);
    self->inner = new_Logger(level);

    pthread_mutex_init(&self->lock, NULL);
    pthread_cond_init(&self->cond, NULL);

    self->running = true;
    self->batch_size = 100;

    self->start = AsyncLogger_start;
    self->stop  = AsyncLogger_stop;

    self->start(self);

    return self;
}