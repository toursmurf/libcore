#define _GNU_SOURCE
/* 단일 HTTP 페이로드 최대 크기: 10MB (OOM 방어) */
#define MAX_HTTP_BODY_SIZE (10 * 1024 * 1024)
#include "http_server.h"
#include "tcp_socket.h"
#include "ws_protocol.h"
#include "string_obj.h"
#include "logger.h" /* 🚨 로그 추적을 위해 명시적 추가 */
#include "event_loop_internal.h" /* 🚀 [신규 패치] V1.6.x 3대 OS 통합 API 접근을 위한 핵심 헤더! */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

extern Logger* logger;
#define LOG_D(fmt, ...) do { if (logger) LOG_DEBUG(logger, fmt, ##__VA_ARGS__); } while(0)

static void append_out_buf(HttpConnection* conn, const uint8_t* data, size_t len) {
    if (conn->out_len + len > conn->out_cap) {
        size_t new_cap = conn->out_cap == 0 ? 4096 : conn->out_cap * 2;
        while (new_cap < conn->out_len + len) new_cap *= 2;
        char* new_buf = (char*)realloc(conn->out_buf, new_cap);
        if (!new_buf) return;
        conn->out_buf = (char *)new_buf;
        conn->out_cap = new_cap;
    }
    memcpy(conn->out_buf + conn->out_len, data, len);
    conn->out_len += len;
}

static void remove_conn_from_server(HttpConnection* conn);
static void conn_shutdown(HttpConnection* conn, EventLoop* loop, Socket* s);

void HttpConnection_flush(HttpConnection* conn) {
    if (!conn || !conn->sock || !conn->sock->is_open) return;

    while (conn->out_len > 0) {
        ssize_t sent = conn->sock->send(conn->sock, conn->out_buf, conn->out_len, NULL, 0);

        if (sent > 0) {
            if ((size_t)sent < conn->out_len) {
                memmove(conn->out_buf, conn->out_buf + sent, conn->out_len - sent);
                conn->out_len -= sent;
            } else {
                conn->out_len = 0;
                break;
            }
        }
        else if (sent == SOCKET_WOULD_BLOCK) {
            break;
        }
        else if (sent <= 0) {
            if (sent < 0 && errno == EINTR) continue;
            if (conn->server && conn->server->loop) {
                conn_shutdown(conn, conn->server->loop, conn->sock);
            }
            return;
        }
    }

    if (conn->server && conn->server->loop) {
        EventLoop* loop = conn->server->loop;
        if (conn->out_len > 0 && !conn->is_write_registered) {
            /* 🚀 [패치] 구형 addSocket -> 신형 event_backend_modify */
            event_backend_modify(loop, conn->sock, EVENT_READ | EVENT_WRITE);
            conn->is_write_registered = true;
        } else if (conn->out_len == 0 && conn->is_write_registered) {
            /* 🚀 [패치] 구형 addSocket -> 신형 event_backend_modify */
            event_backend_modify(loop, conn->sock, EVENT_READ);
            conn->is_write_registered = false;
        }
    }

    if (conn->is_closing && conn->out_len == 0) {
        if (conn->server && conn->server->loop) {
            conn_shutdown(conn, conn->server->loop, conn->sock);
        }
    }
}

void HttpConnection_on_writable(Socket* s, void* loop_ptr) {
    (void)loop_ptr;
    HttpConnection* conn = (HttpConnection*)s->user_data;
    if (conn) {
        HttpConnection_flush(conn);
    }
}

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
    if (self->out_buf) { free(self->out_buf); self->out_buf = NULL; }
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
    self->mode = CONN_MODE_HTTP;
    self->is_closing = false;
    self->shutdown_done = false;
    self->keep_alive = true;
    self->header_len = 0;
    self->body_read = 0;

    self->out_buf = NULL;
    self->out_len = 0;
    self->out_cap = 0;
    self->is_write_registered = false;

    self->ws_user_data = NULL;

    return self;
}

static void conn_shutdown(HttpConnection* conn, EventLoop* loop, Socket* s) {
    if (conn->shutdown_done) return;
    conn->shutdown_done = true;

    if (conn->mode == CONN_MODE_WS && conn->server && conn->server->on_ws_close) {
        conn->server->on_ws_close(conn);
    }
    conn->is_closing = true;
    remove_conn_from_server(conn);

    /* 🚀 [패치] 구형 delSocket -> 신형 event_backend_remove */
    event_backend_remove(loop, s);

    /* 🚀 [패치] 구형 deferRelease -> ARC 표준 RELEASE 통일 */
    RELEASE((Object*)conn);
}

