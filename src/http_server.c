#include "http_server.h"
#include "tcp_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
 * [1] HttpConnection DLL 관리 및 구현부
 * ========================================================= */

/* 🚨 [결함 B 패치] 활성 커넥션 리스트에서 O(1) 안전 제거 */
static void remove_conn_from_server(HttpConnection* conn) {
    if (!conn || !conn->server) return;

    if (conn->prev) conn->prev->next = conn->next;
    else conn->server->conns_head = conn->next;

    if (conn->next) conn->next->prev = conn->prev;

    conn->server = NULL;
    conn->next = NULL;
    conn->prev = NULL;
}

static void HttpConnection_finalize(Object* obj) {
    HttpConnection* self = (HttpConnection*)obj;

    /* 혹시 리스트에 남아있다면 댕글링 포인터 방지를 위해 제거 */
    if (self->server) remove_conn_from_server(self);

    if (self->req) RELEASE(self->req);
    if (self->res) RELEASE(self->res);
    if (self->sock) RELEASE(self->sock);
}

static const Class _HttpConnection_Class = {
    .name = "HttpConnection",
    .size = sizeof(HttpConnection),
    .finalize = HttpConnection_finalize
};

HttpConnection* new_HttpConnection(Socket* client_sock, HttpServer* server) {
    HttpConnection* self = (HttpConnection*)calloc(1, sizeof(HttpConnection));
    if (!self) return NULL;

    Object_Init((Object*)self, &_HttpConnection_Class);
    self->sock = client_sock;
    self->server = server;
    self->router = server ? server->router : NULL;

    self->next = NULL;
    self->prev = NULL;
    self->state = HTTP_STATE_READ_HEADER;
    self->is_closing = false;
    self->header_len = 0;

    return self;
}

void HttpConnection_on_readable(Socket* s, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    HttpConnection* conn = (HttpConnection*)s->user_data;

    if (!conn || conn->is_closing) return;

    /* 🚨 [결함 A 패치] EPOLLET 특성 대응: EAGAIN이 뜰 때까지 영혼까지 Drain! */
    while (1) {
        char buf[4096];
        ssize_t n = s->recv(s, buf, sizeof(buf) - 1, NULL, NULL);

        if (n < 0) {
            /* 커널 버퍼가 비었음 (EAGAIN / EWOULDBLOCK). 다음 이벤트를 기다림 */
            break;
        }

        if (n == 0) {
            /* 클라이언트 측 연결 정상 종료 */
            conn->is_closing = true;
            remove_conn_from_server(conn);
            loop->delSocket(loop, s);
            loop->deferRelease(loop, (Object*)conn);
            return;
        }

        buf[n] = '\0';

        /* 🚨 431 Request Header Fields Too Large 차단 */
        if (conn->header_len + (size_t)n >= sizeof(conn->header_buf)) {
            const char* err_431 = "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n";
            s->send(s, err_431, strlen(err_431), NULL, NULL);

            conn->is_closing = true;
            remove_conn_from_server(conn);
            loop->delSocket(loop, s);
            loop->deferRelease(loop, (Object*)conn);
            return;
        }

        memcpy(conn->header_buf + conn->header_len, buf, n);
        conn->header_len += n;
        conn->header_buf[conn->header_len] = '\0';

        /* 파이프라이닝을 대비한 내부 파싱 루프 */
        while (conn->header_len > 0) {
            char* header_end = strstr(conn->header_buf, "\r\n\r\n");
            if (!header_end) break; /* 헤더가 아직 덜 왔음. 다시 recv 루프로! */

            conn->req = new_HttpRequest();
            conn->res = new_HttpResponse(conn->sock);

            /* OOM(Out of Memory) 즉사 방어 */
            if (!conn->req || !conn->res) {
                if (conn->req) RELEASE(conn->req);
                if (conn->res) RELEASE(conn->res);
                conn->req = NULL;
                conn->res = NULL;

                conn->is_closing = true;
                remove_conn_from_server(conn);
                loop->delSocket(loop, s);
                loop->deferRelease(loop, (Object*)conn);
                return;
            }

            char method_str[16] = {0};
            char path_str[256] = {0};

            if (sscanf(conn->header_buf, "%15s %255s", method_str, path_str) == 2) {
                if (strcmp(method_str, "GET") == 0) conn->req->method = HTTP_GET;
                else if (strcmp(method_str, "POST") == 0) conn->req->method = HTTP_POST;
                else if (strcmp(method_str, "PUT") == 0) conn->req->method = HTTP_PUT;
                else if (strcmp(method_str, "DELETE") == 0) conn->req->method = HTTP_DELETE;
                else conn->req->method = HTTP_UNKNOWN;
                conn->req->path = new_String(path_str);
            } else {
                conn->req->method = HTTP_GET;
                conn->req->path = new_String("/");
            }

            if (conn->router && conn->router->dispatch) {
                conn->router->dispatch(conn->router, conn->req, conn->res);
            }

            int keep_alive = 1;
            /* 현재 헤더 블록 안에서만 Connection: close 판정 */
            *header_end = '\0';
            if (strstr(conn->header_buf, "Connection: close")) keep_alive = 0;
            *header_end = '\r';

            RELEASE(conn->req);
            RELEASE(conn->res);
            conn->req = NULL;
            conn->res = NULL;

            /* 파이프라이닝 대응: 처리한 헤더 블록만큼 버퍼 앞으로 당기기 */
            size_t processed_len = (header_end + 4) - conn->header_buf;
            size_t remaining = conn->header_len - processed_len;
            if (remaining > 0) {
                memmove(conn->header_buf, header_end + 4, remaining);
            }
            conn->header_len = remaining;
            conn->header_buf[conn->header_len] = '\0';

            if (!keep_alive) {
                conn->is_closing = true;
                remove_conn_from_server(conn);
                loop->delSocket(loop, s);
                loop->deferRelease(loop, (Object*)conn);
                return;
            }
        } /* 파싱 루프 종료 */
    } /* 🚨 EAGAIN 탈출 recv 루프 종료 */
}

