/**
 * @file arc_app_context_main.c
 * @brief AppContext 생명주기 관리 / DI / ServiceRegistry 데모
 */

#include "app_context.h"
#include "logger.h"
#include "scheduler.h"
#include "threadpool.h"
#include "event_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

extern AppContext* g_app;
extern Logger*     logger;

/* 우아한 종료 핸들러 */
void handle_sigint(int sig) {
    (void)sig;
    if (logger) {
        logger->warn(logger, "[SYS] Signal received. Shutting down Empire...");
    }
    if (g_app && g_app->runtime) {
        g_app->runtime->setInt(g_app->runtime, "sys.running", 0);
    }
}

/* Heartbeat 작업 */
void task_heartbeat(void* ud) {
    (void)ud;
    /* getConfig 반환값은 const char* */
    const char* app_name = g_app->getConfig(g_app, "app.name");
    if (logger) {
        logger->info(logger, "[PULSE] [%s] System is healthy.",
                     app_name ? app_name : "Unknown");
    }
}

int main(void) {
    /* STEP 1: Logger 초기화 */
    logger = new_Logger(LOG_LEVEL_DEBUG);
    if (!logger) {
        return 1;
    }
    logger->info(logger, "--- Imperial Genesis Integration Test Start ---");
    signal(SIGINT, handle_sigint);

    /* STEP 2: AppContext 생성 */
    g_app = new_AppContext();
    if (!g_app) {
        logger->error(logger, "[MAIN] AppContext OOM!!");
        RELEASE((Object*)logger);
        return 1;
    }

    /* STEP 3: 설정 파일 로드 (없으면 기본값 사용) */
    if (!g_app->init(g_app, "app.conf")) {
        logger->warn(logger, "[MAIN] AppContext init failed — running with defaults.");
    }

    /* STEP 4: 런타임 상태 초기화 */
    g_app->runtime->setInt(g_app->runtime, "sys.running", 1);

    /* STEP 5: 인프라 서비스 생성 및 등록 */
    ThreadPool* pool  = new_ThreadPool(2, 1024);
    EventLoop*  loop  = event_loop_create();
    Scheduler*  sched = new_Scheduler(pool, loop);

    if (!pool || !loop || !sched) {
        logger->error(logger, "[MAIN] Infrastructure OOM!!");
        if (sched) RELEASE((Object*)sched);
        if (loop)  RELEASE((Object*)loop);
        if (pool)  RELEASE((Object*)pool);
        g_app->destroy_all(g_app);
        RELEASE((Object*)logger);
        return 1;
    }

    g_app->registerService(g_app, &Scheduler_Class, (Object*)sched);
    RELEASE((Object*)sched);
    RELEASE((Object*)loop);
    RELEASE((Object*)pool);

    /* STEP 6: Scheduler 가동 */
    Scheduler* registered_sched = REG_GET(g_app->reg, Scheduler);
    if (registered_sched) {
        registered_sched->addEx(registered_sched, "Heartbeat", 2000, true,
                                JOB_PRIO_NORMAL, task_heartbeat, NULL);
        registered_sched->start(registered_sched);
    }

    logger->info(logger, "[MAIN] port: %d",
                 g_app->config->getInt(g_app->config, "server.port", 8080));

    /* STEP 7: 메인 루프 */
    while (g_app->runtime->getInt(g_app->runtime, "sys.running") == 1) {
        usleep(500000);
    }

    /* STEP 8: 대청소 */
    logger->warn(logger, "[MAIN] Commencing full resource cleanup...");
    g_app->destroy_all(g_app);
    logger->info(logger, "[MAIN] All resources cleared. BAAAM!!!");
    RELEASE((Object*)logger);

    return 0;
}