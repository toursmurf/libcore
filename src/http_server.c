#define _GNU_SOURCE
#define MAX_HTTP_BODY_SIZE (10 * 1024 * 1024)

#include "http_server.h"
#include "tcp_socket.h"
#include "ws_protocol.h"
#include "multipart_parser.h"
#include "string_obj.h"
#include "logger.h"
#include "event_loop_internal.h"
/* 🚨 <ctype.h> 척결 완료: 파서 내 Locale 의존성 원천 차단 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <time.h> /* 🛡️ Deadline 시간 체크용 */

extern Logger* logger;
#define LOG_D(fmt, ...) do { if (logger) LOG_DEBUG(logger, fmt, ##__VA_ARGS__); } while(0)

/* 🛡️ Forward Declarations */
static void remove_conn_from_server(HttpConnection* conn);
static void conn_shutdown(HttpConnection* conn, EventLoop* loop, Socket* s);
static void HttpServer_sweep_closing(HttpServer* server);
static void HttpServer_maybe_finish_shutdown(HttpServer* server);

/* 🛡️ HTTP-S: 엄격한 Content-Length 파서 헬퍼 */
typedef enum {
    CL_PARSE_OK = 0,
    CL_PARSE_INVALID,
    CL_PARSE_TOO_LARGE
} ContentLengthParseResult;

static ContentLengthParseResult parse_content_length_strict(const char* s, size_t* out) {
    size_t v = 0;
    if (!s || !out || *s == '\0')
        return CL_PARSE_INVALID;

    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p < '0' || *p > '9')
            return CL_PARSE_INVALID;

        size_t d = (size_t)(*p - '0');

        if (v > (MAX_HTTP_BODY_SIZE - d) / 10)
            return CL_PARSE_TOO_LARGE;

        v = v * 10 + d;
    }

    *out = v;
    return CL_PARSE_OK;
}

/* 🛡️ HTTP-S V4: Field-Name Token (RFC 7230) 검증 헬퍼 (Locale-independent ASCII Only) */
static int http_is_tchar(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }

    switch (c) {
        case '!': case '#': case '$': case '%':
        case '&': case '\'': case '*': case '+':
        case '-': case '.': case '^': case '_':
        case '`': case '|': case '~':
            return 1;
        default:
            return 0;
    }
}

/* 🛡️ 시간 도우미 함수 (Liveness Deadline 계산용) */
static uint64_t http_server_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void notify_ws_close(HttpConnection* conn) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->ws_close_notified) return;
    conn->ws_close_notified = true;
    if (conn->server && conn->server->on_ws_close) {
        conn->server->on_ws_close(conn);
    }
}

/* ==========================================================
 * Graceful Shutdown Accounting & Retry Timer FSM
 * ========================================================== */
static void on_shutdown_retry_tick(void* user_data) {
    HttpServer* server = (HttpServer*)user_data;
    if (!server || !server->shutting_down) return;

    uint64_t now = http_server_now_ms();
    if (now - server->shutdown_started_ms > 5000) {
        HttpConnection* curr = server->conns_head;
        while (curr) {
            HttpConnection* next = curr->next;
            if (curr->out_len > 0) {
                curr->out_len = 0;
            }
            if (!curr->shutdown_done) {
                conn_shutdown(curr, server->loop, curr->sock);
            }
            curr = next;
        }
    }

    HttpServer_sweep_closing(server);
    HttpServer_maybe_finish_shutdown(server);
}

static void HttpServer_maybe_finish_shutdown(HttpServer* server) {
    if (!server || !server->shutting_down) return;

    if (server->server_sock == NULL &&
        server->conns_head == NULL &&
        server->drain_count == 0) {

        LOG_D("[HttpServer] Graceful Shutdown Complete. Stopping EventLoop.");

        if (server->shutdown_retry_timer) {
            Timer* timer = server->shutdown_retry_timer;
            server->shutdown_retry_timer = NULL;
            timer->stop(timer);
            if (server->loop) {
                server->loop->removeTimer(server->loop, timer);
            }
            RELEASE((Object*)timer);
        }

        if (server->loop && server->loop->running) {
            event_loop_stop(server->loop);
        }
    }
}

static void HttpServer_sweep_closing(HttpServer* server) {
    if (!server) return;

    if (server->server_sock && server->shutting_down) {
        int rc = event_backend_remove(server->loop, server->server_sock);
        if (rc == 0 || (rc < 0 && (errno == ENOENT || errno == EBADF))) {
            server->server_sock->close(server->server_sock);
            RELEASE((Object*)server->server_sock);
            server->server_sock = NULL;
        }
    }

    HttpConnection* curr = server->conns_head;
    while (curr) {
        HttpConnection* next = curr->next;
        if (curr->backend_remove_pending && !curr->shutdown_done) {
            int rc = event_backend_remove(server->loop, curr->sock);
            if (rc == 0 || (rc < 0 && (errno == ENOENT || errno == EBADF))) {
                curr->shutdown_done = true;
                curr->backend_remove_pending = false;
                HttpConnection* tgt = curr;
                remove_conn_from_server(tgt);
                server->loop->deferRelease(server->loop, (Object*)tgt);
            }
        }
        curr = next;
    }
}

