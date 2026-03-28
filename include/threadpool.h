#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "object.h"
#include "thread.h"   // 🚀 여기서 Runnable (void* (*)(void*)) 이미 정의됨
#include "queue.h"
#include "semaphore_obj.h"
#include "arraylist.h"
#include <pthread.h>
#include <stdbool.h>

// 🚀 언더바(_) 전면 폐지 및 타입 일치
typedef struct ThreadPool ThreadPool;
typedef struct Task Task;

typedef void* (*TaskRoutine)(void* arg);
typedef void* (*Consumer)(void* data);
typedef void* (*Transformer)(void* data);
typedef bool  (*Predicate)(void* data);

// --- 작업(Task) 단위 객체 ---
struct Task {
    Object base;
    TaskRoutine func;
    void* arg;
};

// --- ThreadPool 구조체 ---
struct ThreadPool {
    Object base;

    int num_threads;
    Thread** workers;
    Queue* taskQueue;
    Semaphore* resourceSem;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool stop;

    // --- 주요 메서드 ---
    void (*submit)(ThreadPool* self, TaskRoutine func, void* arg);
    void* (*parallel_for)(ThreadPool* self, ArrayList* list, Consumer action);
    void (*shutdown)(ThreadPool* self);
    int  (*getPendingCount)(ThreadPool* self);
};

// 🚀 생성자 명칭 통일 (Standard v8.0)
ThreadPool* new_ThreadPool(int num_threads, int max_resources);

#endif