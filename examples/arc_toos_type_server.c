/**
 * @file arc_toos_type_server.c
 * @brief ToosType v0.3 웹소켓 서버 (EventLoop + Native Timer 실전 런타임 적용본)
 */

#include "toos_type_ws.h"
#include "db.h"
#include "config.h"
#include "logger.h"
#include "timer.h"
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

static void handle_sigint(int sig) {
    (void)sig;
    if (g_server) g_server->stop(g_server);
    if (g_loop) event_loop_stop(g_loop);
}

static uint64_t get_current_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)(ts.tv_sec) * 1000ULL + (uint64_t)(ts.tv_nsec) / 1000000ULL;
}

static void game_tick_cb(void* user_data) {
    (void)user_data;
    uint64_t now = get_current_ms();
    toos_type_game_tick(&g_game_ctx, now);
}

static void on_ws_open(HttpConnection* conn) {
    LOG_INFO(logger, "[ToosType] TCP Connection Accepted (fd: %d). Waiting for JOIN_GAME.", conn->sock->fd);
}

static void on_ws_message(HttpConnection* conn, const char* msg, size_t len) {
    uint64_t now = get_current_ms();
    toos_type_game_handle_ws_message(&g_game_ctx, conn, msg, len, now);
}

static void on_ws_close(HttpConnection* conn) {
    toos_type_game_remove_player(&g_game_ctx, conn, get_current_ms());
    LOG_INFO(logger, "[ToosType] Client Disconnected.");
}

static bool setup_dummy_schema(DBClient *db) {
    if (!db) return false;

    if (db->sqlQuery(db, "CREATE TABLE IF NOT EXISTS toos_type_categories ("
                     "id INTEGER PRIMARY KEY, "
                     "code TEXT, name_ko TEXT, name_en TEXT, "
                     "sort_order INTEGER, enabled INTEGER);") == 0) return false;

    if (db->sqlQuery(db, "CREATE TABLE IF NOT EXISTS toos_type_sentences ("
                     "id INTEGER PRIMARY KEY, category_id INTEGER, "
                     "difficulty_ko INTEGER, difficulty_en INTEGER, "
                     "ko_text TEXT, en_text TEXT, enabled INTEGER);") == 0) return false;

    if (db->sqlQuery(db, "INSERT INTO toos_type_categories (id, code, name_ko, name_en, sort_order, enabled) "
                     "VALUES (1, 'BASIC', '기본문장', 'Basic', 1, 1);") == 0) return false;

    if (db->sqlQuery(db, "INSERT INTO toos_type_sentences (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled) "
                     "VALUES (101, 1, 1, 1, '안녕, 세상아!', 'Hello, World!', 1);") == 0) return false;

    if (db->sqlQuery(db, "INSERT INTO toos_type_sentences (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled) "
                     "VALUES (102, 1, 1, 1, '타자 연습 시작합니다.', 'Typing practice starts now.', 1);") == 0) return false;

    if (db->sqlQuery(db, "INSERT INTO toos_type_sentences (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled) "
                     "VALUES (103, 1, 1, 1, '포비 고문님 만세.', 'Long live Poby.', 1);") == 0) return false;

    return true;
}