void HttpServer_drain_retain(HttpServer* server) {
    if (!server) return;
    server->drain_count++;
}

void HttpServer_drain_release(HttpServer* server) {
    if (!server) return;
    if (server->drain_count > 0) server->drain_count--;

    if (server->shutting_down) {
        HttpServer_sweep_closing(server);
        HttpServer_maybe_finish_shutdown(server);
    }
}

int HttpServer_begin_shutdown(HttpServer* server) {
    if (!server) return -1;
    if (server->shutting_down) return 0;

    LOG_D("[HttpServer] Graceful shutdown initiated.");
    server->shutting_down = true;
    server->shutdown_started_ms = http_server_now_ms();

    server->shutdown_retry_timer = new_Timer(50, true, on_shutdown_retry_tick, server);
    if (!server->shutdown_retry_timer) {
        server->shutting_down = false;
        return -1;
    }

    if (server->loop->addTimer(server->loop, server->shutdown_retry_timer) != 0) {
        RELEASE((Object*)server->shutdown_retry_timer);
        server->shutdown_retry_timer = NULL;
        server->shutting_down = false;
        return -1;
    }

    if (!server->shutdown_retry_timer->start(server->shutdown_retry_timer)) {
        server->loop->removeTimer(server->loop, server->shutdown_retry_timer);
        RELEASE((Object*)server->shutdown_retry_timer);
        server->shutdown_retry_timer = NULL;
        server->shutting_down = false;
        return -1;
    }

    HttpServer_sweep_closing(server);

    HttpConnection* curr = server->conns_head;
    while (curr) {
        HttpConnection* next = curr->next;
        if (curr->mode == CONN_MODE_WS) {
            HttpConnection_ws_close(curr);
        } else {
            curr->is_closing = true;
            curr->keep_alive = false;
            conn_shutdown(curr, server->loop, curr->sock);
        }
        curr = next;
    }

    HttpServer_maybe_finish_shutdown(server);
    return 0;
}

/* ==========================================================
 * HttpConnection Core & Frame Processing
 * ========================================================== */

static void ws_secure_zero(void* ptr, size_t len) {
    volatile unsigned char* p = (volatile unsigned char*)ptr;
    while (len--) { *p++ = 0; }
}

static void ws_consume(HttpConnection* conn, size_t consumed) {
    size_t old_len = conn->ws_in_len;
    if (consumed > old_len) consumed = old_len;
    size_t remaining = old_len - consumed;

    if (remaining > 0) {
        memmove(conn->ws_in_buf, conn->ws_in_buf + consumed, remaining);
    }
    ws_secure_zero(conn->ws_in_buf + remaining, old_len - remaining);
    conn->ws_in_len = remaining;
}

static int append_out_buf(HttpConnection* conn, const uint8_t* data, size_t len) {
    if (!conn) return -1;
    if (len == 0) return 0;
    if (!data) return -1;
    if (len > SIZE_MAX - conn->out_len) return -1;

    size_t required = conn->out_len + len;
    if (required > conn->out_cap) {
        size_t new_cap = conn->out_cap ? conn->out_cap : 4096;
        while (new_cap < required) {
            if (new_cap > SIZE_MAX / 2) {
                new_cap = required;
                break;
            }
            new_cap *= 2;
        }
        char* new_buf = (char*)realloc(conn->out_buf, new_cap);
        if (!new_buf) return -1;
        conn->out_buf = new_buf;
        conn->out_cap = new_cap;
    }
    memcpy(conn->out_buf + conn->out_len, data, len);
    conn->out_len = required;
    return 0;
}

void HttpConnection_append_send(HttpConnection* conn, const uint8_t* data, size_t len) {
    if (conn && data && len > 0) append_out_buf(conn, data, len);
}

