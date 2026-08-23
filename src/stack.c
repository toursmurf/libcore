#include <stdio.h>
#include <stdlib.h>
#include "stack.h"

/* =========================================
 * [Object] Overrides
 * ========================================= */
static void Stack_ToString(Object *self, char *buffer, size_t len) {
    Stack *s = (Stack*)self;
    snprintf(buffer, len, "Stack(size=%d)", s->container->getSize(s->container));
}

static void Stack_Finalize(Object *self) {
    Stack *s = (Stack*)self;

    // 1. 내부 컨테이너 반납 (ARC Cascade)
    if (s->container) {
        RELEASE_NULL(s->container);
    }

    // 2. 락 소멸
    pthread_mutex_destroy(&s->lock);
}

const Class stackClass = {
    .name = "Stack",
    .size = sizeof(Stack),
    .toString = Stack_ToString,
    .finalize = Stack_Finalize
};

/* =========================================
 * [Methods] Implementation
 * ========================================= */

static void impl_push(Stack *self, void *data) {
    pthread_mutex_lock(&self->lock);
    // ArrayList가 내부적으로 RETAIN을 수행함
    self->container->add(self->container, (Object*)data);
    pthread_mutex_unlock(&self->lock);
}

static void* impl_pop(Stack *self) {
    pthread_mutex_lock(&self->lock);

    int current_size = self->container->getSize(self->container);
    if (current_size == 0) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }

    //마지막 요소를 detach하여 소유권 이전
    void *item = self->container->detach(self->container, current_size - 1);

    pthread_mutex_unlock(&self->lock);
    return item;
}

static void* impl_peek(Stack *self) {
    pthread_mutex_lock(&self->lock);

    int current_size = self->container->getSize(self->container);
    void *item = NULL;
    if (current_size > 0) {
        item = self->container->get(self->container, current_size - 1);
    }

    pthread_mutex_unlock(&self->lock);
    return item;
}

static bool impl_isFull(Stack *self) {
    pthread_mutex_lock(&self->lock);

    // ArrayList 구조체 내부의 size와 capacity를 직접 비교
    // (우리 ArrayList는 꽉 차면 add 시점에 자동으로 늘어나지만,
    //  지금 이 순간 '여유 공간'이 없는지를 판단합니다.)
    bool full = (self->container->size >= self->container->capacity);

    pthread_mutex_unlock(&self->lock);
    return full;
}

static bool impl_isEmpty(Stack *self) {
    pthread_mutex_lock(&self->lock);
    bool empty = (self->container->getSize(self->container) == 0);
    pthread_mutex_unlock(&self->lock);
    return empty;
}

static int impl_size(Stack *self) {
    pthread_mutex_lock(&self->lock);
    int s = self->container->getSize(self->container);
    pthread_mutex_unlock(&self->lock);
    return s;
}

/* =========================================
 * [Constructor] new_Stack
 * ========================================= */
Stack* new_Stack(int initial_capacity) {
    Stack *s = (Stack*)malloc(sizeof(Stack));
    if (!s) return NULL;

    Object_Init((Object*)s, &stackClass);
    s->container = new_ArrayList(initial_capacity);
    pthread_mutex_init(&s->lock, NULL);

    // 메서드 바인딩
    s->push = impl_push;
    s->pop = impl_pop;
    s->peek = impl_peek;
    s->isEmpty = impl_isEmpty;
    s->isFull = impl_isFull; // ✅ 바인딩 완료
    s->size = impl_size;

    return s;
}