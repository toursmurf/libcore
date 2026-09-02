/*
 * board_handler.h
 * 게시판 핸들러 — libcore OOP 스타일
 *
 * 소유권 규칙:
 *   BoardHandler  [OWNED]    — RELEASE() 로 해제
 *   db            [BORROWED] — main 에서 소유, BoardHandler 가 빌림
 *   pv            [BORROWED] — main 에서 소유, 싱글턴
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

    char           base_dir[256];
    char           upload_dir[256];
    char           tpl_dir[256];
    char           skin[32];
    char           skinDir[512]; // base_dir + tpl_dir + skin 조합 절대경로!

    /* 추가: 최대 업로드 용량 설정값 (app.conf 연동) */
    size_t         max_upload_size;

    /* ── HTTP 핸들러 ── */
    void (*read_write) (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*write)      (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*list)       (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*detail)     (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*modify)     (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*remove)     (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*attach)     (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*attach_remove)(struct BoardHandler* self, struct HttpRequest* req, struct HttpResponse* res);
    void (*comment_write) (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*comment_modify)(BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*comment_remove)(BoardHandler* self, HttpRequest* req, HttpResponse* res);

    /* ── 파일 유틸 ── */
    /* 추가: 파일 용량 초과 검증 (SRP 단일 책임 분리) */
    bool (*file_size_exceeded)(BoardHandler* self, HttpMultipartFile* attach);

    bool (*file_save)    (BoardHandler* self, HttpMultipartFile* attach, char* out_path, size_t out_size);
    bool (*file_delete)  (BoardHandler* self, const char* path);
    void (*file_sanitize)(BoardHandler* self, const char* dirty, char* clean_out, size_t max_len);
    void (*skin_update)  (BoardHandler* self, HttpRequest* req, HttpResponse* res);
    void (*limit_update)(struct BoardHandler* self, HttpRequest* req, HttpResponse* res);
};

/* ── 생성자 (파라미터 추가) ── */
BoardHandler* new_BoardHandler(DBClient* db, PathValidator* pv, const char* base_dir, const char* upload_dir, const char* tpl_dir, size_t max_upload_size);

/* ── 라우터 콜백 어댑터 ── */
void board_read_write_cb    (HttpRequest* req, HttpResponse* res, void* ctx);
void board_write_cb         (HttpRequest* req, HttpResponse* res, void* ctx);
void board_list_cb          (HttpRequest* req, HttpResponse* res, void* ctx);
void board_detail_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_modify_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_remove_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_attach_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_comment_write_cb (HttpRequest* req, HttpResponse* res, void* ctx);
void board_comment_modify_cb(HttpRequest* req, HttpResponse* res, void* ctx);
void board_comment_remove_cb(HttpRequest* req, HttpResponse* res, void* ctx);
void board_skin_update_cb   (HttpRequest* req, HttpResponse* res, void* ctx);
void board_limit_update_cb(HttpRequest* req, HttpResponse* res, void* ctx);
void board_attach_remove_cb(struct HttpRequest* req, struct HttpResponse* res, void* ctx);
#endif /* BOARD_HANDLER_H */