/* =========================================================
 * [2] HttpServer 구현부
 * ========================================================= */
static void HttpServer_finalize(Object* obj) {
    HttpServer* self = (HttpServer*)obj;

    /* 🚨 [결함 B 패치] 서버 강제 종료 시, 남아있는 활성 커넥션 모두 순회 소각! */
    HttpConnection* curr = self->conns_head;
    while (curr) {
        HttpConnection* next = curr->next;

        if (self->loop && curr->sock) {
            self->loop->delSocket(self->loop, curr->sock);
        }

        curr->server = NULL; /* finalize 내부에서의 재귀 호출 방어 */
        RELEASE((Object*)curr);
        curr = next;
    }
    self->conns_head = NULL;

    if (self->server_sock) RELEASE(self->server_sock);
    if (self->router) RELEASE(self->router);
}

static const Class _HttpServer_Class = {
    .name = "HttpServer",
    .size = sizeof(HttpServer),
    .finalize = HttpServer_finalize
};

static void on_accept_cb(Socket* server_sock, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    HttpServer* server = (HttpServer*)server_sock->user_data;

    char client_ip[64];
    int client_port;
    Socket* client_sock = (Socket*)((TcpSocket*)server->server_sock)->accept(
        (TcpSocket*)server->server_sock, client_ip, &client_port);

    if (!client_sock) return;

    HttpConnection* conn = new_HttpConnection(client_sock, server);
    if (!conn) {
        RELEASE((Object*)client_sock);
        return;
    }

    /* 🚨 활성 커넥션 DLL에 현재 객체 삽입 (O(1) 시간복잡도) */
    conn->next = server->conns_head;
    if (server->conns_head) {
        server->conns_head->prev = conn;
    }
    server->conns_head = conn;

    client_sock->user_data = conn;
    client_sock->on_readable = HttpConnection_on_readable;

    loop->addSocket(loop, client_sock, EV_READ);
}

static int impl_listen(HttpServer* self, int port) {
    if (!self || !self->loop) return -1;

    char url[64];
    snprintf(url, sizeof(url), "tcp://0.0.0.0:%d", port);

    self->server_sock = createServer(url, NULL);
    if (!self->server_sock) return -1;

    self->server_sock->user_data = self;
    self->server_sock->on_readable = on_accept_cb;

    self->loop->addSocket(self->loop, self->server_sock, EV_READ);
    return 0;
}

static void impl_stop(HttpServer* self) {
    if (self && self->loop) {
        /* 이벤트 루프 정지 시그널 전송 (선택적) */
        self->loop->stop(self->loop);
    }
}

HttpServer* new_HttpServer(EventLoop* loop, Router* router) {
    HttpServer* self = (HttpServer*)calloc(1, sizeof(HttpServer));
    if (!self) return NULL;
    Object_Init((Object*)self, &_HttpServer_Class);
    self->loop = loop;

    if (router) {
        RETAIN((Object*)router);
    }
    self->router = router;
    self->conns_head = NULL;

    self->listen = impl_listen;
    self->stop = impl_stop;
    return self;
}