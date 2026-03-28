#ifndef QUEUE_H
#define QUEUE_H

#include "object.h"
#include "arraylist.h"
#include <pthread.h>

extern const Class queueClass;

typedef struct Queue Queue;

struct Queue {
    Object base;
    ArrayList* container;
    pthread_mutex_t lock;

    // --- 메서드 인터페이스 ---
    void (*enqueue)(Queue* self, void* data);
    void* (*dequeue)(Queue* self);
    void* (*peek)(Queue* self);

    bool (*isEmpty)(Queue* self);
    int (*size)(Queue* self);
    int (*getSize)(Queue* self);

    void (*forEach)(Queue* self, void (*action)(Object*));

    // 🚀 [복구 완료] 이터레이터 메서드
    ArrayListIterator* (*iterator)(Queue* self);
};

/* [Constructor] */
Queue* new_Queue(int initial_capacity);

#endif