static void remove_conn_from_server(HttpConnection* conn) {
    if (!conn || !conn->server) return;
    if (conn->prev) conn->prev->next = conn->next;
    else if (conn->server->conns_head == conn) conn->server->conns_head = conn->next;
    if (conn->next) conn->next->prev = conn->prev;
    conn->server = NULL;
    conn->next = NULL;
    conn->prev = NULL;
}

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
        } else if (sent == SOCKET_WOULD_BLOCK || errno == EAGAIN) {
            break;
        } else if (sent <= 0) {
            if (sent < 0 && errno == EINTR) continue;
            if (conn->server && conn->server->loop) {
                conn_shutdown(conn, conn->server->loop, conn->sock);
            }
            return;
        }
    }

    if (conn->server && conn->server->loop) {
        EventLoop* loop = conn->server->loop;

        if (conn->out_len > 0) {
            if (conn->is_closing) {
                if (!conn->close_write_only_registered) {
                    if (event_backend_modify(loop, conn->sock, EVENT_WRITE) != 0) {
                        conn_shutdown(conn, loop, conn->sock);
                        return;
                    }
                    conn->is_write_registered = true;
                    conn->close_write_only_registered = true;
                }
            }
            else if (!conn->is_write_registered) {
                if (event_backend_modify(loop, conn->sock, EVENT_READ | EVENT_WRITE) != 0) {
                    conn_shutdown(conn, loop, conn->sock);
                    return;
                }
                conn->is_write_registered = true;
            }
        }
        else if (conn->is_write_registered && !conn->is_closing) {
            if (event_backend_modify(loop, conn->sock, EVENT_READ) != 0) {
                conn_shutdown(conn, loop, conn->sock);
                return;
            }
            conn->is_write_registered = false;
            conn->close_write_only_registered = false;
        }
    }

    if (conn->is_closing && conn->out_len == 0 && !conn->backend_remove_pending) {
        if (conn->server && conn->server->loop) {
            conn_shutdown(conn, conn->server->loop, conn->sock);
        }
    }
}

void HttpConnection_on_writable(Socket* s, void* loop_ptr) {
    (void)loop_ptr;
    HttpConnection* conn = (HttpConnection*)s->user_data;
    if (conn) HttpConnection_flush(conn);
}

static void HttpConnection_finalize(Object* obj) {
    HttpConnection* self = (HttpConnection*)obj;
    if (self->server) remove_conn_from_server(self);
    if (self->out_buf) { free(self->out_buf); self->out_buf = NULL; }
    if (self->ws_in_buf) { free(self->ws_in_buf); self->ws_in_buf = NULL; }
    if (self->ws_user_object) { RELEASE(self->ws_user_object); self->ws_user_object = NULL; }
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
    self->state = HTTP_STATE_READ_HEADER;
    self->mode = CONN_MODE_HTTP;
    self->keep_alive = true;

    self->is_closing = false;
    self->backend_remove_pending = false;
    self->shutdown_done = false;
    self->ws_close_notified = false;

    self->is_write_registered = false;
    self->close_write_only_registered = false;

    self->ws_in_buf = NULL;
    self->ws_in_len = 0;
    self->ws_in_cap = 0;
    self->ws_user_data = NULL;
    self->ws_user_object = NULL;
    return self;
}

static void conn_shutdown(HttpConnection* conn, EventLoop* loop, Socket* s) {
    if (conn->shutdown_done || conn->backend_remove_pending) return;

    conn->is_closing = true;

    if (!conn->ws_close_notified && conn->mode == CONN_MODE_WS) {
        notify_ws_close(conn);
    }

    int rc = event_backend_remove(loop, s);
    if (rc == 0 || (rc < 0 && (errno == ENOENT || errno == EBADF))) {
        conn->shutdown_done = true;
        conn->backend_remove_pending = false;
        HttpServer* server = conn->server;

        remove_conn_from_server(conn);
        loop->deferRelease(loop, (Object*)conn);

        if (server && server->shutting_down) {
            HttpServer_maybe_finish_shutdown(server);
        }
    } else {
        conn->backend_remove_pending = true;
    }
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

int HttpConnection_ws_close_checked(HttpConnection* conn) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->is_closing) return -1;
    if (!conn->sock || !conn->sock->is_open) return -1;

    static const uint8_t close_frame[2] = { 0x88, 0x00 };
    if (append_out_buf(conn, close_frame, 2) < 0) return -1;

    conn->is_closing = true;
    HttpConnection_flush(conn);
    return 0;
}

int HttpConnection_ws_send_text(HttpConnection* conn, const char* text, size_t len) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->is_closing || !text) return -1;
    if (len > MAX_WS_PAYLOAD_SIZE) return -1;

    size_t cap = len + 16;
    uint8_t* frame = (uint8_t*)malloc(cap);
    if (!frame) return -1;
    size_t flen = ws_build_frame(WS_OPCODE_TEXT, (const uint8_t*)text, len, frame, cap);
    if (flen == 0) { free(frame); return -1; }

    if (conn->out_len > MAX_WS_OUT_QUEUE || flen > MAX_WS_OUT_QUEUE - conn->out_len) {
        free(frame); return -1;
    }

    if (append_out_buf(conn, frame, flen) < 0) {
        free(frame); return -1;
    }
    free(frame);
    HttpConnection_flush(conn);
    return 0;
}

