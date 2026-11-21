#define _GNU_SOURCE
#include "cron_scheduler.h"
#include <limits.h>
#include <stdint.h>
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

static bool cron_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool parse_nonneg_int(const char* s, size_t len, int* out) {
    if (!s || len == 0 || !out) {
        return false;
    }

    int val = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        int digit = (int)(c - '0');
        if (val > (INT_MAX - digit) / 10) {
            return false;
        }
        val = val * 10 + digit;
    }

    *out = val;
    return true;
}

static bool parse_field(const char* field, size_t len, int min_val, int max_val, uint64_t* out_bitmap) {
    if (!field || len == 0 || !out_bitmap || min_val < 0 || max_val > 63 || min_val > max_val) {
        return false;
    }

    uint64_t bitmap = 0;
    const char* cur = field;
    const char* end = field + len;

    while (true) {
        if (cur >= end) {
            return false; /* 후행 쉼표("1,") 거부 */
        }

        const char* comma = (const char*)memchr(cur, ',', (size_t)(end - cur));
        const char* seg_end = comma ? comma : end;
        size_t seg_len = (size_t)(seg_end - cur);

        if (seg_len == 0) {
            return false; /* 선행 및 연속 쉼표(",1", "1,,2") 거부 */
        }

        const char* slash = (const char*)memchr(cur, '/', seg_len);
        const char* base_start = cur;
        size_t base_len = seg_len;
        bool has_slash = false;
        int step = 1;

        if (slash) {
            has_slash = true;
            base_len = (size_t)(slash - cur);
            const char* step_start = slash + 1;
            size_t step_len = (size_t)(seg_end - step_start);

            if (!parse_nonneg_int(step_start, step_len, &step) || step <= 0) {
                return false; /* step 0, 음수, 비숫자, 빈 값, 다중 슬래시 거부 */
            }
        }

        if (base_len == 0) {
            return false; /* 슬래시 앞 기준 값 누락("/5") 거부 */
        }

        int from = 0;
        int to   = 0;

        if (base_len == 1 && base_start[0] == '*') {
            from = min_val;
            to   = max_val;
        } else {
            const char* dash = (const char*)memchr(base_start, '-', base_len);
            if (dash) {
                const char* left_start  = base_start;
                size_t      left_len    = (size_t)(dash - base_start);
                const char* right_start = dash + 1;
                size_t      right_len   = (size_t)((base_start + base_len) - right_start);

                if (!parse_nonneg_int(left_start, left_len, &from) ||
                    !parse_nonneg_int(right_start, right_len, &to)) {
                    return false; /* "-5", "5-", "1-2-3", 비숫자 거부 */
                }
                if (from < min_val || to > max_val || from > to) {
                    return false; /* 범위 초과 및 역전 범위("5-2") 거부 */
                }
            } else {
                if (has_slash) {
                    return false; /* 단일 값에 대한 "N/step" 거부 (전체 또는 N-M만 허용) */
                }
                if (!parse_nonneg_int(base_start, base_len, &from)) {
                    return false;
                }
                if (from < min_val || from > max_val) {
                    return false;
                }
                to = from;
            }
        }

        for (int i = from; ; i += step) {
            bitmap |= (1ULL << i);
            if (step > to - i) {
                break;
            }
        }

        if (!comma) {
            break;
        }
        cur = comma + 1;
    }

    if (bitmap == 0) {
        return false;
    }

    *out_bitmap = bitmap;
    return true;
}

