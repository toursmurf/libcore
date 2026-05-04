#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "object.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct RingBuffer RingBuffer;

struct RingBuffer {
    Object base;
    void** items;
    size_t capacity;
    size_t head; // 쓰기 위치
    size_t tail; // 읽기 위치
    size_t count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty; // pop 대기용

    // Java-Style OOP
    bool   (*push)    (RingBuffer* self, void* item);
    void* (*pop)     (RingBuffer* self);
    void* (*popWait) (RingBuffer* self, int timeout_ms);
    size_t (*getSize) (RingBuffer* self);
    bool   (*isEmpty) (RingBuffer* self);
    bool   (*isFull)  (RingBuffer* self);
    void   (*clear)   (RingBuffer* self);
};

RingBuffer* new_RingBuffer(size_t capacity);

#endif // RING_BUFFER_H