int HttpConnection_ws_send_binary(HttpConnection* conn, const uint8_t* data, size_t len) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->is_closing || !data) return -1;
    if (len > MAX_WS_PAYLOAD_SIZE) return -1;

    size_t cap = len + 16;
    uint8_t* frame = (uint8_t*)malloc(cap);
    if (!frame) return -1;
    size_t flen = ws_build_frame(WS_OPCODE_BINARY, data, len, frame, cap);
    if (flen == 0) { free(frame); return -1; }

    if (conn->out_len > MAX_WS_OUT_QUEUE || flen > MAX_WS_OUT_QUEUE - conn->out_len) {
        free(frame); return -1;
    }

    if (append_out_buf(conn, frame, flen) < 0) {
        free(frame); return -1;
    }
    free(frame);
    HttpConnection_flush(conn);
    return 0;
}

int HttpConnection_ws_send_pong(HttpConnection* conn, const uint8_t* data, size_t len) {
    if (!conn || conn->mode != CONN_MODE_WS || conn->is_closing) return -1;
    if (len > 125) return -1;

    size_t cap = len + 16;
    uint8_t* frame = (uint8_t*)malloc(cap);
    if (!frame) return -1;
    size_t flen = ws_build_frame(WS_OPCODE_PONG, data, len, frame, cap);
    if (flen == 0) { free(frame); return -1; }

    if (conn->out_len > MAX_WS_OUT_QUEUE || flen > MAX_WS_OUT_QUEUE - conn->out_len) {
        free(frame); return -1;
    }

    if (append_out_buf(conn, frame, flen) < 0) {
        free(frame); return -1;
    }
    free(frame);
    HttpConnection_flush(conn);
    return 0;
}

static int HttpConnection_process_ws_frames(HttpConnection* conn, EventLoop* loop, Socket* s) {
    while (conn->ws_in_len > 0) {
        uint8_t* payload = NULL;
        size_t payload_len = 0;
        size_t consumed = 0;
        int opcode = 0;

        int status = ws_decode_frame3(conn->ws_in_buf, conn->ws_in_len,
                                      &payload, &payload_len, &consumed, &opcode);

        if (status == WS_DECODE_NEED_MORE) return 0;

        if (status == WS_DECODE_ERROR) {
            if (HttpConnection_ws_close_checked(conn) < 0) {
                conn_shutdown(conn, loop, s);
                return -1;
            }
            return 0;
        }

        if (opcode == WS_OPCODE_CLOSE) {
            ws_consume(conn, consumed);
            if (HttpConnection_ws_close_checked(conn) < 0) {
                conn_shutdown(conn, loop, s);
                return -1;
            }
            return 0;
        }

        if (opcode == WS_OPCODE_PING) {
            int rc = HttpConnection_ws_send_pong(conn, payload, payload_len);
            ws_consume(conn, consumed);
            if (rc < 0) {
                if (!conn->shutdown_done && !conn->backend_remove_pending) conn_shutdown(conn, loop, s);
                return -1;
            }
            if (conn->is_closing || conn->shutdown_done || conn->backend_remove_pending) return -1;
            continue;

        } else if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY) {
            if (conn->server && conn->server->on_ws_frame) {
                conn->server->on_ws_frame(conn, opcode, payload, payload_len);
            } else if (conn->server && conn->server->on_ws_message && (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY)) {
                char* legacy = malloc(payload_len + 1);
                if (!legacy) {
                    ws_consume(conn, consumed);
                    conn_shutdown(conn, loop, s);
                    return -1;
                }
                memcpy(legacy, payload, payload_len);
                legacy[payload_len] = '\0';

                conn->server->on_ws_message(conn, legacy, payload_len);

                ws_secure_zero(legacy, payload_len + 1);
                free(legacy);
            }
        }

        ws_consume(conn, consumed);
        if (conn->is_closing || conn->shutdown_done || conn->backend_remove_pending) return -1;
    }
    return 0;
}

