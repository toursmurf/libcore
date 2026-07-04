#include "http_server.h"
#include "tcp_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* =========================================================
 * [1] HttpConnection DLL 관리 및 구현부
 * ========================================================= */
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

    if (self->server) remove_conn_from_server(self);

    if (self->req) {
        if (self->req->body) free(self->req->body);
        RELEASE(self->req);
    }
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

    while (1) {
        char buf[4096];
        ssize_t n = s->recv(s, buf, sizeof(buf) - 1, NULL, 0);

        /* 🚨 [잔여이슈 2 해결] recv errno 정밀 분기 처리 */
        if (n < 0) {
            if (errno == EINTR) continue; /* 시스템 시그널 인터럽트 -> 즉시 재시도 */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 커널 버퍼가 비었음 -> 이벤트 루프로 돌아가 대기 */
                break;
            }
            /* ECONNRESET, ETIMEDOUT 등 치명적 에러 발생 시 연결 즉시 종료 */
            conn->is_closing = true;
            remove_conn_from_server(conn);
            loop->delSocket(loop, s);
            loop->deferRelease(loop, (Object*)conn);
            return;
        }

        if (n == 0) {
            conn->is_closing = true;
            remove_conn_from_server(conn);
            loop->delSocket(loop, s);
            loop->deferRelease(loop, (Object*)conn);
            return;
        }

        buf[n] = '\0';

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
            if (!header_end) break; /* 헤더가 아직 덜 왔음 */

            conn->req = new_HttpRequest();
            conn->res = new_HttpResponse(conn->sock);

            if (!conn->req || !conn->res) {
                if (conn->req) RELEASE(conn->req);
                if (conn->res) RELEASE(conn->res);
                conn->req = NULL; conn->res = NULL;
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

            /* 🚨 [잔여이슈 1 해결] Content-Length 기반 Request Body 안전 소비 로직 */
            int content_length = 0;
            char* cl_ptr = strcasestr(conn->header_buf, "Content-Length:");
            if (cl_ptr && cl_ptr < header_end) {
                content_length = atoi(cl_ptr + 15);
            }

            size_t header_block_size = (header_end + 4) - conn->header_buf;
            size_t body_in_buf = conn->header_len - header_block_size;
            size_t buffer_consumed = header_block_size;

            if (content_length > 0) {
                conn->req->body = calloc(1, content_length + 1);
                if (conn->req->body) {
                    size_t total_read = 0;

                    /* 버퍼에 이미 들어와 있는 바디 조각 복사 */
                    if (body_in_buf > 0) {
                        size_t to_copy = (body_in_buf > (size_t)content_length) ? (size_t)content_length : body_in_buf;
                        memcpy(conn->req->body, header_end + 4, to_copy);
                        total_read += to_copy;
                        buffer_consumed += to_copy;
                    }

                    /* 소켓에 남아있는 바디 영혼까지 Drain! (EPIPE 완벽 차단) */
                    while (total_read < (size_t)content_length) {
                        char temp[4096];
                        size_t to_read = content_length - total_read;
                        if (to_read > sizeof(temp)) to_read = sizeof(temp);

                        ssize_t rn = s->recv(s, temp, to_read, NULL, NULL);
                        if (rn > 0) {
                            memcpy((char*)conn->req->body + total_read, temp, rn);
                            total_read += rn;
                        } else if (rn < 0) {
                            if (errno == EINTR) continue;
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                usleep(2000); /* 비동기 환경 대응 간이 대기 */
                                continue;
                            }
                            break; /* 통신 절단 */
                        } else {
                            break; /* EOF */
                        }
                    }
                }
            }

            /* 라우터 디스패치 실행 */
            if (conn->router && conn->router->dispatch) {
                conn->router->dispatch(conn->router, conn->req, conn->res);
            }

            int keep_alive = 1;
            *header_end = '\0';
            if (strcasestr(conn->header_buf, "Connection: close")) keep_alive = 0;
            *header_end = '\r';

            /* 바디 메모리 해제 및 객체 반환 */
            if (conn->req->body) {
                free(conn->req->body);
                conn->req->body = NULL;
            }
            RELEASE(conn->req);
            RELEASE(conn->res);
            conn->req = NULL;
            conn->res = NULL;

            /* 파이프라이닝 대응: 소비한 블록만큼 버퍼 앞으로 당기기 */
            size_t remaining = conn->header_len - buffer_consumed;
            if (remaining > 0) {
                memmove(conn->header_buf, conn->header_buf + buffer_consumed, remaining);
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
        }
    }
}

/* =========================================================
 * [2] HttpServer 구현부
 * ========================================================= */
static void HttpServer_finalize(Object* obj) {
    HttpServer* self = (HttpServer*)obj;

    HttpConnection* curr = self->conns_head;
    while (curr) {
        HttpConnection* next = curr->next;
        if (self->loop && curr->sock) {
            self->loop->delSocket(self->loop, curr->sock);
        }
        curr->server = NULL;
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