static void HttpConnection_parse_headers(HttpConnection* conn, char* header_end) {
    if (!conn || !conn->req || !conn->req->headers || !header_end) return;

    char* first_crlf = strstr(conn->header_buf, "\r\n");
    if (!first_crlf || first_crlf >= header_end) return;

    size_t region_len = (size_t)(header_end - (first_crlf + 2));
    if (region_len == 0) return;

    char* copy = (char*)malloc(region_len + 1);
    if (!copy) return;
    memcpy(copy, first_crlf + 2, region_len);
    copy[region_len] = '\0';

    char* saveptr;
    char* line = strtok_r(copy, "\r\n", &saveptr);
    while (line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* key = line;
            char* val = colon + 1;
            for (char* p = key; *p; p++) *p = (char)tolower((unsigned char)*p);
            while (*val == ' ' || *val == '\t') val++;
            hashmap_put_str(conn->req->headers, key, val);
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
    free(copy);
}

int HttpConnection_ws_send(HttpConnection* conn, const char* msg) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->is_closing || !msg) return -1;
    if (!conn->sock || !conn->sock->is_open) return -1;

    size_t msg_len = strlen(msg);
    size_t cap = msg_len + 16;
    uint8_t* frame = (uint8_t*)malloc(cap);
    if (!frame) return -1;

    size_t flen = ws_build_text_frame(msg, frame, cap);
    if (flen == 0) { free(frame); return -1; }

    append_out_buf(conn, frame, flen);
    free(frame);

    HttpConnection_flush(conn);
    return 0;
}

void HttpConnection_ws_close(HttpConnection* conn) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->is_closing) return;

    conn->is_closing = true;
    if (conn->sock && conn->sock->is_open) {
        static const uint8_t close_frame[2] = { 0x88, 0x00 };
        append_out_buf(conn, close_frame, 2);
        HttpConnection_flush(conn);
    }
}

static int HttpConnection_process_ws_frames(HttpConnection* conn, EventLoop* loop, Socket* s) {
    /* ws_decode_frame2에서 MAX_WS_PAYLOAD_SIZE(1MB)로 1차 차단됨
     * 여기서는 실제 메시지 처리 크기(8KB) 상한 적용 — 스택 보호 */
    char payload[8192];

    while (conn->header_len > 0) {
        size_t consumed = 0;
        int is_ping = 0;
        ssize_t r = ws_decode_frame2((const uint8_t*)conn->header_buf, conn->header_len,
                                     payload, sizeof(payload), &consumed, &is_ping);

        if (r == 0) return 0;

        if (r == -2) {
            conn_shutdown(conn, loop, s);
            return -1;
        }

        if (r == -1) {
            static const uint8_t close_frame[2] = { 0x88, 0x00 };
            append_out_buf(conn, close_frame, 2);
            HttpConnection_flush(conn);
            conn_shutdown(conn, loop, s);
            return -1;
        }

        if (is_ping) {
            size_t plen = (size_t)r;
            uint8_t pong[8192 + 16];
            pong[0] = 0x8A;
            size_t hlen = 2;
            if (plen <= 125) { pong[1] = (uint8_t)plen; }
            else { pong[1] = 126; pong[2] = (plen >> 8) & 0xFF; pong[3] = plen & 0xFF; hlen = 4; }
            memcpy(pong + hlen, payload, plen);

            append_out_buf(conn, pong, hlen + plen);
            HttpConnection_flush(conn);
        } else if (conn->server && conn->server->on_ws_message) {
            conn->server->on_ws_message(conn, payload, (size_t)r);
            if (conn->is_closing) return -1;
        }

        size_t remaining = conn->header_len - consumed;
        if (remaining > 0) memmove(conn->header_buf, conn->header_buf + consumed, remaining);
        conn->header_len = remaining;
    }
    return 0;
}

void WsUpgrade_handler(HttpRequest* req, HttpResponse* res, void* user_ctx) {
    (void)user_ctx;

    const char* upgrade = hashmap_get_str(req->headers, "upgrade");
    const char* ws_key  = hashmap_get_str(req->headers, "sec-websocket-key");

    if (!upgrade || strcasecmp(upgrade, "websocket") != 0 || !ws_key) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid Handshake Request");
        return;
    }

    char* accept_key = ws_compute_accept_key(ws_key);
    if (!accept_key) {
        res->sendStatus(res, 500);
        return;
    }

    res->setHeader(res, "Upgrade", "websocket");
    res->setHeader(res, "Connection", "Upgrade");
    res->setHeader(res, "Sec-WebSocket-Accept", accept_key);
    res->sendStatus(res, 101);

    free(accept_key);
}

