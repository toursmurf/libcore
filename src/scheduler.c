/*
 * ────────────────────────────────────────────────────────────────────────
 * [scheduler.c] 투스it홀딩스 제국 - 고정밀 타이머 스케줄러 엔진
 * ────────────────────────────────────────────────────────────────────────
 * 1. 암시적 선언 경고 소각 : event_loop_add_timer 등 글로벌 함수 호출 제거 ✅
 * 2. VTable 다형성 맵핑    : 구형 EventLoop 종속성(addTimer/removeTimer) 완전 철거 ✅
 * 3. 생명주기 오너십       : Job 및 Timer의 정확한 RETAIN/RELEASE 락온 ✅
 * ────────────────────────────────────────────────────────────────────────
 */

#include "scheduler.h"
#include "logger.h"
#include "threadpool.h"
#include "event_loop.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern Logger *logger;
#undef LOG_D
#undef LOG_I
#undef LOG_E
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)

/* [INTERNAL] Classes & Callbacks */
static void ScheduleJob_finalize(Object* obj) {
    ScheduleJob* self = (ScheduleJob*)obj;
    if (self->timer) {
        RELEASE((Object*)self->timer);
        self->timer = NULL;
    }
    LOG_D("[JOB] Finalized: '%s'", self->name);
}

static Class Job_Class = {
    .name = "ScheduleJob",
    .size = sizeof(ScheduleJob),
    .finalize = ScheduleJob_finalize
};

static void* schedule_job_worker_bridge(void* arg) {
    ScheduleJob* job = (ScheduleJob*)arg;
    job->last_run = time(NULL);
    atomic_fetch_add(&job->run_count, 1);

    if (job->callback) {
        job->callback(job->user_data);
    }

    RELEASE((Object*)job);
    return NULL;
}

static void on_scheduler_timer_tick(void* ud) {
    ScheduleJob* job = (ScheduleJob*)ud;
    Scheduler* sched = job->scheduler;
    RETAIN((Object*)job);

    // 🚨 threadpool.h 설계도 규격: pool->submit(pool, func, arg) ✅
    if (sched->pool && sched->pool->submit) {
        sched->pool->submit(sched->pool, (TaskRoutine)schedule_job_worker_bridge, job);
    } else {
        LOG_E("[SCHED] Submit failed!");
        RELEASE((Object*)job);
    }
}

static void Scheduler_finalize(Object* obj) {
    Scheduler* self = (Scheduler*)obj;

    // 1. 동기화 시작
    pthread_mutex_lock(&self->lock);

    // 2. 내부 작업(Job)들 연쇄 석방
    if (self->jobs) {
        int count = self->jobs->getSize(self->jobs);
        for (int i = 0; i < count; i++) {
            ScheduleJob* job = (ScheduleJob*)self->jobs->get(self->jobs, i);
            /* 🚀 [패치] 구형 V1.0 잔재인 removeTimer 호출부 완전 삭제 */
            // 스케줄러가 잡고 있던 Job 소유권 해제 (-1)
            RELEASE((Object*)job);
        }

        // 리스트 자체를 파괴 (ArrayList 내부 카운트 -1)
        RELEASE((Object*)self->jobs);
        self->jobs = NULL; // 안전을 위한 널링
    }

    // 🚨 [의장님! 이 두 줄이 제국 메모리 해방의 마법입니다] ✅
    // 생성자에서 RETAIN 했던 녀석들을 이제 완전히 놓아줍니다.
    if (self->pool) { RELEASE((Object*)self->pool); self->pool = NULL; }
    if (self->loop) { RELEASE((Object*)self->loop); self->loop = NULL; }

    pthread_mutex_unlock(&self->lock);

    // 3. 동기화 도구 파괴
    pthread_mutex_destroy(&self->lock);

    LOG_I("[SCHED] Scheduler finalized and infrastructure released.");
}

const Class Scheduler_Class = {
    .name = "Scheduler",
    .size = sizeof(Scheduler),
    .finalize = Scheduler_finalize
};