/* 🛡️ HTTP-S V4/V5: Embedded CR/LF, Token 검증 및 Locale-independent 소문자 변환 */
static int HttpConnection_parse_headers(HttpConnection* conn, char* header_end) {
    if (!conn || !conn->req || !conn->req->headers || !header_end) return 0;

    /* 첫 번째 CRLF (Request-Line 종료점) 찾기 */
    char* p = strstr(conn->header_buf, "\r\n");
    if (!p || p >= header_end) return 1; /* 헤더 필드 없이 바로 끝남 (정상) */

    p += 2; /* 첫 번째 헤더 라인 시작점으로 이동 */

    bool has_cl = false;
    bool has_te = false;

    while (p < header_end) {
        char* eol = strstr(p, "\r\n");
        if (!eol || eol > header_end) {
            /* CRLF 없이 끝남 (Malformed) -> Fail-closed */
            return 0;
        }

        size_t line_len = (size_t)(eol - p);
        if (line_len == 0) {
            /* "\r\n\r\n" 중복된 빈 줄을 만난 경우 -> 헤더 끝 도달 (정상) */
            break;
        }

        /* 🚨 [HTTP-S] 라인 내부에 Bare CR/LF가 숨어있는지 검사 (Smuggling 차단) */
        if (memchr(p, '\r', line_len) || memchr(p, '\n', line_len)) {
            return 0; /* Fail-closed */
        }

        /* 🚨 [HTTP-S] 콜론(:) 탐지 및 빈 이름(: value) 거부 */
        char* colon = memchr(p, ':', line_len);
        if (!colon || colon == p) {
            return 0; /* Fail-closed */
        }

        /* 🚨 [HTTP-S] Field-Name Token (RFC 7230) 검증 (공백 등 허용 불가 문자 차단) */
        for (char* k = p; k < colon; ++k) {
            if (!http_is_tchar((unsigned char)*k)) {
                return 0; /* Fail-closed */
            }
        }

        *colon = '\0';
        char* key = p;
        char* val = colon + 1;

        /* 🚨 [HTTP-S V5] Key 소문자 변환 (Locale-independent ASCII Manual Lowercase) */
        for (char* k = key; *k; k++) {
            if (*k >= 'A' && *k <= 'Z') {
                *k = (char)(*k - 'A' + 'a');
            }
        }

        /* Value 앞 공백 제거 */
        while (*val == ' ' || *val == '\t') {
            val++;
        }

        *eol = '\0'; /* Value 끝 보장 */

        /* 🚨 [HTTP-S] Duplicate CL & TE 탐지 (Fail-closed) */
        if (strcmp(key, "content-length") == 0) {
            if (has_cl) return 0; /* Duplicate Content-Length 거부 */
            has_cl = true;
        } else if (strcmp(key, "transfer-encoding") == 0) {
            has_te = true;
        }

        if (has_cl && has_te) {
            return 0; /* CL + TE 동시 존재 거부 (Smuggling 차단) */
        }

        hashmap_put_str(conn->req->headers, key, val);
        p = eol + 2; /* 다음 라인으로 전진 */
    }

    /* 🚨 [HTTP-S] TE 단독 사용 거부 (현재 서버는 Request TE 미지원) */
    if (has_te) {
        return 0;
    }

    return 1;
}