static bool parse_cron_expr(const char* expr, CronJob* job) {
    if (!expr || !job) {
        return false;
    }

    const char* fields[6];
    size_t      lens[6];
    int         count = 0;
    const char* p     = expr;

    while (*p) {
        while (cron_space(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (count >= 6) {
            return false; /* 7개 이상 필드 거부 */
        }
        fields[count] = p;
        while (*p != '\0' && !cron_space(*p)) {
            p++;
        }
        lens[count] = (size_t)(p - fields[count]);
        count++;
    }

    if (count != 6) {
        return false;
    }

    uint64_t sec = 0, min = 0, hour = 0, day = 0, mon = 0, wday = 0;
    if (!parse_field(fields[0], lens[0], 0, 59, &sec)  ||
        !parse_field(fields[1], lens[1], 0, 59, &min)  ||
        !parse_field(fields[2], lens[2], 0, 23, &hour) ||
        !parse_field(fields[3], lens[3], 1, 31, &day)  ||
        !parse_field(fields[4], lens[4], 1, 12, &mon)  ||
        !parse_field(fields[5], lens[5], 0,  6, &wday)) {
        return false;
    }

    if (sec == 0 || min == 0 || hour == 0 || day == 0 || mon == 0 || wday == 0) {
        return false;
    }

    job->seconds  = sec;
    job->minutes  = min;
    job->hours    = (uint32_t)hour;
    job->days     = (uint32_t)day;
    job->months   = (uint16_t)mon;
    job->weekdays = (uint8_t)wday;

    return true;
}

static bool cron_matches(const CronJob* job, const struct tm* t) {
    if (!job || !t) {
        return false;
    }
    if (t->tm_sec < 0 || t->tm_sec > 59 ||
        t->tm_min < 0 || t->tm_min > 59 ||
        t->tm_hour < 0 || t->tm_hour > 23 ||
        t->tm_mday < 1 || t->tm_mday > 31 ||
        t->tm_mon < 0 || t->tm_mon > 11 ||
        t->tm_wday < 0 || t->tm_wday > 6) {
        return false;
    }

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
    time_t last_sec = (time_t)-1;

    while (true) {
        time_t now = time(NULL);

        pthread_mutex_lock(&self->lock);
        if (!self->running) {
            pthread_mutex_unlock(&self->lock);
            break;
        }

        if (now != last_sec) {
            last_sec = now;
            struct tm t;

            if (localtime_r(&now, &t) != NULL) {
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

                            pthread_t      tid;
                            pthread_attr_t attr;

                            if (pthread_attr_init(&attr) != 0) {
                                free(fire);
                                continue;
                            }

                            if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0 ||
                                pthread_create(&tid, &attr, fire_callback_thread, fire) != 0) {
                                free(fire);
                            }

                            pthread_attr_destroy(&attr);
                        }
                    }
                }
            }
        }

        pthread_mutex_unlock(&self->lock);
        usleep(200000);
    }

    return NULL;
}

static bool CronScheduler_addCron(CronScheduler* self, const char* name, const char* expr, TimerCallback cb, void* ud) {
    if (!self || !name || !expr || !cb) {
        return false;
    }

    CronJob temp;
    memset(&temp, 0, sizeof(CronJob));

    size_t name_len = strlen(name);
    if (name_len >= sizeof(temp.name)) {
        name_len = sizeof(temp.name) - 1;
    }
    memcpy(temp.name, name, name_len);
    temp.name[name_len] = '\0';
    temp.cb = cb;
    temp.ud = ud;

    if (!parse_cron_expr(expr, &temp)) {
        return false;
    }

    pthread_mutex_lock(&self->lock);

    if (self->job_count >= self->job_capacity) {
        const size_t max_by_size_t = SIZE_MAX / sizeof(CronJob);
        const int max_cap = (max_by_size_t < (size_t)INT_MAX) ? (int)max_by_size_t : INT_MAX;

        if (self->job_capacity >= max_cap) {
            pthread_mutex_unlock(&self->lock);
            return false;
        }

        int new_cap = (self->job_capacity <= 0)
            ? 16
            : ((self->job_capacity > max_cap / 2) ? max_cap : (self->job_capacity * 2));

        CronJob* new_jobs = (CronJob*)realloc(self->jobs, sizeof(CronJob) * (size_t)new_cap);
        if (!new_jobs) {
            pthread_mutex_unlock(&self->lock);
            return false;
        }

        self->jobs         = new_jobs;
        self->job_capacity = new_cap;
    }

    self->jobs[self->job_count++] = temp;
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
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);
    if (self->running) {
        pthread_mutex_unlock(&self->lock);
        return;
    }

    self->running = true;
    if (pthread_create(&self->cron_worker, NULL, cron_worker, self) != 0) {
        self->running = false;
        pthread_mutex_unlock(&self->lock);
        return;
    }
    pthread_mutex_unlock(&self->lock);
}

static void CronScheduler_stop(CronScheduler* self) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);
    if (!self->running) {
        pthread_mutex_unlock(&self->lock);
        return;
    }

    self->running = false;
    pthread_t worker = self->cron_worker;
    pthread_mutex_unlock(&self->lock);

    pthread_join(worker, NULL);
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

    if (pthread_mutex_init(&self->lock, NULL) != 0) {
        free(self);
        return NULL;
    }

    Object_Init((Object*)self, &_cronSchedulerClass);

    self->job_capacity = 16;
    self->jobs = (CronJob*)calloc((size_t)self->job_capacity, sizeof(CronJob));

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