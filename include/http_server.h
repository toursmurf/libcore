#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "object.h"
#include "socket_base.h"
#include "event_loop.h"
#include "router.h"
#include "http_message.h"
#include "timer.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_WS_OUT_QUEUE (2 * 1024 * 1024)

typedef enum { HTTP_STATE_READ_HEADER = 0, HTTP_STATE_READ_BODY } HttpConnState;
typedef enum { CONN_MODE_HTTP = 0, CONN_MODE_WS = 1 } ConnMode;

typedef struct HttpServer HttpServer;
typedef struct HttpConnection HttpConnection;

struct HttpConnection {
    Object base;
    Socket* sock;        /* [OWNED] */
    Router* router;      /* [BORROWED] */
    HttpServer* server;  /* [BORROWED] */

    HttpConnection* next;
    HttpConnection* prev;

    HttpConnState state;
    ConnMode mode;

    /* 🛡️ Lifecycle 분리: Flush 대상 vs 강제 제거 보류 대상 */
    bool is_closing;                  /* CLOSE 프레임 등 Flush 진행 중 */
    bool backend_remove_pending;      /* event_backend_remove가 EAGAIN으로 보류됨 (Sweep 대상) */
    bool shutdown_done;

    bool keep_alive;
    bool ws_close_notified;

    char header_buf[8192];
    size_t header_len;
    size_t body_read;

    uint8_t* ws_in_buf;
    size_t ws_in_len;
    size_t ws_in_cap;

    char* out_buf;
    size_t out_len;
    size_t out_cap;

    /* 🛡️ 커널 이벤트 등록 상태 세분화 (Hot-Spin DoS 방어용) */
    bool is_write_registered;
    bool close_write_only_registered;

    HttpRequest* req;    /* [OWNED] */
    HttpResponse* res;   /* [OWNED] */

    void* ws_user_data;       /* [BORROWED] Legacy 호환 유지 */
    Object* ws_user_object;   /* [OWNED] 신규 ARC 지원용 Additive 추가 */
};

HttpConnection* new_HttpConnection(Socket* client_sock, HttpServer* server);
void HttpConnection_on_readable(Socket* s, void* loop_ptr);
void HttpConnection_on_writable(Socket* s, void* loop_ptr);
void HttpConnection_flush(HttpConnection* conn);

void HttpConnection_append_send(HttpConnection* conn, const uint8_t* data, size_t len);
int HttpConnection_ws_send(HttpConnection* conn, const char* msg);
void HttpConnection_ws_close(HttpConnection* conn);

int HttpConnection_ws_close_checked(HttpConnection* conn);
int HttpConnection_ws_send_text(HttpConnection* conn, const char* text, size_t len);
int HttpConnection_ws_send_binary(HttpConnection* conn, const uint8_t* data, size_t len);
int HttpConnection_ws_send_pong(HttpConnection* conn, const uint8_t* data, size_t len);

void WsUpgrade_handler(HttpRequest* req, HttpResponse* res, void* user_ctx);

struct HttpServer {
    Object base;
    Socket* server_sock; /* [OWNED] */
    Router* router;      /* [OWNED] */
    EventLoop* loop;     /* [BORROWED] */

    HttpConnection* conns_head;

    /* 🛡️ Graceful Shutdown & Async Drain Accounting */
    bool shutting_down;
    size_t drain_count;
    uint64_t shutdown_started_ms; /* 🛡️ Liveness Deadline 추적용 */
    Timer* shutdown_retry_timer;  /* [OWNED] 셧다운 큐 클리어용 재시도 엔진 */

    void (*on_ws_open)   (HttpConnection* conn);
    void (*on_ws_message)(HttpConnection* conn, const char* msg, size_t len);
    void (*on_ws_frame)  (HttpConnection* conn, int opcode, const uint8_t* msg, size_t len);
    void (*on_ws_close)  (HttpConnection* conn);

    int (*listen)(HttpServer* self, int port);
    void (*stop)(HttpServer* self);
};

HttpServer* new_HttpServer(EventLoop* loop, Router* router);

/* ==========================================
 * [NEW] Global Graceful Shutdown Contract
 * ========================================== */
int  HttpServer_begin_shutdown(HttpServer* server);
void HttpServer_drain_retain(HttpServer* server);
void HttpServer_drain_release(HttpServer* server);

#endif /* HTTP_SERVER_H */