/*
 * File: threadpool.c
 * Author: InDong KIM (idong322@naver.com)
 * Copyright (c) 2026 Toos IT Holdings. All rights reserved.
 * License: MIT
 */
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>

// --- Task 객체용 클래스 정의 ---
static void Task_Finalize(Object* obj) {
    (void)obj;
}

const Class taskClass = {
    .name = "Task",
    .size = sizeof(Task),
    .finalize = Task_Finalize
};

/* =========================================
 * [Internal] 워커 스레드 루프
 * ========================================= */
static void* worker_routine(void* arg) {
    ThreadPool* self = (ThreadPool*)arg;

    while (true) {
        pthread_mutex_lock(&self->lock);

        while (self->taskQueue->isEmpty(self->taskQueue) && !self->stop) {
            pthread_cond_wait(&self->cond, &self->lock);
        }

        if (self->stop && self->taskQueue->isEmpty(self->taskQueue)) {
            pthread_mutex_unlock(&self->lock);
            break;
        }

        Task* task = (Task*)self->taskQueue->dequeue(self->taskQueue);
        pthread_mutex_unlock(&self->lock);

        if (task) {
            if (self->resourceSem) self->resourceSem->wait(self->resourceSem);
            task->func(task->arg);
            if (self->resourceSem) self->resourceSem->post(self->resourceSem);
            RELEASE((Object*)task);
        }
    }
    return NULL;
}

/* =========================================
 * [Methods] 구현부
 * ========================================= */
static void impl_submit(ThreadPool* self, TaskRoutine func, void* arg) {
    Task* task = calloc(1, sizeof(Task));
    Object_Init((Object*)task, &taskClass);
    task->func = func;
    task->arg  = arg;

    pthread_mutex_lock(&self->lock);
    self->taskQueue->enqueue(
        self->taskQueue, (Object*)task);
    // ✅ enqueue가 RETAIN했으니
    //    로컬 소유권 반납!
    RELEASE((Object*)task);
    pthread_cond_signal(&self->cond);
    pthread_mutex_unlock(&self->lock);
}

static void* impl_parallel_for(ThreadPool* self, ArrayList* list, Consumer action) {
    if (!list || !action) return NULL;
    int size = list->getSize(list);
    for (int i = 0; i < size; i++) {
        self->submit(self, (TaskRoutine)action, list->get(list, i));
    }
    return NULL;
}

static void impl_shutdown(ThreadPool* self) {
    pthread_mutex_lock(&self->lock);
    if (self->stop) {
        pthread_mutex_unlock(&self->lock);
        return;
    }
    self->stop = true;
    pthread_cond_broadcast(&self->cond);
    pthread_mutex_unlock(&self->lock);

    for (int i = 0; i < self->num_threads; i++) {
        self->workers[i]->join(self->workers[i]);
    }
}

static int impl_getPendingCount(ThreadPool* self) {
    return self->taskQueue->getSize(self->taskQueue);
}

/* =========================================
 * [Object] 생명주기 관리
 * ========================================= */
static void ThreadPool_Finalize(Object* self) {
    ThreadPool* tp = (ThreadPool*)self;
    if (!tp->stop) tp->shutdown(tp);

    for (int i = 0; i < tp->num_threads; i++) {
        RELEASE((Object*)tp->workers[i]);
    }
    free(tp->workers);
    RELEASE((Object*)tp->taskQueue);
    if (tp->resourceSem) RELEASE((Object*)tp->resourceSem);

    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->cond);
}

const Class threadPoolClass = {
    .name = "ThreadPool",
    .size = sizeof(ThreadPool),
    .finalize = ThreadPool_Finalize
};

/* =========================================
 * [Constructor] 생성자
 * ========================================= */
ThreadPool* new_ThreadPool(int num_threads, int max_resources) {
    ThreadPool* tp = (ThreadPool*)calloc(1, sizeof(ThreadPool));
    Object_Init((Object*)tp, &threadPoolClass);

    tp->num_threads = num_threads;
    tp->taskQueue = new_Queue(1024);
    tp->stop = false;

    tp->resourceSem = (max_resources > 0) ? new_Semaphore(max_resources) : NULL;

    pthread_mutex_init(&tp->lock, NULL);
    pthread_cond_init(&tp->cond, NULL);

    tp->submit = impl_submit;
    tp->parallel_for = impl_parallel_for;
    tp->shutdown = impl_shutdown;
    tp->getPendingCount = impl_getPendingCount;

    tp->workers = (Thread**)malloc(sizeof(Thread*) * num_threads);
    for (int i = 0; i < num_threads; i++) {
        tp->workers[i] = new_Thread(worker_routine, tp);
        tp->workers[i]->start(tp->workers[i]);
    }

    return tp;
}