void WsUpgrade_handler(HttpRequest* req, HttpResponse* res, void* user_ctx) {
    (void)user_ctx;
    const char* upgrade = hashmap_get_str(req->headers, "upgrade");
    const char* ws_key = hashmap_get_str(req->headers, "sec-websocket-key");

    if (!upgrade || strcasecmp(upgrade, "websocket") != 0 || !ws_key) {
        res->setStatus(res, 400); res->sendText(res, "Invalid Handshake Request"); return;
    }
    char* accept_key = ws_compute_accept_key(ws_key);
    if (!accept_key) { res->sendStatus(res, 500); return; }

    res->setHeader(res, "Upgrade", "websocket");
    res->setHeader(res, "Connection", "Upgrade");
    res->setHeader(res, "Sec-WebSocket-Accept", accept_key);
    res->sendStatus(res, 101);
    free(accept_key);
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int url_decode_query_component(char* str) {
    if (!str) return 0;
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '%') {
            if (!src[1] || !src[2]) return 0;
            int hi = hex_value((unsigned char)src[1]);
            int lo = hex_value((unsigned char)src[2]);
            if (hi < 0 || lo < 0) return 0;
            unsigned char decoded = (unsigned char)((hi << 4) | lo);
            if (decoded == 0) return 0;
            *dst++ = (char)decoded;
            src += 3;
            continue;
        }
        if (*src == '+') {
            *dst++ = ' ';
            src++;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
    return 1;
}

void HttpConnection_on_readable(Socket* s, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    HttpConnection* conn = (HttpConnection*)s->user_data;
    if (!conn) return;

    if (conn->shutdown_done || conn->backend_remove_pending || conn->is_closing) return;

    while (1) {
        if (conn->mode == CONN_MODE_WS) {
            if (!conn->ws_in_buf) {
                conn->ws_in_cap = 8192;
                conn->ws_in_buf = malloc(conn->ws_in_cap);
                if (!conn->ws_in_buf) { conn_shutdown(conn, loop, s); return; }
            }
            if (conn->ws_in_len == conn->ws_in_cap) {
                size_t new_cap = conn->ws_in_cap * 2;
                if (new_cap > MAX_WS_PAYLOAD_SIZE + 4096) {
                    if (conn->ws_in_cap >= MAX_WS_PAYLOAD_SIZE + 4096) {
                        conn_shutdown(conn, loop, s); return;
                    }
                    new_cap = MAX_WS_PAYLOAD_SIZE + 4096;
                }
                uint8_t* new_buf = realloc(conn->ws_in_buf, new_cap);
                if (!new_buf) { conn_shutdown(conn, loop, s); return; }
                conn->ws_in_buf = new_buf;
                conn->ws_in_cap = new_cap;
            }

            size_t to_read = conn->ws_in_cap - conn->ws_in_len;
            ssize_t n = s->recv(s, conn->ws_in_buf + conn->ws_in_len, to_read, NULL, 0);

            if (n == SOCKET_WOULD_BLOCK) break;
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                conn_shutdown(conn, loop, s); return;
            }

            conn->ws_in_len += n;

            if (HttpConnection_process_ws_frames(conn, loop, s) < 0) return;
            if (conn->is_closing || conn->shutdown_done || conn->backend_remove_pending) return;

            continue;
        }

        if (conn->state == HTTP_STATE_READ_HEADER) {
            char buf[4096];
            ssize_t n = s->recv(s, buf, sizeof(buf) - 1, NULL, 0);
            if (n == SOCKET_WOULD_BLOCK) break;
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                conn_shutdown(conn, loop, s); return;
            }
            buf[n] = '\0';

            if (conn->header_len + (size_t)n >= sizeof(conn->header_buf)) {
                const char* err_431 = "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n";
                s->send(s, err_431, strlen(err_431), NULL, 0);
                conn_shutdown(conn, loop, s); return;
            }
            memcpy(conn->header_buf + conn->header_len, buf, n);
            conn->header_len += n;
            conn->header_buf[conn->header_len] = '\0';

            char* header_end = strstr(conn->header_buf, "\r\n\r\n");
            if (!header_end) continue;

            conn->req = new_HttpRequest();
            conn->res = new_HttpResponse(conn->sock, conn);
            if (!conn->req || !conn->res) { conn_shutdown(conn, loop, s); return; }

            const char* line_end = strstr((char*)conn->header_buf, "\r\n");
            const char* first_space = strchr((char*)conn->header_buf, ' ');
            if (!line_end || !first_space || first_space >= line_end) {
                const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                s->send(s, err_400, strlen(err_400), NULL, 0);
                conn_shutdown(conn, loop, s); return;
            }

            char method_str[16] = {0};
            size_t method_len = (size_t)(first_space - (char*)conn->header_buf);
            if (method_len == 0 || method_len >= sizeof(method_str)) {
                const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                s->send(s, err_400, strlen(err_400), NULL, 0);
                conn_shutdown(conn, loop, s); return;
            }
            memcpy(method_str, conn->header_buf, method_len); method_str[method_len] = '\0';

            const char* target_start = first_space + 1;
            const char* second_space = memchr(target_start, ' ', (size_t)(line_end - target_start));
            if (!second_space) {
                const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                s->send(s, err_400, strlen(err_400), NULL, 0);
                conn_shutdown(conn, loop, s); return;
            }

            size_t target_len = (size_t)(second_space - target_start);
            char path_str[2048] = {0};
            if (target_len == 0 || target_len >= sizeof(path_str)) {
                const char* err_414 = "HTTP/1.1 414 URI Too Long\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                s->send(s, err_414, strlen(err_414), NULL, 0);
                conn_shutdown(conn, loop, s); return;
            }
            memcpy(path_str, target_start, target_len); path_str[target_len] = '\0';

            if (strcmp(method_str, "GET") == 0) conn->req->method = HTTP_GET;
            else if (strcmp(method_str, "POST") == 0) conn->req->method = HTTP_POST;
            else if (strcmp(method_str, "PUT") == 0) conn->req->method = HTTP_PUT;
            else if (strcmp(method_str, "DELETE") == 0) conn->req->method = HTTP_DELETE;
            else conn->req->method = HTTP_UNKNOWN;

            char* qmark = strchr(path_str, '?');
            if (qmark) {
                *qmark = '\0';
                char* query_str = qmark + 1;
                if (!conn->req->query) conn->req->query = new_HashMap(8);
                char* saveptr = NULL;
                char* token = strtok_r(query_str, "&", &saveptr);
                while (token) {
                    char* eq = strchr(token, '=');
                    if (eq) {
                        *eq = '\0';
                        char* key = token;
                        char* value = eq + 1;
                        if (!url_decode_query_component(key) || !url_decode_query_component(value)) {
                            const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                            s->send(s, err_400, strlen(err_400), NULL, 0);
                            conn_shutdown(conn, loop, s); return;
                        }
                        hashmap_put_str(conn->req->query, key, value);
                    }
                    token = strtok_r(NULL, "&", &saveptr);
                }
            }
            conn->req->path = new_String(path_str);

            /* 🚨 [HTTP-S V3/V4] 헤더 파싱 중 위반 사항 발생 시 즉시 Fail-closed */
            if (!HttpConnection_parse_headers(conn, header_end)) {
                const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                s->send(s, err_400, strlen(err_400), NULL, 0);
                conn_shutdown(conn, loop, s); return;
            }

            const char* conn_hdr = hashmap_get_str(conn->req->headers, "connection");
            conn->keep_alive = !(conn_hdr && strcasecmp(conn_hdr, "close") == 0);

            /* 🚨 [HTTP-S] 1차 방어막 & SSOT 기록: 엄격한 파싱 및 Fail-closed */
            size_t content_length = 0;
            const char* cl_str2 = hashmap_get_str(conn->req->headers, "content-length");
            if (cl_str2) {
                ContentLengthParseResult pr = parse_content_length_strict(cl_str2, &content_length);
                if (pr == CL_PARSE_INVALID) {
                    const char* err_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    s->send(s, err_400, strlen(err_400), NULL, 0);
                    conn_shutdown(conn, loop, s); return;
                }
                if (pr == CL_PARSE_TOO_LARGE) {
                    const char* err_413 = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    s->send(s, err_413, strlen(err_413), NULL, 0);
                    conn_shutdown(conn, loop, s); return;
                }
                /* [SSOT] 통과된 완벽한 길이를 미리 req에 확정 기록 */
                conn->req->body_len = content_length;
            }

            size_t header_block_size = (header_end + 4) - conn->header_buf;
            size_t body_in_buf = conn->header_len - header_block_size;

            if (content_length > 0) {
                conn->req->body = calloc(1, content_length + 1);
                if (!conn->req->body) { conn_shutdown(conn, loop, s); return; }

                size_t total_read = 0;
                if (body_in_buf > 0) {
                    size_t to_copy = (body_in_buf > content_length) ? content_length : body_in_buf;
                    memcpy(conn->req->body, header_end + 4, to_copy);
                    total_read += to_copy;
                }

                size_t consumed = header_block_size + total_read;
                if (conn->header_len > consumed) {
                    memmove(conn->header_buf, conn->header_buf + consumed, conn->header_len - consumed);
                    conn->header_len -= consumed;
                } else conn->header_len = 0;
                conn->header_buf[conn->header_len] = '\0';

                if (total_read < content_length) {
                    conn->state = HTTP_STATE_READ_BODY;
                    conn->body_read = total_read;
                    continue;
                }
            } else {
                size_t consumed = header_block_size;
                if (conn->header_len > consumed) {
                    memmove(conn->header_buf, conn->header_buf + consumed, conn->header_len - consumed);
                    conn->header_len -= consumed;
                } else conn->header_len = 0;
                conn->header_buf[conn->header_len] = '\0';
            }
        }

        if (conn->state == HTTP_STATE_READ_BODY) {
            /* 🚨 [HTTP-S] 2차 폭탄 제거: 재파싱 없이 SSOT 값(conn->req->body_len) 신뢰 */
            size_t content_length = conn->req->body_len;

            size_t total_read = conn->body_read;
            while (total_read < content_length) {
                char temp[4096];
                size_t to_read = content_length - total_read;
                if (to_read > sizeof(temp)) to_read = sizeof(temp);
                ssize_t rn = s->recv(s, temp, to_read, NULL, 0);

                if (rn == SOCKET_WOULD_BLOCK) { conn->body_read = total_read; return; }
                else if (rn <= 0) {
                    if (rn < 0 && errno == EINTR) continue;
                    conn_shutdown(conn, loop, s); return;
                }
                memcpy((char*)conn->req->body + total_read, temp, rn);
                total_read += rn;
            }
            conn->state = HTTP_STATE_READ_HEADER;
            conn->body_read = 0;
        }

        if (conn->req->body) {
            /* 🚨 [HTTP-S] 3차 폭탄 제거: 불필요한 cl_str 재파싱 삭제 완료 */
            const char* ct = hashmap_get_str(conn->req->headers, "content-type");
            if (ct) {
                if (strstr(ct, "application/x-www-form-urlencoded")) {
                    char* src_body = (char*)conn->req->body;
                    char* saveptr = NULL;
                    char* copy = strdup(src_body);
                    if (copy) {
                        char* token = strtok_r(copy, "&", &saveptr);
                        while (token) {
                            char* eq = strchr(token, '=');
                            if (eq) { *eq = '\0'; hashmap_put_str(conn->req->form, token, eq + 1); }
                            token = strtok_r(NULL, "&", &saveptr);
                        }
                        free(copy);
                    }
                } else if (strstr(ct, "application/json")) {
                    conn->req->json = new_JSON((char*)conn->req->body);
                } else if (strstr(ct, "multipart/form-data")) {
                    char boundary[256] = "";
                    if (Multipart_extract_boundary(ct, boundary, sizeof(boundary)) > 0) {
                        conn->req->multipart = Multipart_parse(conn->req->body, conn->req->body_len, boundary);
                    }
                }
            }
        }

        if (conn->server && conn->server->shutting_down) {
            const char* err_503 = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n";
            s->send(s, err_503, strlen(err_503), NULL, 0);
            conn_shutdown(conn, loop, s);
            return;
        }

        if (conn->router && conn->router->dispatch) {
            conn->router->dispatch(conn->router, conn->req, conn->res);
        }

        bool ws_upgraded = (conn->res && conn->res->status_code == 101);
        RELEASE(conn->req); RELEASE(conn->res); conn->req = NULL; conn->res = NULL;

        if (ws_upgraded) {
            conn->mode = CONN_MODE_WS;
            conn->keep_alive = true;

            if (conn->header_len > 0) {
                conn->ws_in_cap = (conn->header_len > 8192) ? conn->header_len * 2 : 8192;
                conn->ws_in_buf = malloc(conn->ws_in_cap);
                if (!conn->ws_in_buf) { conn_shutdown(conn, loop, s); return; }
                memcpy(conn->ws_in_buf, conn->header_buf, conn->header_len);
                conn->ws_in_len = conn->header_len;
                conn->header_len = 0;
            }

            if (conn->server && conn->server->on_ws_open) {
                conn->server->on_ws_open(conn);
                if (conn->is_closing || conn->shutdown_done || conn->backend_remove_pending) return;
            }

            if (conn->ws_in_len > 0) {
                if (HttpConnection_process_ws_frames(conn, loop, s) < 0) return;
                if (conn->is_closing || conn->shutdown_done || conn->backend_remove_pending) return;
            }
            break;
        }

        if (!conn->keep_alive) { conn_shutdown(conn, loop, s); return; }
        if (conn->header_len == 0) break;
    }
}

