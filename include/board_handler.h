/*
 * board_handler.h
 * 게시판 핸들러 — libcore OOP 스타일
 *
 * 소유권 규칙:
 *   BoardHandler  [OWNED]   — RELEASE() 로 해제
 *   db            [BORROWED] — main 에서 소유, BoardHandler 가 빌림
 *   pv            [BORROWED] — main 에서 소유, 싱글턴
 *
 * 사용법:
 *   BoardHandler* bh = new_BoardHandler(db, pv, upload_dir);
 *
 *   router->POST  (router, "/board/write", board_write_cb,  bh);
 *   router->GET   (router, "/board/list",  board_list_cb,   bh);
 *   router->GET   (router, "/board/:id",   board_detail_cb, bh);
 *   router->PUT   (router, "/board/:id",   board_modify_cb, bh);
 *   router->DELETE(router, "/board/:id",   board_remove_cb, bh);
 *
 *   ...
 *   RELEASE((Object*)bh);
 */

#ifndef BOARD_HANDLER_H
#define BOARD_HANDLER_H

#include "object.h"
#include "http_server.h"
#include "path_validator.h"
#include "db.h"
#include "http_client.h"   /* HttpMultipartFile */
#include <stdbool.h>
#include <stddef.h>

typedef struct BoardHandler BoardHandler;

struct BoardHandler {
    Object base;

    /* ── 의존성 [BORROWED] ── */
    DBClient*      db;
    PathValidator* pv;
    char           upload_dir[512];

    /* ── HTTP 핸들러 ── */
    void (*write)  (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*list)   (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*detail) (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*modify) (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*remove) (BoardHandler* self, HttpRequest* req, HttpResponse* res);

    /* ── 파일 유틸 ── */
    bool (*file_save)    (BoardHandler*      self,
                          HttpMultipartFile* attach,
                          char*              out_path,
                          size_t             out_size);
    bool (*file_delete)  (BoardHandler* self, const char* path);
    void (*file_sanitize)(BoardHandler* self,
                          const char*   dirty,
                          char*         clean_out,
                          size_t        max_len);
};

BoardHandler* new_BoardHandler(DBClient*      db,
                                PathValidator* pv,
                                const char*    upload_dir);

/* ── 라우터 콜백 어댑터 ── */
void board_write_cb  (HttpRequest* req, HttpResponse* res, void* ctx);
void board_list_cb   (HttpRequest* req, HttpResponse* res, void* ctx);
void board_detail_cb (HttpRequest* req, HttpResponse* res, void* ctx);
void board_modify_cb (HttpRequest* req, HttpResponse* res, void* ctx);
void board_remove_cb (HttpRequest* req, HttpResponse* res, void* ctx);

#endif /* BOARD_HANDLER_H */