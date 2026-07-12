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

typedef enum {
    HTTP_STATE_READ_HEADER,
    HTTP_STATE_DISPATCH,
    HTTP_STATE_CLOSED
} HttpConnState;

typedef enum {
    CONN_MODE_HTTP = 0,
    CONN_MODE_WS   = 1
} ConnMode;

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
    bool is_closing;
    bool keep_alive;
    char header_buf[8192];
    size_t header_len;

    /* 🚨 [핵심] 비동기 송신 큐 및 EPOLLOUT 상태 관리 플래그 */
    char* out_buf;
    size_t out_len;
    size_t out_cap;
    bool is_write_registered;

    HttpRequest* req;    /* [OWNED] */
    HttpResponse* res;   /* [OWNED] */
    void* ws_user_data;  /* [BORROWED] */
};

HttpConnection* new_HttpConnection(Socket* client_sock, HttpServer* server);
void HttpConnection_on_readable(Socket* s, void* loop_ptr);

void HttpConnection_on_writable(Socket* s, void* loop_ptr);
void HttpConnection_flush(HttpConnection* conn);

int HttpConnection_ws_send(HttpConnection* conn, const char* msg);
void HttpConnection_ws_close(HttpConnection* conn);
void WsUpgrade_handler(HttpRequest* req, HttpResponse* res, void* user_ctx);

struct HttpServer {
    Object base;
    Socket* server_sock; /* [OWNED] */
    Router* router;      /* [OWNED] */
    EventLoop* loop;     /* [BORROWED] */

    HttpConnection* conns_head;

    void (*on_ws_open)   (HttpConnection* conn);
    void (*on_ws_message)(HttpConnection* conn, const char* msg, size_t len);
    void (*on_ws_close)  (HttpConnection* conn);

    int (*listen)(HttpServer* self, int port);
    void (*stop)(HttpServer* self);
};

HttpServer* new_HttpServer(EventLoop* loop, Router* router);

#endif /* HTTP_SERVER_H */