static void HttpServer_finalize(Object* obj) {
    HttpServer* self = (HttpServer*)obj;

    HttpConnection* curr = self->conns_head;
    while (curr) {
        HttpConnection* next = curr->next;
        curr->server = NULL;
        RELEASE((Object*)curr);
        curr = next;
    }
    self->conns_head = NULL;

    if (self->server_sock) {
        RELEASE((Object*)self->server_sock);
        self->server_sock = NULL;
    }

    if (self->shutdown_retry_timer) {
        RELEASE((Object*)self->shutdown_retry_timer);
        self->shutdown_retry_timer = NULL;
    }

    if (self->router) {
        RELEASE((Object*)self->router);
        self->router = NULL;
    }
}

static const Class _HttpServer_Class = {
    .name = "HttpServer",
    .size = sizeof(HttpServer),
    .finalize = HttpServer_finalize
};

static void on_accept_cb(Socket* server_sock, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    HttpServer* server = (HttpServer*)server_sock->user_data;

    if (server && server->shutting_down) {
        int rc = event_backend_remove(loop, server_sock);
        if (rc == 0 || (rc < 0 && (errno == ENOENT || errno == EBADF))) {
            server->server_sock->close(server->server_sock);
            RELEASE((Object*)server->server_sock);
            server->server_sock = NULL;
            HttpServer_maybe_finish_shutdown(server);
        }
        return;
    }

    while (1) {
        char client_ip[64];
        int client_port;
        Socket* client_sock = (Socket*)((TcpSocket*)server->server_sock)->accept((TcpSocket*)server->server_sock, client_ip, &client_port);

        if (!client_sock) break;

        int flag = 1;
        setsockopt(client_sock->fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

        HttpConnection* conn = new_HttpConnection(client_sock, server);
        if (!conn) { RELEASE((Object*)client_sock); continue; }

        if (event_backend_add(loop, client_sock, EVENT_READ) != 0) {
            RELEASE((Object*)conn);
            continue;
        }

        conn->next = server->conns_head;
        if (server->conns_head) server->conns_head->prev = conn;
        server->conns_head = conn;

        client_sock->user_data = conn;
        client_sock->on_readable = HttpConnection_on_readable;
        client_sock->on_writable = HttpConnection_on_writable;
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

    if (event_backend_add(self->loop, self->server_sock, EVENT_READ) != 0) {
        self->server_sock->close(self->server_sock);
        RELEASE((Object*)self->server_sock);
        self->server_sock = NULL;
        return -1;
    }
    return 0;
}

static void impl_stop(HttpServer* self) {
    if (self && self->loop) {
        event_loop_stop(self->loop);
    }
}

HttpServer* new_HttpServer(EventLoop* loop, Router* router) {
    HttpServer* self = (HttpServer*)calloc(1, sizeof(HttpServer));
    if (!self) return NULL;
    Object_Init((Object*)self, &_HttpServer_Class);
    self->loop = loop;
    if (router) RETAIN((Object*)router);
    self->router = router;
    self->conns_head = NULL;

    self->shutting_down = false;
    self->drain_count = 0;
    self->shutdown_started_ms = 0;
    self->shutdown_retry_timer = NULL;

    self->listen = impl_listen;
    self->stop = impl_stop;
    return self;
}