void HttpConnection_on_readable(Socket* s, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    HttpConnection* conn = (HttpConnection*)s->user_data;

    if (!conn || conn->is_closing) return;

    while (1) {
        if (conn->mode == CONN_MODE_WS) {
            char buf[4096];
            ssize_t n = s->recv(s, buf, sizeof(buf) - 1, NULL, 0);

            if (n == SOCKET_WOULD_BLOCK) break;
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                conn_shutdown(conn, loop, s);
                return;
            }

            if (conn->header_len + (size_t)n >= sizeof(conn->header_buf)) {
                conn_shutdown(conn, loop, s);
                return;
            }
            memcpy(conn->header_buf + conn->header_len, buf, n);
            conn->header_len += n;
            if (HttpConnection_process_ws_frames(conn, loop, s) < 0) return;
            continue;
        }

        if (conn->state == HTTP_STATE_READ_HEADER) {
            char buf[4096];
            ssize_t n = s->recv(s, buf, sizeof(buf) - 1, NULL, 0);

            if (n == SOCKET_WOULD_BLOCK) break;
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                conn_shutdown(conn, loop, s);
                return;
            }

            /* 🚨 [추적] 클라이언트 패킷 도달 확인 */
            buf[n] = '\0';
            LOG_D("[RECV] fd=%d, size=%zd, data=%s", conn->sock->fd, n, buf);

            if (conn->header_len + (size_t)n >= sizeof(conn->header_buf)) {
                const char* err_431 = "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n";
                s->send(s, err_431, strlen(err_431), NULL, 0);
                conn_shutdown(conn, loop, s);
                return;
            }

            memcpy(conn->header_buf + conn->header_len, buf, n);
            conn->header_len += n;
            conn->header_buf[conn->header_len] = '\0';

            char* header_end = strstr(conn->header_buf, "\r\n\r\n");
            if (!header_end) continue;

            conn->req = new_HttpRequest();
            conn->res = new_HttpResponse(conn->sock);

            if (!conn->req || !conn->res) {
                conn_shutdown(conn, loop, s);
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

                /* 🚀 [신규 패치] ? 문자를 찾아 경로와 쿼리 스트링 완벽 분리 */
                char* qs = strchr(path_str, '?');
                if (qs) {
                    *qs = '\0'; /* 경로 문자열 종료 처리 */
                    qs++;       /* 쿼리 스트링 시작점 이동 */

                    /* 쿼리 스트링 파싱 (key=value&key2=value2) */
                    char* saveptr;
                    char* token = strtok_r(qs, "&", &saveptr);
                    while (token) {
                        char* eq = strchr(token, '=');
                        if (eq) {
                            *eq = '\0';
                            hashmap_put_str(conn->req->query, token, eq + 1);
                        }
                        token = strtok_r(NULL, "&", &saveptr);
                    }
                }
                
                /* ? 이후가 깔끔하게 잘려나간 순수 경로(/logs)만 저장 */
                conn->req->path = new_String(path_str);
            } else {
                conn->req->method = HTTP_GET;
                conn->req->path = new_String("/");
            }

            HttpConnection_parse_headers(conn, header_end);

            const char* conn_hdr = hashmap_get_str(conn->req->headers, "connection");
            conn->keep_alive = !(conn_hdr && strcasecmp(conn_hdr, "close") == 0);

            long content_length = 0;
            char* cl_ptr = strcasestr(conn->header_buf, "Content-Length:");
            if (cl_ptr && cl_ptr < header_end) {
                char* endptr = NULL;
                long cl_val = strtol(cl_ptr + 15, &endptr, 10);
                /* [방어 1] 음수 차단 */
                if (cl_val < 0) {
                    const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    s->send(s, err_400, strlen(err_400), NULL, 0);
                    conn_shutdown(conn, loop, s);
                    return;
                }
                /* [방어 2] 상한(10MB) 초과 차단 */
                if (cl_val > MAX_HTTP_BODY_SIZE) {
                    const char* err_413 = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    s->send(s, err_413, strlen(err_413), NULL, 0);
                    conn_shutdown(conn, loop, s);
                    return;
                }
                content_length = (endptr != cl_ptr + 15) ? cl_val : 0;
            }

            size_t header_block_size = (header_end + 4) - conn->header_buf;
            size_t body_in_buf = conn->header_len - header_block_size;

            if (content_length > 0) {
                conn->req->body = calloc(1, (size_t)content_length + 1);
                if (!conn->req->body) {
                    conn_shutdown(conn, loop, s);
                    return;
                }

                size_t total_read = 0;
                if (body_in_buf > 0) {
                    size_t to_copy = (body_in_buf > (size_t)content_length) ? (size_t)content_length : body_in_buf;
                    memcpy(conn->req->body, header_end + 4, to_copy);
                    total_read += to_copy;
                }

                size_t consumed = header_block_size + total_read;
                if (conn->header_len > consumed) {
                    memmove(conn->header_buf, conn->header_buf + consumed, conn->header_len - consumed);
                    conn->header_len -= consumed;
                } else {
                    conn->header_len = 0;
                }
                conn->header_buf[conn->header_len] = '\0';

                if (total_read < (size_t)content_length) {
                    conn->state = HTTP_STATE_READ_BODY;
                    conn->body_read = total_read;
                    continue;
                }
            } else {
                size_t consumed = header_block_size;
                if (conn->header_len > consumed) {
                    memmove(conn->header_buf, conn->header_buf + consumed, conn->header_len - consumed);
                    conn->header_len -= consumed;
                } else {
                    conn->header_len = 0;
                }
                conn->header_buf[conn->header_len] = '\0';
            }
        }

        if (conn->state == HTTP_STATE_READ_BODY) {
            const char* cl_str = hashmap_get_str(conn->req->headers, "content-length");
            long content_length = 0;
            if (cl_str) {
                char* endptr = NULL;
                long cl_val = strtol(cl_str, &endptr, 10);
                content_length = (endptr != cl_str && cl_val > 0 && cl_val <= MAX_HTTP_BODY_SIZE)
                                 ? cl_val : 0;
            }
            size_t total_read = conn->body_read;

            while (total_read < (size_t)content_length) {
                char temp[4096];
                size_t to_read = (size_t)content_length - total_read;
                if (to_read > sizeof(temp)) to_read = sizeof(temp);

                ssize_t rn = s->recv(s, temp, to_read, NULL, 0);
                if (rn == SOCKET_WOULD_BLOCK) {
                    conn->body_read = total_read;
                    return;
                } else if (rn <= 0) {
                    if (rn < 0 && errno == EINTR) continue;
                    conn_shutdown(conn, loop, s);
                    return;
                }

                memcpy((char*)conn->req->body + total_read, temp, rn);
                total_read += rn;
            }

            conn->state = HTTP_STATE_READ_HEADER;
            conn->body_read = 0;
        }

        if (conn->router && conn->router->dispatch) {
            conn->router->dispatch(conn->router, conn->req, conn->res);
        }

        bool ws_upgraded = (conn->res->status_code == 101);

        if (conn->req->body) {
            free(conn->req->body);
            conn->req->body = NULL;
        }
        RELEASE(conn->req);
        RELEASE(conn->res);
        conn->req = NULL;
        conn->res = NULL;

        if (ws_upgraded) {
            conn->mode = CONN_MODE_WS;
            conn->keep_alive = true;
            if (conn->server && conn->server->on_ws_open) {
                conn->server->on_ws_open(conn);
            }
            if (conn->header_len > 0) {
                if (HttpConnection_process_ws_frames(conn, loop, s) < 0) return;
            }
            break;
        }

        if (!conn->keep_alive) {
            conn_shutdown(conn, loop, s);
            return;
        }

        if (conn->header_len == 0) {
            break;
        }
    }
}

