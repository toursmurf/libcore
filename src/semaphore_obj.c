/* semaphore.c */
#include "semaphore_obj.h"
#include <stdlib.h>
#include <errno.h>

static void impl_wait(Semaphore* self) {
    // [맥/환경 의존성 방어] EINTR이면 실제로 소유를 얻기 전까지 재시도합니다.
    while (sem_wait(&self->sem) == -1 && errno == EINTR) {
        /* retry */
    }
}

static void impl_post(Semaphore* self) { sem_post(&self->sem); }

static bool impl_tryWait(Semaphore* self) {
    // [맥/환경 의존성 방어] EAGAIN=자원 없음, EINTR=재시도 그 외는 실패로 처리
    while (1) {
        if (sem_trywait(&self->sem) == 0) return true;
        if (errno == EAGAIN) return false;
        if (errno == EINTR) continue;
        return false;
    }
}
static int impl_getValue(Semaphore* self) {
    int val;
    sem_getvalue(&self->sem, &val);
    return val;
}

static void Semaphore_Finalize(Object* self) {
    Semaphore* s = (Semaphore*)self;
    sem_destroy(&s->sem);
}

const Class semaphoreClass = { .name = "Semaphore", .size = sizeof(Semaphore), .finalize = Semaphore_Finalize };

Semaphore* new_Semaphore(int initial_value) {
    Semaphore* s = (Semaphore*)malloc(sizeof(Semaphore));
    Object_Init((Object*)s, &semaphoreClass);
    sem_init(&s->sem, 0, initial_value);

    s->wait = impl_wait; s->post = impl_post;
    s->tryWait = impl_tryWait; s->getValue = impl_getValue;
    return s;
}