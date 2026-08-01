#include "http_message.h"
#include "http_server.h"   /* HttpConnection_append_send, HttpConnection_flush */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
* [0] 유틸리티: O(1) 정적 상태 메시지 테이블
* ========================================================= */
const char* Http_statusMessage(int code) {
    switch (code) {
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

/* =========================================================
* [1] HttpRequest 구현부
* ========================================================= */
static void HttpRequest_finalize(Object* obj) {
    HttpRequest* self = (HttpRequest*)obj;
    if (self->path)      RELEASE(self->path);
    if (self->headers)   RELEASE(self->headers);
    if (self->query)     RELEASE(self->query);
    if (self->json)      RELEASE(self->json);
    if (self->form)      RELEASE(self->form);
    if (self->multipart) RELEASE((Object*)self->multipart);
    if (self->body)      { free(self->body); self->body = NULL; }
}

static const Class _HttpRequest_Class = {
    .name = "HttpRequest",
    .size = sizeof(HttpRequest),
    .finalize = HttpRequest_finalize
};

HttpRequest* new_HttpRequest(void) {
    HttpRequest* self = (HttpRequest*)calloc(1, sizeof(HttpRequest));
    if (!self) return NULL;

    Object_Init((Object*)self, &_HttpRequest_Class);

    self->method  = HTTP_UNKNOWN;
    self->headers = new_HashMap(32);
    self->query   = new_HashMap(16);
    self->form    = new_HashMap(16);

    if (!self->headers || !self->query || !self->form) {
        RELEASE(self);
        return NULL;
    }
    return self;
}

/* =========================================================
* [2] HttpResponse 구현부
* ========================================================= */
static void impl_setStatus(HttpResponse* self, int code) {
    if (self) self->status_code = code;
}

/* 🚨 [클순 마님(🔫) 패치] NULL 방어 및 ARC 최적화 */
static void impl_setHeader(HttpResponse* self, const char* key, const char* value) {
    if (!self || !self->headers || !key || !value) return;

    String* v = new_String(value);
    if (!v) return; /* OOM 즉시 탈출 */

    self->headers->put(self->headers, key, (Object*)v);
    RELEASE(v);
}

/* =========================================================
 * 🚨 [클순 마님(🔫) 패치] Rule 6-2 준수: 헤더 순회용 컨텍스트
 * ========================================================= */
typedef struct {
    char* buf;
    size_t cap;
    int len;
    bool overflow;
} HdrCtx;

/* 안전한 Mutex 보호망 안에서 실행되는 콜백 함수 */
static bool write_hdr_cb(const char* k, Object* v, void* ctx_ptr) {
    HdrCtx* ctx = (HdrCtx*)ctx_ptr;

    /* 이미 버퍼가 꽉 찼다면 순회 중단 */
    if (ctx->overflow) return false;

    String* val_str = (String*)v;
    if (val_str && val_str->c_str) {
        int n = snprintf(ctx->buf + ctx->len, ctx->cap - ctx->len, "%s: %s\r\n",
                         k, val_str->c_str(val_str));

        /* 버퍼 오버플로우 감지 시 플래그 켜고 순회 강제 종료 */
        if (n < 0 || (size_t)n >= ctx->cap - ctx->len) {
            ctx->overflow = true;
            return false;
        }
        ctx->len += n;
    }
    return true; /* 다음 노드로 계속 진행 */
}

/* 🚨 [클순 마님(🔫) 패치] snprintf 오버플로우 방어막 및 iterate 적용 */
static void send_internal(HttpResponse* self, const char* ctype, const char* body, size_t body_len) {
    if (!self || !self->socket || !self->socket->is_open) return;

    /* ── 헤더 조립 ── */
    char head_buf[4096];
    int len = snprintf(head_buf, sizeof(head_buf), "HTTP/1.1 %d %s\r\n",
                       self->status_code, Http_statusMessage(self->status_code));
    if (len < 0 || (size_t)len >= sizeof(head_buf)) return;

    if (ctype) {
        int n = snprintf(head_buf + len, sizeof(head_buf) - len,
                         "Content-Type: %s\r\n", ctype);
        if (n > 0 && (size_t)n < sizeof(head_buf) - len) len += n;
    }

    int n_len = snprintf(head_buf + len, sizeof(head_buf) - len,
                         "Content-Length: %zu\r\n", body_len);
    if (n_len > 0 && (size_t)n_len < sizeof(head_buf) - len) len += n_len;

    HdrCtx ctx = {
        .buf      = head_buf,
        .cap      = sizeof(head_buf),
        .len      = len,
        .overflow = false
    };
    if (self->headers)
        self->headers->iterate(self->headers, write_hdr_cb, &ctx);

    /* 헤더 오버플로 시 500 응답으로 전환 (부분 헤더 무음 발사 차단) */
    if (ctx.overflow) {
        static const char err500[] =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n";
        if (self->conn)
            HttpConnection_append_send(self->conn,
                                       (const uint8_t*)err500, sizeof(err500)-1);
        else
            self->socket->send(self->socket, err500, sizeof(err500)-1, NULL, 0);
        return;
    }

    int n_end = snprintf(head_buf + ctx.len, sizeof(head_buf) - ctx.len, "\r\n");
    if (n_end > 0 && (size_t)n_end < sizeof(head_buf) - ctx.len)
        ctx.len += n_end;

    /* ── flush 체계 경유 송신 ──
     *   conn 있으면 append_out_buf + HttpConnection_flush (EPOLLOUT 통일)
     *   conn 없으면 직발사 (WsUpgrade_handler 등 conn 미확보 경로 방어) */
    if (self->conn) {
        HttpConnection_append_send(self->conn,
                                   (const uint8_t*)head_buf, ctx.len);
        if (body && body_len > 0)
            HttpConnection_append_send(self->conn,
                                       (const uint8_t*)body, body_len);
        HttpConnection_flush(self->conn);
    } else {
        self->socket->send(self->socket, head_buf, ctx.len, NULL, 0);
        if (body && body_len > 0)
            self->socket->send(self->socket, body, body_len, NULL, 0);
    }
}

static void impl_sendText(HttpResponse* self, const char* text) {
    if (!self) return;
    if (!text) text = "";
    send_internal(self, "text/plain; charset=utf-8", text, strlen(text));
}

static void impl_sendJson(HttpResponse* self, JSONNode* json) {
    if (!self || !json) return;

    char* json_str = json->toString(json);
    if (!json_str) {
        impl_setStatus(self, 500);
        impl_sendText(self, "Internal Server Error: JSON Stringify failed");
        return;
    }
    send_internal(self, "application/json", json_str, strlen(json_str));
    free(json_str);
}

static void impl_sendStatus(HttpResponse* self, int code) {
    if (!self) return;
    self->status_code = code;
    send_internal(self, NULL, "", 0);
}

static void impl_redirect(HttpResponse* self, const char* url) {
    if (!self || !url) return;
    self->status_code = 302;
    impl_setHeader(self, "Location", url);
    send_internal(self, NULL, "", 0);
}

static void impl_sendFile(HttpResponse* self, const char* path) {
    if (!self || !path) return;

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        impl_sendStatus(self, 404);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char* ctype = "application/octet-stream";
    if (strstr(path, ".html")) ctype = "text/html; charset=utf-8";
    else if (strstr(path, ".json")) ctype = "application/json";
    else if (strstr(path, ".png"))  ctype = "image/png";
    else if (strstr(path, ".jpg"))  ctype = "image/jpeg";

    char head_buf[1024];
    int len = snprintf(head_buf, sizeof(head_buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n\r\n", ctype, size);

    /* 🚨 [나초안(🪲) 패치] sendFile 버퍼 오버플로우 방어 및 fd 누수 방지 */
    if (len < 0 || (size_t)len >= sizeof(head_buf)) {
        fclose(fp);
        return;
    }

    if (self->conn) {
        HttpConnection_append_send(self->conn, (const uint8_t*)head_buf, len);
    } else {
        self->socket->send(self->socket, head_buf, len, NULL, 0);
    }

    char chunk[8192];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (self->conn) {
            HttpConnection_append_send(self->conn, (const uint8_t*)chunk, read_bytes);
        } else {
            ssize_t sent = self->socket->send(self->socket, chunk, read_bytes, NULL, 0);
            if (sent < 0) break;
        }
    }
    fclose(fp);
    if (self->conn) HttpConnection_flush(self->conn);
}

/* [BORROWED] 절대 소켓을 닫거나 해제하지 않음 */
static void HttpResponse_finalize(Object* obj) {
    HttpResponse* self = (HttpResponse*)obj;
    if (self->headers) RELEASE(self->headers);
}

static const Class _HttpResponse_Class = {
    .name = "HttpResponse",
    .size = sizeof(HttpResponse),
    .finalize = HttpResponse_finalize
};

HttpResponse* new_HttpResponse(Socket* sock, struct HttpConnection* conn) {
    HttpResponse* self = (HttpResponse*)calloc(1, sizeof(HttpResponse));
    if (!self) return NULL;

    Object_Init((Object*)self, &_HttpResponse_Class);

    self->socket = sock;
    self->conn   = conn;   /* [BORROWED] flush 경로용 */
    self->status_code = 200;
    self->headers = new_HashMap(16);

    if (!self->headers) {
        RELEASE(self);
        return NULL;
    }

    self->setStatus  = impl_setStatus;
    self->sendStatus = impl_sendStatus;
    self->sendText   = impl_sendText;
    self->sendJson   = impl_sendJson;
    self->sendFile   = impl_sendFile;
    self->setHeader  = impl_setHeader;
    self->redirect   = impl_redirect;

    return self;
}