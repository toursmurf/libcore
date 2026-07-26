#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "object.h"
#include "arraylist.h"
#include "threadpool.h"
#include "event_loop.h"
#include "timer.h"
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>

extern const Class Scheduler_Class;

typedef enum {
    JOB_PRIO_LOW    = 0,
    JOB_PRIO_NORMAL = 1,
    JOB_PRIO_HIGH   = 2,
    JOB_PRIO_URGENT = 3
} JobPriority;

typedef struct ScheduleJob ScheduleJob;
typedef struct Scheduler Scheduler;

struct ScheduleJob {
    Object          base;
    char            name[64];
    Timer*          timer;      // [OWNED]
    TimerCallback   callback;
    void*           user_data;  // [BORROWED]
    Scheduler*      scheduler;  // [Check 1] 지휘부 참조 필드 추가!!
    atomic_size_t   run_count;
    time_t          last_run;
    JobPriority     priority;
};

struct Scheduler {
    Object          base;
    ArrayList*      jobs;       // [OWNED]
    pthread_mutex_t lock;
    ThreadPool*     pool;       // [BORROWED]
    EventLoop*      loop;       // [BORROWED]

    bool (*add)     (Scheduler* self, const char* name, long ms, bool repeat, TimerCallback cb, void* ud);
    bool (*addEx)   (Scheduler* self, const char* name, long ms, bool repeat, JobPriority prio, TimerCallback cb, void* ud);
    bool (*remove)  (Scheduler* self, const char* name);
    void (*start)   (Scheduler* self);
    void (*stop)    (Scheduler* self);
    size_t (*count) (Scheduler* self);
};

Scheduler* new_Scheduler(ThreadPool* pool, EventLoop* loop);

#endif // SCHEDULER_H