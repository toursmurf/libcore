#ifndef CRON_SCHEDULER_H
#define CRON_SCHEDULER_H

#include "scheduler.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct CronJob CronJob;
typedef struct CronScheduler CronScheduler;

struct CronJob{
    char     name[64];
    uint64_t seconds;
    uint64_t minutes;
    uint32_t hours;
    uint32_t days;
    uint16_t months;
    uint8_t  weekdays;
    TimerCallback cb;
    void* ud;
};

struct CronScheduler {
    Scheduler       base;
    CronJob* jobs;
    int             job_count;
    int             job_capacity;
    pthread_t       cron_worker;
    volatile bool   running;
    pthread_mutex_t lock;

    bool (*addCron)   (CronScheduler* self, const char* name, const char* expr, TimerCallback cb, void* ud);
    bool (*removeCron)(CronScheduler* self, const char* name);
    void (*start)     (CronScheduler* self);
    void (*stop)      (CronScheduler* self);
};

CronScheduler* new_CronScheduler(void);

#endif // CRON_SCHEDULER_H