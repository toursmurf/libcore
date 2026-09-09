#include "timer.h"
#include "logger.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

extern Logger *logger;

#undef LOG_D
#undef LOG_I
#undef LOG_E
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)

/* ============================================================================
 * Linux: timerfd 기반 고정밀 타이머 구현부
 * ============================================================================ */
#if defined(__linux__) || defined(__gnu_linux__)

#include <sys/timerfd.h>

static void Timer_finalize(Object* obj) {
    Timer* self = (Timer*)obj;
    if (self->tfd >= 0) {
        close(self->tfd);
        self->tfd = -1;
    }
    self->active = false;
}

static Class Timer_Class = {
  .name = "Timer",
  .size = sizeof(Timer),
  .finalize = Timer_finalize
};

static bool Timer_start(Timer* self) {
    if (!self || self->active || self->tfd < 0 || !self->platform_data) return false;

    struct itimerspec ts;
    long sec = self->interval_ms / 1000;
    long nsec = (self->interval_ms % 1000) * 1000000;

    ts.it_value.tv_sec = sec;
    ts.it_value.tv_nsec = nsec;

    if (self->repeating) {
        ts.it_interval.tv_sec = sec;
        ts.it_interval.tv_nsec = nsec;
    } else {
        ts.it_interval.tv_sec = 0;
        ts.it_interval.tv_nsec = 0;
    }

    if (timerfd_settime(self->tfd, 0, &ts, NULL) == -1) {
        return false;
    }

    self->active = true;
    return true;
}

static void Timer_stop(Timer* self) {
    if (!self || !self->active) return;
    struct itimerspec ts = {0};
    if (timerfd_settime(self->tfd, 0, &ts, NULL) == 0) {
        self->active = false;
    }
}

static void Timer_reset(Timer* self) { Timer_stop(self); Timer_start(self); }
static bool Timer_isActive(Timer* self) { return self ? self->active : false; }

Timer* new_TimerNamed(const char* name, long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    if (interval_ms <= 0) return NULL;

    Timer* self = (Timer*)calloc(1, sizeof(Timer));
    if (!self) return NULL;

    Object_Init((Object*)self, &Timer_Class);
    self->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (self->tfd < 0) { free(self); return NULL; }

    self->interval_ms = interval_ms;
    self->repeating = repeating;
    self->callback = cb;
    self->user_data = user_data;
    self->platform_data = NULL;

    if (name) snprintf(self->name, sizeof(self->name), "%s", name);
    else snprintf(self->name, sizeof(self->name), "AnonymousTimer");

    self->start = Timer_start;
    self->stop = Timer_stop;
    self->reset = Timer_reset;
    self->isActive = Timer_isActive;

    return self;
}

Timer* new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    return new_TimerNamed(NULL, interval_ms, repeating, cb, user_data);
}

void on_timer_event(Timer* self) {
    if (!self || self->tfd < 0) return;
    uint64_t exp;
    ssize_t s = read(self->tfd, &exp, sizeof(uint64_t));
    if (s == sizeof(uint64_t) && self->callback) self->callback(self->user_data);
    if (!self->repeating) self->active = false;
}

/* ============================================================================
 * macOS / *BSD: kqueue 기반 고정밀 타이머 구현부 (순수 네이티브)
 * ============================================================================ */
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

#include <sys/time.h>

extern bool kqueue_update_timer(Timer* timer, bool active);

static void Timer_finalize(Object* obj) {
    Timer* self = (Timer*)obj;
    if (self->active) {
        kqueue_update_timer(self, false);
        self->active = false;
    }
    self->tfd = -1;
}

static Class Timer_Class = {
  .name = "Timer",
  .size = sizeof(Timer),
  .finalize = Timer_finalize
};

static bool Timer_start(Timer* self) {
    if (!self || self->active || !self->platform_data) return false;

    if (!kqueue_update_timer(self, true)) return false;

    self->active = true;
    return true;
}

static void Timer_stop(Timer* self) {
    if (!self || !self->active) return;
    if (kqueue_update_timer(self, false)) {
        self->active = false;
    }
}

