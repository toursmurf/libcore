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

static void ScheduleJob_finalize(Object* obj) {
    ScheduleJob* self = (ScheduleJob*)obj;
    if (self->timer) {
        RELEASE((Object*)self->timer);
        self->timer = NULL;
    }
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

    if (sched->pool && sched->pool->submit) {
        sched->pool->submit(sched->pool, (TaskRoutine)schedule_job_worker_bridge, job);
    } else {
        RELEASE((Object*)job);
    }
}

static void Scheduler_finalize(Object* obj) {
    Scheduler* self = (Scheduler*)obj;

    pthread_mutex_lock(&self->lock);

    if (self->jobs) {
        int count = self->jobs->getSize(self->jobs);
        for (int i = 0; i < count; i++) {
            ScheduleJob* job = (ScheduleJob*)self->jobs->get(self->jobs, i);
            if (job->timer) {
                if (self->loop && self->loop->removeTimer) {
                    self->loop->removeTimer(self->loop, job->timer);
                }
            }
        }
        RELEASE((Object*)self->jobs);
        self->jobs = NULL;
    }

    if (self->pool) { RELEASE((Object*)self->pool); self->pool = NULL; }
    if (self->loop) { RELEASE((Object*)self->loop); self->loop = NULL; }

    pthread_mutex_unlock(&self->lock);
    pthread_mutex_destroy(&self->lock);
}

const Class Scheduler_Class = {
    .name = "Scheduler",
    .size = sizeof(Scheduler),
    .finalize = Scheduler_finalize
};

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
    atomic_init(&job->run_count, 0);

    job->timer = new_TimerNamed(job->name, ms, repeat, on_scheduler_timer_tick, job);
    if (!job->timer) {
        RELEASE((Object*)job);
        return false;
    }

    pthread_mutex_lock(&self->lock);

    int before_size = self->jobs->getSize(self->jobs);
    self->jobs->add(self->jobs, (Object*)job);
    int after_size = self->jobs->getSize(self->jobs);

    if (after_size != before_size + 1) {
        pthread_mutex_unlock(&self->lock);
        RELEASE((Object*)job);
        return false;
    }

    if (self->loop && self->loop->addTimer) {
        if (self->loop->addTimer(self->loop, job->timer) != 0) {
            self->jobs->remove(self->jobs, after_size - 1);
            pthread_mutex_unlock(&self->lock);
            RELEASE((Object*)job);
            return false;
        }
    } else {
        self->jobs->remove(self->jobs, after_size - 1);
        pthread_mutex_unlock(&self->lock);
        RELEASE((Object*)job);
        return false;
    }

    pthread_mutex_unlock(&self->lock);
    RELEASE((Object*)job);
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

            if (job->timer) {
                if (self->loop && self->loop->removeTimer) {
                    if (self->loop->removeTimer(self->loop, job->timer) != 0) {
                        pthread_mutex_unlock(&self->lock);
                        return false;
                    }
                }
            }

            self->jobs->remove(self->jobs, i);
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
        if (job->timer) job->timer->start(job->timer);
    }
    pthread_mutex_unlock(&self->lock);
}

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
        if (job->timer) job->timer->stop(job->timer);
    }
    pthread_mutex_unlock(&self->lock);
}

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

    return self;
}