/**
 * @file arc_scheduler_system_monitor.c
 * @brief 🇰🇷 Scheduler를 활용하여 주기적으로 시스템 리소스를 모니터링하는 데모입니다.
 * 🇬🇧 Demo monitoring system resources periodically using Scheduler.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include "scheduler.h"
#include "logger.h"
#include "threadpool.h"
#include "event_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

extern Logger *logger;
void event_loop_run(EventLoop* loop);

// [전역 사령부] 시그널 핸들러가 통제할 수 있도록 배치
EventLoop* global_loop = NULL; 

// [로거 매크로]
#undef LOG_D
#undef LOG_I
#undef LOG_W
#undef LOG_E
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) LOG_WARN(logger, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)

// [시그널 핸들러] 우아한 퇴근을 위한 비상 정지 장치
void handle_sigint(int sig) {
    (void)sig;
    if (global_loop) {
        global_loop->is_running = false; 
    }
}

/* ============================================================
 * [실탄 장전] 사라졌던 태스크 함수들 복구 완료! ✅
 * ============================================================ */

// 1. [Normal Prio] 시스템 자원 감시
void task_monitor_resources(void* ud) {
    (void)ud;
    LOG_I("[MONITOR] Checking System Resources...");
    int cpu_usage = rand() % 100;
    int mem_usage = rand() % 100;
    printf("   📊 [STAT] CPU: %d%% | MEM: %d%%\n", cpu_usage, mem_usage);
}

// 2. [High Prio] SNMP 네트워크 장비 폴링
void task_poll_network_devices(void* ud) {
    (void)ud;
    LOG_I("[SNMP] Polling Core Switches (10.0.0.1, 10.0.0.2)...");
    LOG_D("[SNMP] Interface status: ALL UP");
}

// 3. [Urgent Prio] 크리티컬 장애 감시
void task_critical_health_check(void* ud) {
    (void)ud;
    LOG_W("[CHECK] Running Critical Health Check...");
    if ((rand() % 10) > 8) {
        LOG_E("[ALERT] Critical Error Detected! Sending Slack Notification...");
    }
}

/* ============================================================
 * [Main] 시스템 구동부
 * ============================================================ */

int main() {
    // 1. 제국 표준 sigaction 장착 (SA_RESTART 무효화)
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, NULL);

    // 2. 로거 가동
    logger = new_Logger(LOG_LEVEL_DEBUG);

    LOG_I("====================================================");
    LOG_I("    Imperial Infrastructure Sentinel v1.1 Standard  ");
    LOG_I("====================================================");

    // 3. 인프라 준비
    ThreadPool* pool = new_ThreadPool(4, 1024);
    global_loop = new_EventLoop(1024);
    Scheduler* sched = new_Scheduler(pool, global_loop);

    // 4. 임무 스케줄링 (이름, 주기(ms), 반복, 우선순위, 콜백)
    sched->addEx(sched, "Resource-Monitor", 10000, true, JOB_PRIO_NORMAL, task_monitor_resources, NULL);
    sched->addEx(sched, "SNMP-Polling", 5000, true, JOB_PRIO_HIGH, task_poll_network_devices, NULL);
    sched->addEx(sched, "Critical-Check", 2000, true, JOB_PRIO_URGENT, task_critical_health_check, NULL);

    // 5. 가동
    sched->start(sched);
    LOG_I("[SYSTEM] Infrastructure Sentinel is now ACTIVE. Press ^C to STOP.");

    // 6. 이벤트 루프 진입
    event_loop_run(global_loop); 

    // 7. 우아한 자원 회수
    LOG_W("[SYSTEM] Loop broken. Cleaning up resources...");
    printf("   [CLEANUP] Monitor safely deactivated. BAAAM!!!\n");

    if(sched) {RELEASE((Object*)sched); sched=NULL;}
    if(global_loop) { RELEASE((Object*)global_loop); global_loop=NULL;}
    if(pool)  { RELEASE((Object*)pool); pool=NULL;}
    if(logger) { RELEASE((Object*)logger); logger=NULL;}


    return 0;
}
