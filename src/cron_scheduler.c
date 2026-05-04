#define _GNU_SOURCE
#include "cron_scheduler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    TimerCallback cb;
    void* ud;
} CronFireArg;

static void* fire_callback_thread(void* arg) {
    CronFireArg* fire = (CronFireArg*)arg;

    if (fire->cb) {
        fire->cb(fire->ud);
    }

    free(fire);
    return NULL;
}

static uint64_t parse_field(const char* field, int min_val, int max_val) {
    uint64_t bitmap = 0;

    if (strcmp(field, "*") == 0) {
        for (int i = min_val; i <= max_val; i++) {
            bitmap |= (1ULL << i);
        }
        return bitmap;
    }

    if (field[0] == '*' && field[1] == '/') {
        int step = atoi(field + 2);
        if (step <= 0) {
            step = 1;
        }
        for (int i = min_val; i <= max_val; i += step) {
            bitmap |= (1ULL << i);
        }
        return bitmap;
    }

    char buf[128];
    strncpy(buf, field, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* saveptr = NULL;
    char* token   = strtok_r(buf, ",", &saveptr);

    while (token) {
        char* dash = strchr(token, '-');

        if (dash) {
            int from = atoi(token);
            int to   = atoi(dash + 1);

            if (from < min_val) {
                from = min_val;
            }
            if (to > max_val) {
                to = max_val;
            }

            for (int i = from; i <= to; i++) {
                bitmap |= (1ULL << i);
            }
        } else {
            int val = atoi(token);
            if (val >= min_val && val <= max_val) {
                bitmap |= (1ULL << val);
            }
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    return bitmap;
}

static bool parse_cron_expr(const char* expr, CronJob* job) {
    char buf[256];
    strncpy(buf, expr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* fields[6];
    int   count   = 0;
    char* saveptr = NULL;
    char* token   = strtok_r(buf, " \t", &saveptr);

    while (token && count < 6) {
        fields[count++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    if (count != 6) {
        return false;
    }

    job->seconds  =           parse_field(fields[0],  0, 59);
    job->minutes  =           parse_field(fields[1],  0, 59);
    job->hours    = (uint32_t)parse_field(fields[2],  0, 23);
    job->days     = (uint32_t)parse_field(fields[3],  1, 31);
    job->months   = (uint16_t)parse_field(fields[4],  1, 12);
    job->weekdays = (uint8_t) parse_field(fields[5],  0,  6);

    return true;
}

static bool cron_matches(const CronJob* job, const struct tm* t) {
    if (!(job->seconds & (1ULL << t->tm_sec))) {
        return false;
    }
    if (!(job->minutes & (1ULL << t->tm_min))) {
        return false;
    }
    if (!(job->hours & (1ULL << t->tm_hour))) {
        return false;
    }
    if (!(job->days & (1ULL << t->tm_mday))) {
        return false;
    }
    if (!(job->months & (1ULL << (t->tm_mon + 1)))) {
        return false;
    }
    if (!(job->weekdays & (1ULL << t->tm_wday))) {
        return false;
    }

    return true;
}

static void* cron_worker(void* arg) {
    CronScheduler* self = (CronScheduler*)arg;
    int last_sec = -1;

    while (self->running) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);

        if (t.tm_sec != last_sec) {
            last_sec = t.tm_sec;

            pthread_mutex_lock(&self->lock);

            for (int i = 0; i < self->job_count; i++) {
                CronJob* job = &self->jobs[i];

                if (cron_matches(job, &t)) {
                    if (job->cb) {
                        CronFireArg* fire = (CronFireArg*)malloc(sizeof(CronFireArg));

                        if (!fire) {
                            continue;
                        }

                        fire->cb = job->cb;
                        fire->ud = job->ud;

                        pthread_t       tid;
                        pthread_attr_t  attr;
                        pthread_attr_init(&attr);
                        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                        if (pthread_create(&tid, &attr, fire_callback_thread, fire) != 0) {
                            free(fire);
                        }

                        pthread_attr_destroy(&attr);
                    }
                }
            }

            pthread_mutex_unlock(&self->lock);
        }

        usleep(200000);
    }

    return NULL;
}

static bool CronScheduler_addCron(CronScheduler* self, const char* name, const char* expr, TimerCallback cb, void* ud) {
    if (!self || !name || !expr || !cb) {
        return false;
    }

    pthread_mutex_lock(&self->lock);

    if (self->job_count >= self->job_capacity) {
        int new_cap = self->job_capacity * 2;
        CronJob* new_jobs = (CronJob*)realloc(self->jobs, sizeof(CronJob) * new_cap);

        if (!new_jobs) {
            pthread_mutex_unlock(&self->lock);
            return false;
        }

        self->jobs         = new_jobs;
        self->job_capacity = new_cap;
    }

    CronJob* job = &self->jobs[self->job_count];
    memset(job, 0, sizeof(CronJob));

    strncpy(job->name, name, sizeof(job->name) - 1);
    job->cb = cb;
    job->ud = ud;

    if (!parse_cron_expr(expr, job)) {
        pthread_mutex_unlock(&self->lock);
        return false;
    }

    self->job_count++;
    pthread_mutex_unlock(&self->lock);

    return true;
}

static bool CronScheduler_removeCron(CronScheduler* self, const char* name) {
    if (!self || !name) {
        return false;
    }

    pthread_mutex_lock(&self->lock);

    for (int i = 0; i < self->job_count; i++) {
        if (strcmp(self->jobs[i].name, name) == 0) {
            self->jobs[i] = self->jobs[self->job_count - 1];
            self->job_count--;
            pthread_mutex_unlock(&self->lock);
            return true;
        }
    }

    pthread_mutex_unlock(&self->lock);
    return false;
}

static void CronScheduler_start(CronScheduler* self) {
    if (!self || self->running) {
        return;
    }

    self->running = true;
    pthread_create(&self->cron_worker, NULL, cron_worker, self);
}

static void CronScheduler_stop(CronScheduler* self) {
    if (!self || !self->running) {
        return;
    }

    self->running = false;
    pthread_join(self->cron_worker, NULL);
}

static void CronScheduler_finalize(Object* obj) {
    CronScheduler* self = (CronScheduler*)obj;
    CronScheduler_stop(self);

    if (self->jobs) {
        free(self->jobs);
        self->jobs = NULL;
    }

    pthread_mutex_destroy(&self->lock);
}

static const Class _cronSchedulerClass = {
    .name     = "CronScheduler",
    .size     = sizeof(CronScheduler),
    .finalize = CronScheduler_finalize
};

CronScheduler* new_CronScheduler(void) {
    CronScheduler* self = (CronScheduler*)calloc(1, sizeof(CronScheduler));
    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &_cronSchedulerClass);

    pthread_mutex_init(&self->lock, NULL);

    self->job_capacity = 16;
    self->jobs = (CronJob*)calloc(self->job_capacity, sizeof(CronJob));

    if (!self->jobs) {
        RELEASE((Object*)self);
        return NULL;
    }

    self->job_count = 0;
    self->running   = false;

    self->addCron    = CronScheduler_addCron;
    self->removeCron = CronScheduler_removeCron;
    self->start      = CronScheduler_start;
    self->stop       = CronScheduler_stop;

    return self;
}