static void HttpServer_finalize(Object* obj) {
    HttpServer* self = (HttpServer*)obj;

    HttpConnection* curr = self->conns_head;
    while (curr) {
        HttpConnection* next = curr->next;
        if (self->loop && curr->sock) {
            /* 🚀 [패치] 구형 delSocket -> 신형 event_backend_remove */
            event_backend_remove(self->loop, curr->sock);
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

    while (1) {
        char client_ip[64];
        int client_port;
        Socket* client_sock = (Socket*)((TcpSocket*)server->server_sock)->accept(
            (TcpSocket*)server->server_sock, client_ip, &client_port);

        if (!client_sock) break;

        int flag = 1;
        setsockopt(client_sock->fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

        HttpConnection* conn = new_HttpConnection(client_sock, server);
        if (!conn) {
            RELEASE((Object*)client_sock);
            continue;
        }

        conn->next = server->conns_head;
        if (server->conns_head) {
            server->conns_head->prev = conn;
        }
        server->conns_head = conn;

        client_sock->user_data = conn;
        client_sock->on_readable = HttpConnection_on_readable;
        client_sock->on_writable = HttpConnection_on_writable;

        /* 🚀 [패치] 구형 addSocket -> 신형 event_backend_add */
        event_backend_add(loop, client_sock, EVENT_READ);
    }
}

static int impl_listen(HttpServer* self, int port) {
    if (!self || !self->loop) return -1;

    char url[64];
    snprintf(url, sizeof(url), "tcp://0.0.0.0:%d", port);

    self->server_sock = createServer(url, NULL);
    if (!self->server_sock) return -1;

    self->server_sock->user_data = self;
    self->server_sock->on_readable = on_accept_cb;

    /* 🚀 [패치] 구형 addSocket -> 신형 event_backend_add */
    event_backend_add(self->loop, self->server_sock, EVENT_READ);
    return 0;
}

static void impl_stop(HttpServer* self) {
    if (self && self->loop) {
        /* 🚀 [패치] 구형 stop -> 공용 API event_loop_stop */
        event_loop_stop(self->loop);
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
