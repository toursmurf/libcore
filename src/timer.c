#include "timer.h"
#include "logger.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

// [FIX] 제국 표준 로거 인스턴스 참조
extern Logger *logger; 

#undef LOG_D
#undef LOG_I
#undef LOG_E
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)


/* ============================================================================
 * 🐧 Linux: timerfd 기반 고정밀 타이머 구현부 (원본 보존)
 * ============================================================================ */
#if defined(__linux__) || defined(__gnu_linux__)

#include <sys/timerfd.h>

static void Timer_finalize(Object* obj) {
    Timer* self = (Timer*)obj;
    if (self->tfd >= 0) {
        LOG_D("[TIMER] Closing fd %d for '%s'", self->tfd, self->name);
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
    if (!self || self->active || self->tfd < 0) return false;

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
        LOG_E("[TIMER] Failed to settime for '%s'", self->name);
        return false;
    }

    self->active = true;
    LOG_I("[TIMER] Started: '%s' (%ld ms, repeat=%s)", 
          self->name, self->interval_ms, self->repeating ? "true" : "false");
    return true;
}

static void Timer_stop(Timer* self) {
    if (!self || !self->active) return;
    struct itimerspec ts = {0}; 
    timerfd_settime(self->tfd, 0, &ts, NULL);
    self->active = false;
    LOG_I("[TIMER] Stopped: '%s'", self->name);
}

static void Timer_reset(Timer* self) { Timer_stop(self); Timer_start(self); }
static bool Timer_isActive(Timer* self) { return self ? self->active : false; }

Timer* new_TimerNamed(const char* name, long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    Timer* self = (Timer*)calloc(1, sizeof(Timer));
    if (!self) return NULL;

    Object_Init((Object*)self, &Timer_Class);
    self->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (self->tfd < 0) { free(self); return NULL; }

    self->interval_ms = interval_ms;
    self->repeating = repeating;
    self->callback = cb;
    self->user_data = user_data;
    
    if (name) snprintf(self->name, sizeof(self->name), "%s", name);
    else snprintf(self->name, sizeof(self->name), "AnonymousTimer");

    self->start = Timer_start;
    self->stop = Timer_stop;
    self->reset = Timer_reset;
    self->isActive = Timer_isActive;

    LOG_D("[TIMER] Created: '%s' (fd=%d)", self->name, self->tfd);
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
 * 🍎 macOS / 🪟 Windows: 독립 Thread 우회 타이머 구현부
 * ============================================================================ */
#else

#include <sys/time.h>

static void Timer_finalize(Object* obj) {
    Timer* self = (Timer*)obj;
    self->active = false;
    self->tfd = -1; // 맥/윈도우에서는 tfd 미사용
    LOG_D("[TIMER] Finalized (Threaded) '%s'", self->name);
}

static Class Timer_Class = {
  .name = "Timer",
  .size = sizeof(Timer),
  .finalize = Timer_finalize
};

/* 🚀 백그라운드 독립 스레드 워커 */
static void* timer_thread_routine(void* arg) {
    Timer* self = (Timer*)arg;
    
    while (self->active) {
        long interval_us = self->interval_ms * 1000;
        // 빠른 stop() 반응을 위해 최대 10ms 단위로 쪼개서 수면
        long sleep_step = (interval_us > 10000) ? 10000 : interval_us; 
        long slept = 0;
        
        while (slept < interval_us && self->active) {
            long to_sleep = interval_us - slept;
            if (to_sleep > sleep_step) to_sleep = sleep_step;
            usleep((useconds_t)to_sleep);
            slept += to_sleep;
        }
        
        if (!self->active) break;
        
        /* 타격(Callback) 발사! */
        if (self->callback) {
            self->callback(self->user_data);
        }
        
        if (!self->repeating) {
            self->active = false;
            break;
        }
    }
    
    /* 🛡️ [ARC 철통 방어] 스레드 종료 시 객체 소유권(Lock-on) 해제! */
    RELEASE((Object*)self);
    return NULL;
}

static bool Timer_start(Timer* self) {
    if (!self || self->active) return false;
    self->active = true;
    
    /* 🛡️ [ARC 철통 방어] 스레드가 도는 동안 타이머 객체가 메모리에서 파괴되지 않도록 보호! */
    RETAIN((Object*)self);
    
    pthread_t tid;
    if (pthread_create(&tid, NULL, timer_thread_routine, self) != 0) {
        self->active = false;
        RELEASE((Object*)self); // 실패 시 즉시 해제
        LOG_E("[TIMER] Failed to spawn thread for '%s'", self->name);
        return false;
    }
    pthread_detach(tid); // 스레드 독립 방사
    
    LOG_I("[TIMER] Started (Threaded): '%s' (%ld ms, repeat=%s)", 
          self->name, self->interval_ms, self->repeating ? "true" : "false");
    return true;
}

static void Timer_stop(Timer* self) {
    if (!self || !self->active) return;
    self->active = false; // 플래그를 내려 스레드가 스스로 종료되도록 유도
    LOG_I("[TIMER] Stopped (Threaded): '%s'", self->name);
}

static void Timer_reset(Timer* self) { Timer_stop(self); Timer_start(self); }
static bool Timer_isActive(Timer* self) { return self ? self->active : false; }

Timer* new_TimerNamed(const char* name, long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    Timer* self = (Timer*)calloc(1, sizeof(Timer));
    if (!self) return NULL;

    Object_Init((Object*)self, &Timer_Class);
    self->tfd = -1; // 독립 스레드 모드이므로 fd 미사용
    self->interval_ms = interval_ms;
    self->repeating = repeating;
    self->callback = cb;
    self->user_data = user_data;
    
    if (name) snprintf(self->name, sizeof(self->name), "%s", name);
    else snprintf(self->name, sizeof(self->name), "AnonymousTimer");

    self->start = Timer_start;
    self->stop = Timer_stop;
    self->reset = Timer_reset;
    self->isActive = Timer_isActive;

    LOG_D("[TIMER] Created (Threaded): '%s'", self->name);
    return self;
}

Timer* new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* user_data) {
    return new_TimerNamed(NULL, interval_ms, repeating, cb, user_data);
}

void on_timer_event(Timer* self) {
    (void)self;
    // 독립 스레드 모드에서는 EventLoop의 수동 호출(Poll)을 사용하지 않으므로 무시합니다.
}

#endif