/* [VTABLE IMPLEMENTATION] */
static bool Scheduler_addEx_impl(Scheduler* self, const char* name, long ms, bool repeat, JobPriority prio, TimerCallback cb, void* ud) {
    if (!self || !cb) return false;

    ScheduleJob* job = (ScheduleJob*)calloc(1, sizeof(ScheduleJob));
    if (!job) return false;

    Object_Init((Object*)job, &Job_Class);
    snprintf(job->name, sizeof(job->name), "%s", name ? name : "UnnamedJob");
    job->callback = cb;
    job->user_data = ud;
    job->priority = prio;
    job->scheduler = self;

    job->timer = new_TimerNamed(job->name, ms, repeat, on_scheduler_timer_tick, job);
    if (!job->timer) {
        RELEASE((Object*)job);
        return false;
    }

    pthread_mutex_lock(&self->lock);
    self->jobs->add(self->jobs, (Object*)job);

    /* 🚀 [패치] 구형 V1.0 잔재인 addTimer 호출부 완전 삭제 */

    pthread_mutex_unlock(&self->lock);
    LOG_I("[SCHED] Registered: '%s' (%ldms)", job->name, ms);
    return true;
}

static bool Scheduler_add_impl(Scheduler* self, const char* name, long ms, bool repeat, TimerCallback cb, void* ud) {
    return Scheduler_addEx_impl(self, name, ms, repeat, JOB_PRIO_NORMAL, cb, ud);
}

static bool Scheduler_remove_impl(Scheduler* self, const char* name) {
    if (!self || !name) return false;

    pthread_mutex_lock(&self->lock);
    bool found = false;
    int count = self->jobs->getSize(self->jobs);

    for (int i = 0; i < count; i++) {
        ScheduleJob* job = (ScheduleJob*)self->jobs->get(self->jobs, i);
        if (strcmp(job->name, name) == 0) {
            job->timer->stop(job->timer);

            /* 🚀 [패치] 구형 V1.0 잔재인 removeTimer 호출부 완전 삭제 */
            
            self->jobs->remove(self->jobs, i); 
            RELEASE((Object*)job); 
            found = true; 
            break;
        }
    }
    pthread_mutex_unlock(&self->lock);
    return found;
}

static void Scheduler_start_impl(Scheduler* self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    int count = self->jobs->getSize(self->jobs);
    for (int i = 0; i < count; i++) {
        ScheduleJob* job = (ScheduleJob*)self->jobs->get(self->jobs, i);
        job->timer->start(job->timer);
    }
    pthread_mutex_unlock(&self->lock);
    LOG_I("[SCHED] All jobs heartbeat started.");
}

// [FIX] 전용 개수 반환 함수를 명확하게 정의합니다! ✅
static size_t Scheduler_count_impl(Scheduler* self) {
    if (!self || !self->jobs) return 0;
    pthread_mutex_lock(&self->lock);
    size_t count = (size_t)self->jobs->getSize(self->jobs);
    pthread_mutex_unlock(&self->lock);
    return count;
}

static void Scheduler_stop_impl(Scheduler* self) {
    if (!self) return;
    pthread_mutex_lock(&self->lock);
    int count = self->jobs->getSize(self->jobs);
    for (int i = 0; i < count; i++) {
        ScheduleJob* job = (ScheduleJob*)self->jobs->get(self->jobs, i);
        job->timer->stop(job->timer);
    }
    pthread_mutex_unlock(&self->lock);
}

/* [CONSTRUCTOR] */
Scheduler* new_Scheduler(ThreadPool* pool, EventLoop* loop) {
    if (!pool || !loop) return NULL;
    
    Scheduler* self = (Scheduler*)calloc(1, sizeof(Scheduler));
    if (!self) return NULL;
    
    Object_Init((Object*)self, &Scheduler_Class);
    self->jobs = new_ArrayList(16);
    pthread_mutex_init(&self->lock, NULL);
    
    self->pool = (ThreadPool*)RETAIN((Object*)pool);
    self->loop = (EventLoop*)RETAIN((Object*)loop);
    
    self->add    = Scheduler_add_impl; 
    self->addEx  = Scheduler_addEx_impl;
    self->remove = Scheduler_remove_impl; 
    self->start  = Scheduler_start_impl;
    self->stop   = Scheduler_stop_impl; 
    self->count  = Scheduler_count_impl; 
    
    LOG_I("[SCHED] Imperial Scheduler ready.");
    return self;
}