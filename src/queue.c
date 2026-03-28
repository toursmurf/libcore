#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

/* =========================================
 * [Object] Overrides
 * ========================================= */
static void Queue_ToString(Object *self, char *buffer, size_t len) {
    Queue *q = (Queue*)self;
    snprintf(buffer, len, "Queue(size=%d)", q->container->getSize(q->container));
}

static void Queue_Finalize(Object *self) {
    Queue *q = (Queue*)self;
    if (q->container) {
        RELEASE_NULL(q->container);
    }
    pthread_mutex_destroy(&q->lock);
}

const Class queueClass = {
    .name = "Queue",
    .size = sizeof(Queue),
    .toString = Queue_ToString,
    .finalize = Queue_Finalize
};

/* =========================================
 * [Methods] Implementation
 * ========================================= */

static void impl_enqueue(Queue *self, void *data) {
    pthread_mutex_lock(&self->lock);
    self->container->add(self->container, (Object*)data);
    pthread_mutex_unlock(&self->lock);
}

static void* impl_dequeue(Queue *self) {
    pthread_mutex_lock(&self->lock);
    if (self->container->getSize(self->container) == 0) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }
    // detach를 통해 안전하게 소유권 이전
    void *item = self->container->detach(self->container, 0);
    pthread_mutex_unlock(&self->lock);
    return item;
}

static void* impl_peek(Queue *self) {
    pthread_mutex_lock(&self->lock);
    void *item = self->container->get(self->container, 0);
    pthread_mutex_unlock(&self->lock);
    return item;
}

static bool impl_isEmpty(Queue *self) {
    pthread_mutex_lock(&self->lock);
    bool empty = (self->container->getSize(self->container) == 0);
    pthread_mutex_unlock(&self->lock);
    return empty;
}

static int impl_size(Queue *self) {
    pthread_mutex_lock(&self->lock);
    int s = self->container->getSize(self->container);
    pthread_mutex_unlock(&self->lock);
    return s;
}

static void impl_forEach(Queue *self, void (*action)(Object*)) {
    pthread_mutex_lock(&self->lock);
    self->container->forEach(self->container, action);
    pthread_mutex_unlock(&self->lock);
}

// 🚀 [복구 완료] 이터레이터 구현부
static ArrayListIterator* impl_iterator(Queue *self) {
    pthread_mutex_lock(&self->lock);
    // Queue의 내부 ArrayList 이터레이터를 생성하여 반환
    ArrayListIterator* it = self->container->iterator(self->container);
    pthread_mutex_unlock(&self->lock);
    return it;
}

/* =========================================
 * [Constructor] new_Queue
 * ========================================= */
Queue* new_Queue(int initial_capacity) {
    Queue *q = (Queue*)malloc(sizeof(Queue));
    if (!q) return NULL;

    Object_Init((Object*)q, &queueClass);

    q->container = new_ArrayList(initial_capacity);
    if (!q->container) { free(q); return NULL; }

    pthread_mutex_init(&q->lock, NULL);

    // 메서드 연결
    q->enqueue = impl_enqueue;
    q->dequeue = impl_dequeue;
    q->peek = impl_peek;
    q->isEmpty = impl_isEmpty;
    q->size = impl_size;
    q->getSize = impl_size;
    q->forEach = impl_forEach;
    q->iterator = impl_iterator; // ✅ 바인딩 완료!

    return q;
}