int main() {
    signal(SIGINT, handle_sigint);

    Router* router = NULL;
    int exit_code = 1;

    Config* cfg = new_Config();
    if (!cfg) {
        fprintf(stderr, "[FATAL] Config allocation failed\n");
        return exit_code;
    }
    cfg->load(cfg, "config.ini");

    int port = cfg->getInt(cfg, "port", 8080);
    const char* base_dir = cfg->getString(cfg, "base_dir", ".");
    const char* log_file = cfg->getString(cfg, "log_file", "logs/toostype.log");

    char abs_log[1024];
    int n_log = snprintf(abs_log, sizeof(abs_log), "%s/%s", base_dir, log_file);
    if (n_log < 0 || (size_t)n_log >= sizeof(abs_log)) {
        fprintf(stderr, "[FATAL] Log path too long.\n");
        goto fail_init;
    }

    logger = new_Logger(LOG_LEVEL_DEBUG);
    if (!logger) {
        fprintf(stderr, "[FATAL] Logger allocation failed\n");
        goto fail_init;
    }
    logger->setLogFile(logger, abs_log);

    const char* db_host    = cfg->getString(cfg, "db_host",    "127.0.0.1");
    int         db_port    = cfg->getInt   (cfg, "db_port",    0);
    const char* db_user    = cfg->getString(cfg, "db_user",    "");
    const char* db_pass    = cfg->getString(cfg, "db_pass",    "");
    const char* db_name    = cfg->getString(cfg, "db_name",    ":memory:");
    const char* db_charset = cfg->getString(cfg, "db_charset", "utf8mb4");
    const char* db_type    = cfg->getString(cfg, "db_type",    "SQLITE");

    //SQLITE 연결을 시도하기 전 가장 먼저 엔진 적합성(Backend Compatibility)부터 철저히 검증!
    if (strcmp(db_type, "SQLITE") != 0) {
        LOG_ERROR(logger, "[ToosType] Current repository supports SQLITE only. Found: %s", db_type);
        goto fail_init;
    }

    char resolved_db[512] = {0};
    if (strcmp(db_name, ":memory:") != 0) {
        int n_db = snprintf(resolved_db, sizeof(resolved_db), "%s/%s", base_dir, db_name);
        if (n_db < 0 || (size_t)n_db >= sizeof(resolved_db)) {
            LOG_ERROR(logger, "[ToosType] DB path too long.");
            goto fail_init;
        }
    } else {
        int n_db = snprintf(resolved_db, sizeof(resolved_db), "%s", db_name);
        if (n_db < 0 || (size_t)n_db >= sizeof(resolved_db)) {
            LOG_ERROR(logger, "[ToosType] DB name too long.");
            goto fail_init;
        }
    }

    g_db_client = new_DBClientDirect(db_host, resolved_db, db_user, db_pass, db_port, db_charset, db_type);
    if (!g_db_client) {
        LOG_ERROR(logger, "[ToosType] DBClient creation failed");
        goto fail_init;
    }
    if (!g_db_client->connect(g_db_client)) {
        LOG_ERROR(logger, "[ToosType] DB Connection failed: %s", resolved_db);
        goto fail_init;
    }

    if (strcmp(db_name, ":memory:") == 0) {
        if (!setup_dummy_schema(g_db_client)) {
            LOG_ERROR(logger, "[ToosType] Dummy schema setup failed.");
            goto fail_init;
        }
        LOG_INFO(logger, "[ToosType] Dummy in-memory database schema and seed data loaded successfully.");
    } else {
        LOG_INFO(logger, "[ToosType] Connected to persistent database: %s", resolved_db);
    }

    sqlite3 *native_db = (sqlite3 *)g_db_client->conn;
    if (!native_db) {
        LOG_ERROR(logger, "[ToosType] Native SQLite handle is NULL.");
        goto fail_init;
    }

    g_loop = event_loop_create();
    // 🔴 [마감] EventLoop 생성 실패 시 즉시 중단 방어벽!
    if (!g_loop) {
        LOG_ERROR(logger, "[ToosType] Failed to create EventLoop.");
        goto fail_init;
    }

    router = new_Router(NULL);
    if (!router) {
        LOG_ERROR(logger, "[ToosType] Failed to create Router.");
        goto fail_init;
    }

    router->GET(router, "/ws", WsUpgrade_handler);

    g_server = new_HttpServer(g_loop, router);
    if (!g_server) {
        LOG_ERROR(logger, "[ToosType] Failed to create HttpServer.");
        goto fail_init;
    }

    g_server->on_ws_open = on_ws_open;
    g_server->on_ws_message = on_ws_message;
    g_server->on_ws_close = on_ws_close;

    if (!toos_type_game_context_init(&g_game_ctx, native_db, 1, 1)) {
        LOG_ERROR(logger, "[ToosType] Failed to init GameContext.");
        goto fail_init;
    }

    Timer* tick_timer = new_TimerNamed("ToosTypeTick", 50, true, game_tick_cb, NULL);
    if (!tick_timer) {
        LOG_ERROR(logger, "[ToosType] Failed to create tick_timer.");
        goto fail_init;
    }

    if (!g_loop->addTimer || g_loop->addTimer(g_loop, tick_timer) != 0) {
        LOG_ERROR(logger, "[ToosType] Failed to add tick_timer to EventLoop.");
        RELEASE((Object*)tick_timer);
        goto fail_init;
    }

    if (!tick_timer->start(tick_timer)) {
        LOG_ERROR(logger, "[ToosType] Failed to start tick_timer.");
        g_loop->removeTimer(g_loop, tick_timer);
        RELEASE((Object*)tick_timer);
        goto fail_init;
    }

    RELEASE((Object*)tick_timer);

    if (g_server->listen(g_server, port) != 0) {
        LOG_ERROR(logger, "[ToosType] HttpServer listen failed on port %d", port);
        goto fail_init;
    }

    LOG_INFO(logger, "[ToosType] Server successfully started and listening on port %d", port);

    //EventLoop의 반환값을 캡처하여 정상 종료(0)와 에러 종료(-1)를 명확히 구분하여 exit_code에 반영
    int run_rc = event_loop_run(g_loop);
    exit_code = (run_rc == 0) ? 0 : 1;

fail_init:
    toos_type_game_context_deinit(&g_game_ctx);
    if (g_server)
        RELEASE((Object*)g_server);
    if (router)
        RELEASE((Object*)router);
    if (g_loop)
        RELEASE((Object*)g_loop);
    if (g_db_client)
        RELEASE((Object*)g_db_client);
    if (cfg)
        RELEASE((Object*)cfg);
    if (logger)
        RELEASE((Object*)logger);

    return exit_code;
}