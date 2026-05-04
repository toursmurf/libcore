#define _GNU_SOURCE
#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// ============================================================
// 메서드 구현부
// ============================================================
static bool rb_push(RingBuffer* self, void* item) {
    if (!self || !item) return false;
    pthread_mutex_lock(&self->lock);

    if (self->count >= self->capacity) {
        // 가득 차면 drop!! (EventLoop 블로킹 금지)
        pthread_mutex_unlock(&self->lock);
        return false;
    }

    self->items[self->head] = item;
    self->head = (self->head + 1) % self->capacity;
    self->count++;

    pthread_cond_signal(&self->not_empty);
    pthread_mutex_unlock(&self->lock);
    return true;
}

static void* rb_pop(RingBuffer* self) {
    if (!self) return NULL;
    pthread_mutex_lock(&self->lock);

    if (self->count == 0) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }

    void* item = self->items[self->tail];
    self->items[self->tail] = NULL;
    self->tail = (self->tail + 1) % self->capacity;
    self->count--;

    pthread_mutex_unlock(&self->lock);
    return item;
}

static void* rb_pop_wait(RingBuffer* self, int timeout_ms) {
    if (!self) return NULL;
    pthread_mutex_lock(&self->lock);

    if (self->count == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&self->not_empty, &self->lock, &ts);
    }

    if (self->count == 0) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }

    void* item = self->items[self->tail];
    self->items[self->tail] = NULL;
    self->tail = (self->tail + 1) % self->capacity;
    self->count--;

    pthread_mutex_unlock(&self->lock);
    return item;
}

static size_t rb_get_size(RingBuffer* self) {
    if (!self) return 0;
    pthread_mutex_lock(&self->lock);
    size_t n = self->count;
    pthread_mutex_unlock(&self->lock);
    return n;
}

static bool rb_is_empty(RingBuffer* self) {
    return rb_get_size(self) == 0;
}

static bool rb_is_full(RingBuffer* self) {
    if (!self) return false;
    pthread_mutex_lock(&self->lock);
    bool full = (self->count >= self->capacity);
    pthread_mutex_unlock(&self->lock);
    return full;
}

static void rb_clear(RingBuffer* self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    memset(self->items, 0, sizeof(void*) * self->capacity);
    self->head = 0;
    self->tail = 0;
    self->count = 0;
    pthread_mutex_unlock(&self->lock);
}

// ============================================================
// ARC 소멸자 및 생성자
// ============================================================
static void rb_finalize(Object* obj) {
    RingBuffer* self = (RingBuffer*)obj;
    if (!self) return;

    // items 내부 포인터는 호출자가 관리!!
    // RingBuffer는 배열만 해제!!
    if (self->items) {
        free(self->items);
        self->items = NULL;
    }
    pthread_mutex_destroy(&self->lock);
    pthread_cond_destroy(&self->not_empty);
}

// 🚨 런타임 사이즈 완벽 주입!!
static const Class _rbClass = {
    .name = "RingBuffer",
    .size = sizeof(RingBuffer),
    .finalize = rb_finalize
};

RingBuffer* new_RingBuffer(size_t capacity) {
    if (capacity == 0) capacity = 1024;

    RingBuffer* self = (RingBuffer*)calloc(1, sizeof(RingBuffer));
    if (!self) return NULL;

    Object_Init((Object*)self, &_rbClass);

    // 🚨 배열 할당 및 방어 로직 (NULL 체크 후 메모리 반환) 완비!!
    self->items = (void**)calloc(capacity, sizeof(void*));
    if (!self->items) {
        free(self);
        return NULL;
    }

    self->capacity = capacity;
    self->head = 0;
    self->tail = 0;
    self->count = 0;

    pthread_mutex_init(&self->lock, NULL);
    pthread_cond_init(&self->not_empty, NULL);

    self->push = rb_push;
    self->pop = rb_pop;
    self->popWait = rb_pop_wait;
    self->getSize = rb_get_size;
    self->isEmpty = rb_is_empty;
    self->isFull = rb_is_full;
    self->clear = rb_clear;

    return self;
}