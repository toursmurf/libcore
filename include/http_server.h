#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

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
    Socket* sock;
    Router* router;
    HttpServer* server;

    HttpConnection* next;
    HttpConnection* prev;

    HttpConnState state;
    bool is_closing;
    char header_buf[8192];
    size_t header_len;
    HttpRequest* req;
    HttpResponse* res;
};

HttpConnection* new_HttpConnection(Socket* client_sock, HttpServer* server);
void HttpConnection_on_readable(Socket* s, void* loop_ptr);

struct HttpServer {
    Object base;
    Socket* server_sock;
    Router* router;
    EventLoop* loop;

    HttpConnection* conns_head;

    int (*listen)(HttpServer* self, int port);
    void (*stop)(HttpServer* self);
};

HttpServer* new_HttpServer(EventLoop* loop, Router* router);

#endif /* HTTP_SERVER_H */
