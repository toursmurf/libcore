/* semaphore.h */
#ifndef SEMAPHORE_OBJ_H
#define SEMAPHORE_OBJ_H

#include "object.h"
#include <semaphore.h>

typedef struct Semaphore Semaphore;

struct Semaphore {
    Object base;
    sem_t sem;

    // 메서드 영역
    void (*wait)(Semaphore* self);      // P 연산 (자원 획득)
    void (*post)(Semaphore* self);      // V 연산 (자원 반납)
    bool (*tryWait)(Semaphore* self);   // 비차단 획득
    int (*getValue)(Semaphore* self);   // 현재 잔여 자원 확인
};

Semaphore* new_Semaphore(int initial_value);

#endif
