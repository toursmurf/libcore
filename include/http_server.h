#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#define _GNU_SOURCE
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

typedef struct HttpServer HttpServer;

typedef struct HttpConnection HttpConnection;
struct HttpConnection {
    Object base;
    Socket* sock;        /* [OWNED] */
    Router* router;      /* [BORROWED] */
    HttpServer* server;  /* [BORROWED] 활성 커넥션 추적용 서버 포인터 */

    /* O(1) 삭제를 위한 Intrusive 양방향 연결 리스트 노드 */
    HttpConnection* next;
    HttpConnection* prev;

    HttpConnState state;
    bool is_closing;
    char header_buf[8192];
    size_t header_len;
    HttpRequest* req;    /* [OWNED] */
    HttpResponse* res;   /* [OWNED] */
};

HttpConnection* new_HttpConnection(Socket* client_sock, HttpServer* server);
void HttpConnection_on_readable(Socket* s, void* loop_ptr);

struct HttpServer {
    Object base;
    Socket* server_sock; /* [OWNED] */
    Router* router;      /* [OWNED] */
    EventLoop* loop;     /* [BORROWED] */

    /* 종료 시 누수 방지를 위한 활성 커넥션 DLL 헤드 */
    HttpConnection* conns_head;

    int (*listen)(HttpServer* self, int port);
    void (*stop)(HttpServer* self);
};

HttpServer* new_HttpServer(EventLoop* loop, Router* router);

#endif /* HTTP_SERVER_H */