static void Timer_reset(Timer* self) { Timer_stop(self); Timer_start(self); }
static bool Timer_isActive(Timer* self) { return self ? self->active : false; }

Timer* new_TimerNamed(const char* name, long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    if (interval_ms <= 0) return NULL;

    Timer* self = (Timer*)calloc(1, sizeof(Timer));
    if (!self) return NULL;

    Object_Init((Object*)self, &Timer_Class);
    self->tfd = -1;
    self->interval_ms = interval_ms;
    self->repeating = repeating;
    self->callback = cb;
    self->user_data = user_data;
    self->platform_data = NULL;

    if (name) snprintf(self->name, sizeof(self->name), "%s", name);
    else snprintf(self->name, sizeof(self->name), "AnonymousTimer");

    self->start = Timer_start;
    self->stop = Timer_stop;
    self->reset = Timer_reset;
    self->isActive = Timer_isActive;

    return self;
}

Timer* new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    return new_TimerNamed(NULL, interval_ms, repeating, cb, user_data);
}

void on_timer_event(Timer* self) {
    if (!self || !self->active) return;
    if (self->callback) self->callback(self->user_data);
    if (!self->repeating) self->active = false;
}

/* ============================================================================
 * Windows: 독립 Thread 우회 타이머 구현부 (원본 보존)
 * ============================================================================ */
#else

#include <sys/time.h>

static void Timer_finalize(Object* obj) {
    Timer* self = (Timer*)obj;
    self->active = false;
    self->tfd = -1;
}

static Class Timer_Class = {
  .name = "Timer",
  .size = sizeof(Timer),
  .finalize = Timer_finalize
};

static void* timer_thread_routine(void* arg) {
    Timer* self = (Timer*)arg;

    while (self->active) {
        long interval_us = self->interval_ms * 1000;
        long sleep_step = (interval_us > 10000) ? 10000 : interval_us;
        long slept = 0;

        while (slept < interval_us && self->active) {
            long to_sleep = interval_us - slept;
            if (to_sleep > sleep_step) to_sleep = sleep_step;
            usleep((useconds_t)to_sleep);
            slept += to_sleep;
        }

        if (!self->active) break;

        if (self->callback) {
            self->callback(self->user_data);
        }

        if (!self->repeating) {
            self->active = false;
            break;
        }
    }

    RELEASE((Object*)self);
    return NULL;
}

static bool Timer_start(Timer* self) {
    if (!self || self->active) return false;
    self->active = true;

    RETAIN((Object*)self);

    pthread_t tid;
    if (pthread_create(&tid, NULL, timer_thread_routine, self) != 0) {
        self->active = false;
        RELEASE((Object*)self);
        return false;
    }
    pthread_detach(tid);
    return true;
}

static void Timer_stop(Timer* self) {
    if (!self || !self->active) return;
    self->active = false;
}

static void Timer_reset(Timer* self) { Timer_stop(self); Timer_start(self); }
static bool Timer_isActive(Timer* self) { return self ? self->active : false; }

Timer* new_TimerNamed(const char* name, long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    if (interval_ms <= 0) return NULL;

    Timer* self = (Timer*)calloc(1, sizeof(Timer));
    if (!self) return NULL;

    Object_Init((Object*)self, &Timer_Class);
    self->tfd = -1;
    self->interval_ms = interval_ms;
    self->repeating = repeating;
    self->callback = cb;
    self->user_data = user_data;
    self->platform_data = NULL;

    if (name) snprintf(self->name, sizeof(self->name), "%s", name);
    else snprintf(self->name, sizeof(self->name), "AnonymousTimer");

    self->start = Timer_start;
    self->stop = Timer_stop;
    self->reset = Timer_reset;
    self->isActive = Timer_isActive;

    return self;
}

Timer* new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    return new_TimerNamed(NULL, interval_ms, repeating, cb, user_data);
}

void on_timer_event(Timer* self) {
    (void)self;
}

#endif