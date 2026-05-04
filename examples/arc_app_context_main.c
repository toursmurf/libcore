/**
 * @file arc_app_context_main.c
 * @brief 🇰🇷 AppContext를 통한 애플리케이션 생명주기 관리, 의존성 주입(DI), 서비스 레지스트리(Service Registry) 데모입니다.
 * 🇬🇧 Application lifecycle management, Dependency Injection (DI), and Service Registry demo using AppContext.
 * @note  This example strictly follows the ARC memory management rules.
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

// [1] 전역 사령관 및 로거 참조
extern AppContext* g_app;
extern Logger* logger;

// [2] 우아한 퇴근을 위한 시그널 핸들러
void handle_sigint(int sig) {
    (void)sig;
    if (logger) logger->warn(logger, "[SYS] Signal received. Shutting down Empire...");
    if (g_app && g_app->runtime) {
        g_app->runtime->setInt(g_app->runtime, "sys.running", 0);
    }
}

// [3] 서비스로 등록할 테스트 작업 (Scheduler용)
void task_heartbeat(void* ud) {
    (void)ud;
    // AppContext를 통해 현재 앱 이름을 가져와서 출력 (DI 활용 예시)
    String* app_name = g_app->getConfig(g_app, "app.name");
    if (logger) {
        logger->info(logger, "[PULSE] [%s] System is healthy.",
                    app_name ? app_name->value : "Unknown");
    }
}

int main() {
    // ---------------------------------------------------------
    // STEP 1: 최전선 전원 공급 (Logger)
    // ---------------------------------------------------------
    logger = new_Logger(LOG_LEVEL_DEBUG);
    logger->info(logger, "--- Imperial Genesis Integration Test Start ---");

    // 시그널 방패 장착
    signal(SIGINT, handle_sigint);

    // ---------------------------------------------------------
    // STEP 2: 총사령부 건설 (AppContext)
    // ---------------------------------------------------------
    g_app = new_AppContext();
    g_app->init(g_app);

    // ---------------------------------------------------------
    // STEP 3: 제국 설정 주입 (Config)
    // ---------------------------------------------------------
    g_app->setConfig(g_app, "app.name", "Imperial-Sentinel-v1");
    g_app->config->setInt(g_app->config, "server.port", 8080);

    // 런타임 상태 초기화
    g_app->runtime->setInt(g_app->runtime, "sys.running", 1);

    // ---------------------------------------------------------
    // STEP 4: 인프라 서비스 생성 및 등록 (ServiceRegistry / DI)
    // ---------------------------------------------------------
    ThreadPool* pool = new_ThreadPool(2, 1024);
    EventLoop* loop = new_EventLoop(1024);
    Scheduler* sched = new_Scheduler(pool, loop);

    // 서비스 레지스트리에 등록 (이제 어디서든 g_app으로 꺼내 쓸 수 있음!)
    g_app->registerService(g_app, &Scheduler_Class, (Object*)sched);

    // RETAIN/RELEASE 규칙에 따라, Registry가 소유권을 가졌으므로
    // 여기서 생성한 지역 포인터들은 소유권을 내려놓음 (가정: registerService가 RETAIN 함)
    RELEASE((Object*)sched);
    RELEASE((Object*)loop);
    RELEASE((Object*)pool);

    // ---------------------------------------------------------
    // STEP 5: 서비스 활용 (Scheduler 가동)
    // ---------------------------------------------------------
    // 다시 꺼내서 쓰기 (APP_GET_SERVICE 매크로 활용!)
    Scheduler* registered_sched = APP_GET_SERVICE(g_app, Scheduler);
    if (registered_sched) {
        registered_sched->addEx(registered_sched, "Heartbeat", 2000, true,
                                JOB_PRIO_NORMAL, task_heartbeat, NULL);
        registered_sched->start(registered_sched);
    }

    logger->info(logger, "[MAIN] System Sentinel is ACTIVE. Port: %d",
                g_app->config->getInt(g_app->config, "server.port"));

    // ---------------------------------------------------------
    // STEP 6: 메인 루프 (런타임 컨텍스트 감시)
    // ---------------------------------------------------------
    while (g_app->runtime->getInt(g_app->runtime, "sys.running") == 1) {
        // 실제로는 여기서 EventLoop_run 등을 돌릴 수 있습니다.
        // 여기선 시뮬레이션을 위해 짧게 대기하며 루프를 돕니다.
        usleep(500000);
    }

    // ---------------------------------------------------------
    // STEP 7: 대청소 및 퇴근 (0 Bytes Leaks를 향해!)
    // ---------------------------------------------------------
    logger->warn(logger, "[MAIN] Commencing full resource cleanup...");
    g_app->destroy_all(g_app); // Context, Registry, Services(Scheduler 등) 연쇄 파괴
    logger->info(logger, "[MAIN] All system resources cleared. BAAAM!!!");
    RELEASE((Object*)logger); // 로거 최종 소각

    return 0;
}