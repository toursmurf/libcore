#ifndef THREAD_H
#define THREAD_H

#include "object.h"
#include <pthread.h>
#include <stdbool.h>

extern const Class threadClass;

typedef struct Thread Thread;

// 스레드가 실행할 작업 함수 타입 (Java의 Runnable 역할)
typedef void* (*Runnable)(void* arg);

struct Thread {
    // 1. 상속 (ARC 및 다형성을 위한 Object base, 반드시 첫 번째 위치!)
    Object base;

    // 2. 스레드 고유 상태 및 자원
    pthread_t handle;       // OS 스레드 핸들
    Runnable run_func;      // 백그라운드에서 실행할 함수
    void* arg;              // 함수에 넘길 인자
    void* return_value;     // 작업 결과 값 저장소
    
    bool is_running;        // 실행 상태 플래그

    // 3. 메서드 인터페이스
    void (*start)(Thread* self);
    void* (*join)(Thread* self);
    void (*detach)(Thread* self);
};

/* =========================================
 * [Constructor]
 * run_func : 실행할 함수 포인터 (NULL 불가)
 * arg      : 넘겨줄 데이터 (Object* 타입일 경우 ARC 소유권 관리에 주의해야 함!)
 * ========================================= */
Thread* new_Thread(Runnable run_func, void* arg);

#endif