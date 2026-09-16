#include "toos_type_ws.h"
#include "toos_type_game_context.h"
#include "db.h"
#include "config.h"
#include "logger.h"
#include "timer.h"
#include "http_server.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern Logger* logger;

EventLoop* g_loop = NULL;
HttpServer* g_server = NULL;
ToosTypeGameContext g_game_ctx;
DBClient* g_db_client = NULL;

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_stop_requested = 1;
}

static uint64_t get_current_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)(ts.tv_sec) * 1000ULL + (uint64_t)(ts.tv_nsec) / 1000000ULL;
}

static void game_tick_cb(void* user_data) {
    (void)user_data;

    if (g_stop_requested) {
        if (g_server) g_server->stop(g_server);
        if (g_loop) event_loop_stop(g_loop);
        return;
    }

    uint64_t now_ms = get_current_ms();
    if (now_ms != 0) {
        ToosTypeGameContext_tick(&g_game_ctx, now_ms);
    }
}

static bool setup_dummy_schema(DBClient *db) {
    if (!db) return false;
    if (db->sqlQuery(db, "CREATE TABLE IF NOT EXISTS toos_type_categories (id INTEGER PRIMARY KEY, code TEXT, name_ko TEXT, name_en TEXT, sort_order INTEGER, enabled INTEGER);") == 0) return false;
    if (db->sqlQuery(db, "CREATE TABLE IF NOT EXISTS toos_type_sentences (id INTEGER PRIMARY KEY, category_id INTEGER, difficulty_ko INTEGER, difficulty_en INTEGER, ko_text TEXT, en_text TEXT, enabled INTEGER);") == 0) return false;
    if (db->sqlQuery(db, "INSERT INTO toos_type_categories (id, code, name_ko, name_en, sort_order, enabled) VALUES (1, 'BASIC', '기본문장', 'Basic', 1, 1);") == 0) return false;
    if (db->sqlQuery(db, "INSERT INTO toos_type_sentences (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled) VALUES (101, 1, 1, 1, '안녕하세요.', 'Hello.', 1);") == 0) return false;
    if (db->sqlQuery(db, "INSERT INTO toos_type_sentences (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled) VALUES (102, 1, 1, 1, '타자 연습 시작합니다.', 'Typing practice starts now.', 1);") == 0) return false;
    return true;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    Router* router = NULL;
    int exit_code = 1;

    Config* cfg = new_Config();
    if (!cfg) return exit_code;
    if (!cfg->load(cfg, "examples/toostype/app.conf")) goto fail_init;

    int port = cfg->getInt(cfg, "port", 8080);
    if (port < 1 || port > 65535) goto fail_init;

    const char* base_dir = cfg->getString(cfg, "base_dir", ".");
    const char* log_file = cfg->getString(cfg, "log_file", "logs/toostype.log");

    char abs_log[1024];
    int n_log = snprintf(abs_log, sizeof(abs_log), "%s/%s", base_dir, log_file);
    if (n_log < 0 || (size_t)n_log >= sizeof(abs_log)) goto fail_init;

    logger = new_Logger(LOG_LEVEL_DEBUG);
    if (!logger) goto fail_init;
    logger->setLogFile(logger, abs_log);

    const char* db_host = cfg->getString(cfg, "db_host", "127.0.0.1");
    int db_port = cfg->getInt(cfg, "db_port", 0);
    const char* db_user = cfg->getString(cfg, "db_user", "");
    const char* db_pass = cfg->getString(cfg, "db_pass", "");
    const char* db_name = cfg->getString(cfg, "db_name", ":memory:");
    const char* db_charset = cfg->getString(cfg, "db_charset", "utf8mb4");
    const char* db_type = cfg->getString(cfg, "db_type", "SQLITE");
    if (strcmp(db_type, "SQLITE") != 0) goto fail_init;

    char resolved_db[512] = {0};
    int n_db;
    if (strcmp(db_name, ":memory:") != 0) {
        n_db = snprintf(resolved_db, sizeof(resolved_db), "%s/%s", base_dir, db_name);
    } else {
        n_db = snprintf(resolved_db, sizeof(resolved_db), "%s", db_name);
    }
    if (n_db < 0 || (size_t)n_db >= sizeof(resolved_db)) goto fail_init;

    g_db_client = new_DBClientDirect(db_host, resolved_db, db_user, db_pass, db_port, db_charset, db_type);
    if (!g_db_client || !g_db_client->connect(g_db_client)) goto fail_init;

    if (strcmp(db_name, ":memory:") == 0) {
        if (!setup_dummy_schema(g_db_client)) goto fail_init;
    }

    sqlite3 *native_db = (sqlite3 *)g_db_client->conn;
    if (!native_db) goto fail_init;

    ToosTypeGameProfile profile;
    memset(&profile, 0, sizeof(profile));

    // [패치] 카운트다운 5초 고정
    int raw_countdown = cfg->getInt(cfg, "countdown_ms", 5000);
    if (raw_countdown <= 0) goto fail_init;
    profile.countdown_ms = (uint32_t)raw_countdown;

    int raw_play_score = cfg->getInt(cfg, "play_score_per_sec", 2);
    if (raw_play_score < 0) goto fail_init;
    profile.play_score_per_sec = (uint32_t)raw_play_score;

    int raw_acc_mult = cfg->getInt(cfg, "accuracy_score_multiplier", 2);
    if (raw_acc_mult < 0) goto fail_init;
    profile.accuracy_score_multiplier = (uint32_t)raw_acc_mult;

    int raw_seed = cfg->getInt(cfg, "random_seed", 0);
    if (raw_seed < 0) goto fail_init;

    uint64_t seed = (uint64_t)raw_seed;
    if (seed == 0) {
        seed = (uint64_t)time(NULL) ^ get_current_ms();
        if (seed == 0) seed = 0x9E3779B97F4A7C15ULL;
    }
    profile.random_seed = seed;

    // [핵심 패치] Phase 4 유령 완전 삭제 및 3 Phase 안전 주입
    int p_to[TOOS_TYPE_MAX_PHASES];
    int p_score[TOOS_TYPE_MAX_PHASES];

    p_to[0] = cfg->getInt(cfg, "phase1_timeout_ms", 30000);
    p_to[1] = cfg->getInt(cfg, "phase2_timeout_ms", 20000);
    p_to[2] = cfg->getInt(cfg, "phase3_timeout_ms", 15000);

    p_score[0] = cfg->getInt(cfg, "phase1_score", 1);
    p_score[1] = cfg->getInt(cfg, "phase2_score", 2);
    p_score[2] = cfg->getInt(cfg, "phase3_score", 3);

    for (uint32_t i = 0; i < TOOS_TYPE_MAX_PHASES; i++) {
        if (p_to[i] <= 0 || p_score[i] < 0) goto fail_init;

        // 기본 3분 세팅 (60초 x 3)
        profile.phases[i].duration_ms = 60000;
        profile.phases[i].input_timeout_ms = (uint32_t)p_to[i];
        profile.phases[i].correct_score = (uint32_t)p_score[i];
    }

    ToosTypeGameContext_init(&g_game_ctx, native_db, 1, &profile);

    g_loop = event_loop_create();
    if (!g_loop) goto fail_init;

    router = new_Router(NULL);
    if (!router) goto fail_init;

    router->GET(router, "/ws", WsUpgrade_handler);

    g_server = new_HttpServer(g_loop, router);
    if (!g_server) goto fail_init;

    toos_type_ws_bind(g_server, &g_game_ctx);

    Timer* tick_timer = new_TimerNamed("ToosTypeTick", 50, true, game_tick_cb, NULL);
    if (!tick_timer) goto fail_init;

    if (!g_loop->addTimer || g_loop->addTimer(g_loop, tick_timer) != 0) {
        RELEASE((Object*)tick_timer); goto fail_init;
    }

    if (!tick_timer->start(tick_timer)) {
        g_loop->removeTimer(g_loop, tick_timer); RELEASE((Object*)tick_timer); goto fail_init;
    }
    RELEASE((Object*)tick_timer);

    if (g_server->listen(g_server, port) != 0) goto fail_init;

    LOG_INFO(logger, "[ToosType v1.3] Server successfully started and listening on port %d", port);

    int run_rc = event_loop_run(g_loop);
    exit_code = (run_rc == 0) ? 0 : 1;

fail_init:
    ToosTypeGameContext_deinit(&g_game_ctx);

    HttpServer* srv_ptr = g_server;
    g_server = NULL;
    if (srv_ptr) RELEASE((Object*)srv_ptr);
    Router* r_ptr = router;
    router = NULL;
    if (r_ptr) RELEASE((Object*)r_ptr);
    EventLoop* lp_ptr = g_loop;
    g_loop = NULL;
    if (lp_ptr) RELEASE((Object*)lp_ptr);
    DBClient* db_ptr = g_db_client;
    g_db_client = NULL;
    if (db_ptr) RELEASE((Object*)db_ptr);
    if (cfg) RELEASE((Object*)cfg);
    if (logger) RELEASE((Object*)logger);

    return exit_code;
}