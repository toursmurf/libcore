/*
 * File: examples/arc_toos_pixel_server.c
 * libcore 투스픽셀 서버 — Phase 2 (Zero Defect Final)
 */

#include "libcore.h"
#include "event_loop.h"
#include "http_server.h"
#include "router.h"
#include "logger.h"
#include "config.h"
#include "db.h"
#include "string_obj.h"
#include "hashmap.h"
#include "game_room.h"
#include "game_room_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

static HttpServer*  g_server = NULL;
GameRoom*           g_active_room = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_server) {
        g_server->stop(g_server);
    }
}

static uint64_t get_monotonic_ms_impl(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

uint64_t get_monotonic_ms(void) {
    return get_monotonic_ms_impl();
}

static int load_setting_int(DBClient *db, const char *key, int default_value, int min_value, int max_value) {
    char sql[256];
    int n = snprintf(sql, sizeof(sql), "SELECT value FROM game_settings WHERE key='%s' LIMIT 1;", key);
    if (n < 0 || (size_t)n >= sizeof(sql)) return default_value;

    HashMap *row = db->getRecordFromQuery(db, sql);
    if (!row) return default_value;

    String *s = (String*)row->get(row, "value");
    int result = default_value;

    if (s && s->value) {
        char *end = NULL;
        long v = strtol(s->value, &end, 10);
        if (end != s->value && *end == '\0' && v >= min_value && v <= max_value) {
            result = (int)v;
        }
    }

    RELEASE((Object*)row);
    return result;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    Config* cfg = new_Config();
    if (!cfg) {
        fprintf(stderr, "[FATAL] Config 생성 실패\n");
        return 1;
    }
    if (!cfg->load(cfg, "examples/toospixel/app.conf")) {
        fprintf(stderr, "[WARN] app.conf load failed; using defaults\n");
    }

    int port             = cfg->getInt(cfg, "port", 8888);
    const char* db_file  = cfg->getString(cfg, "db_file", "examples/toospixel/toos_pixel.db");
    const char* log_file = cfg->getString(cfg, "log_file", "logs/toospixel.log");
    const char* base_dir = cfg->getString(cfg, "base_dir", ".");

    char abs_log[1024];
    snprintf(abs_log, sizeof(abs_log), "%s/%s", base_dir, log_file);

    logger = new_Logger(LOG_LEVEL_DEBUG);
    if (!logger) {
        fprintf(stderr, "[FATAL] Logger 생성 실패\n");
        RELEASE((Object*)cfg);
        return 1;
    }
    logger->setLogFile(logger, abs_log);

    DBClient* db = new_DBClientDirect("127.0.0.1", db_file, "", "", 0, "UTF8", "SQLITE");
    if (!db || !db->connect(db)) {
        LOG_ERROR(logger, "SQLite 연결 실패: %s", db_file);
        if (db) RELEASE((Object*)db);
        RELEASE((Object*)logger);
        logger = NULL;
        RELEASE((Object*)cfg);
        return 1;
    }

    int board_width  = load_setting_int(db, "board_width", 64, 8, 2048);
    int board_height = load_setting_int(db, "board_height", 64, 8, 2048);
    LOG_INFO(logger, "DB 설정 로드 완료 (초기 기본값) - 캔버스: %dx%d", board_width, board_height);
    RELEASE((Object*)db);

    g_active_room = new_GameRoom(1, (uint16_t)board_width, (uint16_t)board_height);
    if (!g_active_room) {
        LOG_ERROR(logger, "GameRoom 생성 실패");
        RELEASE((Object*)logger);
        logger = NULL;
        RELEASE((Object*)cfg);
        return 1;
    }

    Router* router = new_Router(NULL);
    if (!router) {
        LOG_ERROR(logger, "Router 생성 실패");
        RELEASE((Object*)g_active_room);
        RELEASE((Object*)logger);
        logger = NULL;
        RELEASE((Object*)cfg);
        return 1;
    }
    router->GET(router, "/ws", WsUpgrade_handler);

    EventLoop* loop = event_loop_create();
    if (!loop) {
        LOG_ERROR(logger, "EventLoop 생성 실패");
        RELEASE((Object*)router);
        RELEASE((Object*)g_active_room);
        RELEASE((Object*)logger);
        logger = NULL;
        RELEASE((Object*)cfg);
        return 1;
    }

    HttpServer* server = new_HttpServer(loop, router);
    if (!server) {
        LOG_ERROR(logger, "HttpServer 생성 실패");
        RELEASE((Object*)loop);
        RELEASE((Object*)router);
        RELEASE((Object*)g_active_room);
        RELEASE((Object*)logger);
        logger = NULL;
        RELEASE((Object*)cfg);
        return 1;
    }
    g_server = server;

    server->on_ws_open    = GameRoomHandler_on_ws_open;
    server->on_ws_message = GameRoomHandler_on_ws_message;
    server->on_ws_close   = GameRoomHandler_on_ws_close;

    if (server->listen(server, port) != 0) {
        LOG_ERROR(logger, "포트 %d 바인딩 실패", port);
        GameRoomHandler_cleanup_sessions(server);
        g_server = NULL;
        RELEASE((Object*)server);
        RELEASE((Object*)loop);
        RELEASE((Object*)router);
        RELEASE((Object*)g_active_room);
        RELEASE((Object*)logger);
        logger = NULL;
        RELEASE((Object*)cfg);
        return 1;
    }

    LOG_INFO(logger, "ToosPixel Server Phase 2 시작 (0.0.0.0:%d)", port);
    printf("🚀 ToosPixel Server (Zero Defect Final) running on ws://0.0.0.0:%d/ws\n", port);
    printf("Press Ctrl+C to stop gracefully.\n");

    event_loop_run(loop);

    printf("\n🛑 Shutting down ToosPixel Server gracefully...\n");
    GameRoomHandler_cleanup_sessions(server);
    g_server = NULL;

    RELEASE((Object*)server);
    RELEASE((Object*)loop);
    RELEASE((Object*)router);
    RELEASE((Object*)g_active_room);
    RELEASE((Object*)logger);
    logger = NULL;
    RELEASE((Object*)cfg);

    printf("✨ ToosPixel Server shutdown complete. Goodbye!\n");
    return 0;
}