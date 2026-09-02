/*
 * File: examples/arc_board_server.c
 * libcore 게시판 서버 — Composition Root (v1.7.2 Final + Limit Config + Upload Size Limit)
 */

#include "libcore.h"
#include "board_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

/* ── 전역 ── */
extern Logger*    logger;
static HttpServer* g_server = NULL;

/* ── 시그널 ── */
static void on_signal(int sig) {
    (void)sig;
    if (g_server) g_server->stop(g_server);
}

/* ── main ── */
int main(void) {
    signal(SIGPIPE, SIG_IGN);

    /* Config 먼저 로드! (로그 경로 합성을 위해) */
    Config* cfg = new_Config();
    if (!cfg) {
        fprintf(stderr, "[FATAL] Config 실패\n");
        return 1;
    }
    cfg->load(cfg, "examples/board/app.conf");

    int         port       = cfg->getInt   (cfg, "port",       8080);

    /* 🚨 1. 설정에서 절대/상대 경로 분리 로드! 🚨 */
    const char* base_dir   = cfg->getString(cfg, "base_dir",   ".");
    const char* upload_dir = cfg->getString(cfg, "upload_dir", "uploads");
    const char* tpl_dir    = cfg->getString(cfg, "tpl_dir",    "templates");
    const char* log_file   = cfg->getString(cfg, "log_file",   "logs/board.log");

    /* ✅ 추가: 최대 파일 업로드 용량 설정 로드 (기본값 10MB) */
    size_t max_upload_size = (size_t)cfg->getInt(cfg, "max_upload_size", 10485760);

    const char* db_host    = cfg->getString(cfg, "db_host",    "127.0.0.1");
    int         db_port    = cfg->getInt   (cfg, "db_port",    3306);
    const char* db_user    = cfg->getString(cfg, "db_user",    "board");
    const char* db_pass    = cfg->getString(cfg, "db_pass",    "");
    const char* db_name    = cfg->getString(cfg, "db_name",    "board");
    const char* db_charset  = cfg->getString(cfg, "db_charset",    "utf8mb4");
    const char* db_type    = cfg->getString(cfg, "db_type",    "MYSQL");

    char resolved_db[512] = {0};
    if (strncmp(db_type, "SQLITE", 6) == 0) {
        snprintf(resolved_db, sizeof(resolved_db),
                 "%s/%s", base_dir, db_name);
    } else {
        snprintf(resolved_db, sizeof(resolved_db), "%s", db_name);
    }
    /* 🚨 2. 로거 초기화 (절대경로 합성) 🚨 */
    char abs_log[1024];
    snprintf(abs_log, sizeof(abs_log), "%s/%s", base_dir, log_file);

    logger = new_Logger(LOG_LEVEL_DEBUG);
    if (!logger) {
        fprintf(stderr, "[FATAL] Logger 실패\n");
        goto fail_cfg;
    }
    logger->setLogFile(logger, abs_log);

    /* DBClient */
    DBClient* db = new_DBClientDirect(
        db_host, resolved_db, db_user, db_pass,
        db_port, db_charset, db_type);
    if (!db) {
        LOG_ERROR(logger, "DB 실패");
        goto fail_logger;
    }
    db->connect(db);

    /* PathValidator */
    PathValidator* pv = new_PathValidator();
    if (!pv) {
        LOG_ERROR(logger, "PathValidator 실패");
        goto fail_db;
    }

    /* 🚨 3. BoardHandler 생성 (DB 정합성을 위해 base_dir 과 상대경로들, 그리고 max_upload_size 분리 주입!) 🚨 */
    BoardHandler* bh = new_BoardHandler(db, pv, base_dir, upload_dir, tpl_dir, max_upload_size);
    if (!bh) {
        LOG_ERROR(logger, "BoardHandler 실패");
        goto fail_pv;
    }

    /* Router — user_ctx = bh */
    Router* router = new_Router(bh);
    if (!router) {
        LOG_ERROR(logger, "Router 실패");
        goto fail_bh;
    }

    /* 라우트 등록 nodejs+express 같은 의미 */
    /* 게시물 목록 출력 router */
    router->GET   (router, "/",  board_list_cb);
    router->GET   (router, "/board/list",  board_list_cb);
    /* 새글쓰기 폼 router */
    router->GET   (router, "/board/write", board_read_write_cb);
    /* 새글 저장 router */
    router->POST  (router, "/board/write", board_write_cb);
    /* 게시글 상세 보기 폼,  /board/:id?mode=modify 수정하기 폼 */
    router->GET   (router, "/board/:id",   board_detail_cb);
    /* 게시글 수정 저장 router */
    router->PUT   (router, "/board/:id",   board_modify_cb);
    /* 게시글 삭제 router */
    router->DELETE(router, "/board/:id",   board_remove_cb);
    /* 첨부 파일 다운로드 router */
    router->GET   (router, "/board/attach/:id", board_attach_cb);
    /* 코멘트 저장  router */
    router->POST  (router, "/board/:id/comment",   board_comment_write_cb);
    /* 코멘트 수정 router */
    router->PUT   (router, "/board/comment/:id",   board_comment_modify_cb);
    /* 코멘트 삭제 router */
    router->DELETE(router, "/board/comment/:id",   board_comment_remove_cb);
    /* 스킨 및 리밋 업데이트 라우트 개방!!! */
    router->POST  (router, "/board/skin", board_skin_update_cb);
    /* 게시판 목록 갯수 업데이트 */
    router->POST  (router, "/board/limit", board_limit_update_cb);
    /* 첨부 파일 삭제하기  */
    router->DELETE(router, "/board/attach/:id", board_attach_remove_cb);

    /* EventLoop */
    EventLoop* loop = event_loop_create();
    if (!loop) {
        LOG_ERROR(logger, "EventLoop 실패");
        goto fail_router;
    }

    /* HttpServer */
    HttpServer* server = new_HttpServer(loop, router);
    if (!server) {
        LOG_ERROR(logger, "Server 실패");
        goto fail_loop;
    }
    g_server = server;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    LOG_INFO(logger,
    "Board Server v1.7.2 시작: "
    "port %d (Base Dir: %s, "
    "DB: %s %s:%d/%s, "
    "Max Upload: %zu bytes)",
    port, base_dir,
    db_type, db_host, db_port, resolved_db,
    max_upload_size);

    server->listen(server, port);
    event_loop_run(loop);

    /* 소멸 역순 */
    RELEASE((Object*)server);
fail_loop:   event_loop_destroy(loop);
fail_router: RELEASE((Object*)router);
fail_bh:     RELEASE((Object*)bh);
fail_pv:     RELEASE((Object*)pv);
fail_db:     RELEASE((Object*)db);
fail_logger: RELEASE((Object*)logger);
fail_cfg:    RELEASE((Object*)cfg);

    return 0;
}