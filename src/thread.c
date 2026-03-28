#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thread.h"

extern const Class threadClass;

// 🚀 [수술 1] ARC 마법: 스스로 소유권을 내려놓는 안락사 로직
static void* internal_thread_entry(void* arg) {
    Thread* self = (Thread*)arg;
    
    // 1. 실제 작업 실행
    if (self->run_func) {
        self->return_value = self->run_func(self->arg);
    }
    
    self->is_running = false;
    
    // 임시 변수에 리턴값을 담아둠 (RELEASE 후에는 self 접근 불가)
    void* ret = self->return_value;
    
    // 2. [핵심] 작업이 끝났으므로, 시작할 때 걸어둔 RETAIN을 해제!
    // 만약 메인에서 이미 RELEASE 했다면, 이 순간 객체가 완전히 소각됩니다.
    RELEASE(self);
    
    return ret;
}

/* ================= Methods ================= */
static void impl_start(Thread* self) {
    if (self->is_running) return;
    self->is_running = true;
    
    // 🚀 [수술 2] ARC 마법: OS 스레드가 끝날 때까지 내 몸통이 증발하지 않도록 RETAIN!
    RETAIN(self); 
    
    if (pthread_create(&self->handle, NULL, internal_thread_entry, (void*)self) != 0) {
        perror("[Thread] Creation failed");
        self->is_running = false;
        RELEASE(self); // 스레드 생성 실패 시 다시 소유권 반납
    }
}

static void* impl_join(Thread* self) {
    // 🚀 [수술 3] 좀비 스레드 방지: is_running과 무관하게 joinable 상태면 무조건 join!
    // (핸들이 0이 아니면 join을 시도하고, 완료 후 0으로 초기화)
    if (self->handle != 0) {
        pthread_join(self->handle, &self->return_value);
        self->handle = 0; // 두 번 join 방지
        self->is_running = false;
    }
    return self->return_value;
}

static void impl_detach(Thread* self) {
    if (self->handle != 0) {
        pthread_detach(self->handle);
        self->handle = 0; // 분리 완료 처리
        self->is_running = false;
    }
}

/* ================= Object Overrides ================= */
static void Thread_ToString(Object* self, char* buffer, size_t len) {
    Thread* t = (Thread*)self;
    snprintf(buffer, len, "Thread(id=%lu, running=%s)", 
             (unsigned long)t->handle, t->is_running ? "true" : "false");
}

static void Thread_Finalize(Object* self) {
    Thread* t = (Thread*)self;
    // 🚀 객체가 진짜 소멸될 때, 아직 스레드 핸들이 남아있다면 분리(detach)하여 고아 스레드 방지
    if (t->handle != 0) {
        pthread_detach(t->handle);
    }
}

const Class threadClass = {
    .name = "Thread",
    .size = sizeof(Thread),
    .toString = Thread_ToString,
    .finalize = Thread_Finalize
};

Thread* new_Thread(Runnable run_func, void* arg) {
    Thread* t = (Thread*)malloc(sizeof(Thread));
    if (!t) return NULL;
    
    Object_Init((Object*)t, &threadClass);
    
    t->handle = 0; // 0으로 초기화하여 join 여부 판별
    t->run_func = run_func;
    t->arg = arg;
    t->return_value = NULL;
    t->is_running = false;
    
    t->start = impl_start;
    t->join = impl_join;
    t->detach = impl_detach;
    
    return t;
}