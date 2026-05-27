#include "scheduler.h"
#include "logger.h"
#include "threadpool.h"
#include "event_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

extern Logger *logger;
EventLoop* global_loop = NULL;

#undef LOG_D
#undef LOG_I
#undef LOG_W
#undef LOG_E
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) LOG_WARN(logger, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)

/* [Signal Handler] 시스템 콜 재시작 방지형 sigaction 핸들러 ✅ */
void handle_sigint(int sig) {
    (void)sig;
    if (global_loop) {
        global_loop->is_running = false; // volatile 필드 타격!
    }
}

static void task_pulse_cb(void* ud) { (void)ud; static int count = 0; LOG_D("[PULSE] System Heartbeat #%d", ++count); }
static void task_resource_check(void* ud) { (void)ud; LOG_I("[SCHED] Storage Integrity Scan... OK."); }

int main() {
    // 1. Sigaction 설정 (SA_RESTART 미사용!!) ✅
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // 2. 인프라 가동
    logger = new_Logger(LOG_LEVEL_DEBUG);
    ThreadPool* pool = new_ThreadPool(4, 1024);
    global_loop = new_EventLoop(1024);
    Scheduler* sched = new_Scheduler(pool, global_loop);

    // 3. 임무 배치
    Timer* pulse = new_TimerNamed("Pulse", 500, true, task_pulse_cb, NULL);
    if (global_loop->addTimer) {
      global_loop->addTimer(global_loop, pulse);
    }
    pulse->start(pulse);
    sched->add(sched, "Resource-Check", 2000, true, task_resource_check, NULL);

    sched->start(sched);
    LOG_I("[SYSTEM] Sentinel ACTIVE. Press ^C to STOP.");

    // 4. 가동 및 정지 (이제 ^C가 완벽히 먹힙니다!)
    event_loop_run(global_loop);

    // 5. 우아한 퇴근
    LOG_W("[SYSTEM] Loop broken. Cleaning up resources...");

    if (global_loop->removeTimer) {
      global_loop->removeTimer(global_loop, pulse);
    }

    RELEASE((Object*)pulse);
    RELEASE((Object*)sched);
    RELEASE((Object*)global_loop);
    RELEASE((Object*)pool);
    RELEASE((Object*)logger);

    printf("   [CLEANUP] Sentinel safely deactivated. BAAAM!!!\n");

    // 1. 스케줄러를 먼저 파괴 (내부에서 pool과 loop를 RELEASE함)
    if (sched) RELEASE((Object*)sched);

    // 2. main이 소유했던 참조권을 마저 해제 ✅
    if (global_loop)  RELEASE((Object*)global_loop);
    if (pool)  RELEASE((Object*)pool);

    // 3. 로거 소각
    if (logger) RELEASE((Object*)logger);
    return 0;
}
