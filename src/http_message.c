/*[cite: 1] */
#include "http_message.h"
#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* Http_statusMessage(int code) {
    switch (code) {
        case 101:
            return "Switching Protocols";
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

static void HttpRequest_finalize(Object* obj) {
    HttpRequest* self = (HttpRequest*)obj;

    if (self->path) {
        RELEASE(self->path);
    }

    if (self->headers) {
        RELEASE(self->headers);
    }

    if (self->query) {
        RELEASE(self->query);
    }

    if (self->params) {
        RELEASE(self->params);
    }

    if (self->json) {
        RELEASE(self->json);
    }

    if (self->form) {
        RELEASE(self->form);
    }

    if (self->multipart) {
        RELEASE((Object*)self->multipart);
    }

    if (self->body) {
        free(self->body);
        self->body = NULL;
    }
}

static const Class _HttpRequest_Class = {
    .name = "HttpRequest",
    .size = sizeof(HttpRequest),
    .finalize = HttpRequest_finalize
};

HttpRequest* new_HttpRequest(void) {
    HttpRequest* self = (HttpRequest*)calloc(1, sizeof(HttpRequest));

    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &_HttpRequest_Class);

    self->method = HTTP_UNKNOWN;
    self->headers = new_HashMap(32);
    self->query = new_HashMap(16);
    self->form = new_HashMap(16);
    self->params = new_HashMap(8);

    if (!self->headers || !self->query || !self->form || !self->params) {
        RELEASE(self);
        return NULL;
    }

    return self;
}

static void impl_setStatus(HttpResponse* self, int code) {
    if (self) {
        self->status_code = code;
    }
}

static void impl_setHeader(HttpResponse* self, const char* key, const char* value) {
    if (!self || !self->headers || !key || !value) {
        return;
    }

    String* v = new_String(value);

    if (!v) {
        return;
    }

    self->headers->put(self->headers, key, (Object*)v);
    RELEASE(v);
}

typedef struct {
    char* buf;
    size_t cap;
    int len;
    bool overflow;
} HdrCtx;

static bool write_hdr_cb(const char* k, Object* v, void* ctx_ptr) {
    HdrCtx* ctx = (HdrCtx*)ctx_ptr;

    if (ctx->overflow) {
        return false;
    }

    String* val_str = (String*)v;

    if (val_str && val_str->c_str) {
        int n = snprintf(ctx->buf + ctx->len, ctx->cap - ctx->len, "%s: %s\r\n", k, val_str->c_str(val_str));

        if (n < 0 || (size_t)n >= ctx->cap - ctx->len) {
            ctx->overflow = true;
            return false;
        }

        ctx->len += n;
    }

    return true;
}

static void send_internal(HttpResponse* self, const char* ctype, const char* body, size_t body_len) {
    if (!self || !self->socket || !self->socket->is_open) {
        return;
    }

    char head_buf[4096];
    int len = snprintf(head_buf, sizeof(head_buf), "HTTP/1.1 %d %s\r\n", self->status_code, Http_statusMessage(self->status_code));

    if (len < 0 || (size_t)len >= sizeof(head_buf)) {
        return;
    }

    const char* custom_ctype = NULL;

    if (self->headers) {
        custom_ctype = hashmap_get_str(self->headers, "Content-Type");

        if (!custom_ctype) {
            custom_ctype = hashmap_get_str(self->headers, "content-type");
        }
    }

    if (ctype && !custom_ctype) {
        int n = snprintf(head_buf + len, sizeof(head_buf) - len, "Content-Type: %s\r\n", ctype);

        if (n < 0 || (size_t)n >= sizeof(head_buf) - len) {
            return;
        }

        len += n;
    }

    int n_len = snprintf(head_buf + len, sizeof(head_buf) - len, "Content-Length: %zu\r\n", body_len);

    if (n_len > 0 && (size_t)n_len < sizeof(head_buf) - len) {
        len += n_len;
    }

    HdrCtx ctx = {
        .buf = head_buf,
        .cap = sizeof(head_buf),
        .len = len,
        .overflow = false
    };

    if (self->headers) {
        self->headers->iterate(self->headers, write_hdr_cb, &ctx);
    }

    if (ctx.overflow) {
        static const char err500[] =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";

        if (self->conn) {
            HttpConnection_append_send(self->conn, (const uint8_t*)err500, sizeof(err500) - 1);
        } else {
            self->socket->send(self->socket, err500, sizeof(err500) - 1, NULL, 0);
        }

        return;
    }

    int n_end = snprintf(head_buf + ctx.len, sizeof(head_buf) - ctx.len, "\r\n");

    if (n_end > 0 && (size_t)n_end < sizeof(head_buf) - ctx.len) {
        ctx.len += n_end;
    }

    if (self->conn) {
        HttpConnection_append_send(self->conn, (const uint8_t*)head_buf, ctx.len);

        if (body && body_len > 0) {
            HttpConnection_append_send(self->conn, (const uint8_t*)body, body_len);
        }

        HttpConnection_flush(self->conn);
    } else {
        self->socket->send(self->socket, head_buf, ctx.len, NULL, 0);

        if (body && body_len > 0) {
            self->socket->send(self->socket, body, body_len, NULL, 0);
        }
    }
}

static void impl_sendText(HttpResponse* self, const char* text) {
    if (!self) {
        return;
    }

    if (!text) {
        text = "";
    }

    send_internal(self, "text/plain; charset=utf-8", text, strlen(text));
}

static void impl_sendJson(HttpResponse* self, JSONNode* json) {
    if (!self || !json) {
        return;
    }

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
    if (!self) {
        return;
    }

    self->status_code = code;

    send_internal(self, NULL, "", 0);
}

static void impl_redirect(HttpResponse* self, const char* url) {
    if (!self || !url) {
        return;
    }

    self->status_code = 302;

    impl_setHeader(self, "Location", url);
    send_internal(self, NULL, "", 0);
}

static void impl_sendFile(HttpResponse* self, const char* path) {
    if (!self || !path) {
        return;
    }

    FILE* fp = fopen(path, "rb");

    if (!fp) {
        impl_sendStatus(self, 404);
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return;
    }

    long size = ftell(fp);

    if (size < 0) {
        fclose(fp);
        return;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return;
    }

    char head_buf[4096];
    int len = snprintf(head_buf, sizeof(head_buf),
        "HTTP/1.1 200 OK\r\n");

    if (len < 0 || (size_t)len >= sizeof(head_buf)) {
        fclose(fp);
        return;
    }

    /* Content-Type — explicit 없으면 path 기반 기본값 */
    const char* custom_ctype = NULL;

    if (self->headers) {
        custom_ctype = hashmap_get_str(self->headers, "Content-Type");

        if (!custom_ctype) {
            custom_ctype = hashmap_get_str(self->headers, "content-type");
        }
    }

    if (!custom_ctype) {
        const char* ctype = "application/octet-stream";

        if (strstr(path, ".html")) {
            ctype = "text/html; charset=utf-8";
        } else if (strstr(path, ".json")) {
            ctype = "application/json";
        } else if (strstr(path, ".png")) {
            ctype = "image/png";
        } else if (strstr(path, ".jpg")) {
            ctype = "image/jpeg";
        }

        int n = snprintf(head_buf + len,
            sizeof(head_buf) - len,
            "Content-Type: %s\r\n", ctype);

        if (n > 0 && (size_t)n < sizeof(head_buf) - len) {
            len += n;
        }
    }

    /* Content-Length — explicit 없으면 파일 사이즈 */
    const char* custom_clen = NULL;

    if (self->headers) {
        custom_clen = hashmap_get_str(self->headers, "Content-Length");
    }

    if (!custom_clen) {
        int n = snprintf(head_buf + len,
            sizeof(head_buf) - len,
            "Content-Length: %ld\r\n", size);

        if (n > 0 && (size_t)n < sizeof(head_buf) - len) {
            len += n;
        }
    }

    /* self->headers 순회 — Content-Disposition 등!! */
    HdrCtx ctx = {
        .buf      = head_buf,
        .cap      = sizeof(head_buf),
        .len      = len,
        .overflow = false
    };

    if (self->headers) {
        self->headers->iterate(self->headers, write_hdr_cb, &ctx);
    }

    if (ctx.overflow) {
        static const char err500[] =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";

        if (self->conn) {
            HttpConnection_append_send(self->conn,
                (const uint8_t*)err500, sizeof(err500) - 1);
        } else {
            self->socket->send(self->socket,
                err500, sizeof(err500) - 1, NULL, 0);
        }

        fclose(fp);
        return;
    }

    /* \r\n 실패 시 종료!! */
    int n_end = snprintf(head_buf + ctx.len,
        sizeof(head_buf) - ctx.len, "\r\n");

    if (n_end < 0 || (size_t)n_end >= sizeof(head_buf) - ctx.len) {
        fclose(fp);
        return;
    }

    ctx.len += n_end;

    if (self->conn) {
        HttpConnection_append_send(self->conn,
            (const uint8_t*)head_buf, ctx.len);
    } else {
        self->socket->send(self->socket,
            head_buf, ctx.len, NULL, 0);
    }

    /* size까지만 전송!! */
    char chunk[8192];
    long remaining = size;

    while (remaining > 0) {
        size_t want = remaining > (long)sizeof(chunk)
            ? sizeof(chunk)
            : (size_t)remaining;

        size_t read_bytes = fread(chunk, 1, want, fp);

        if (read_bytes == 0) {
            break;
        }

        if (self->conn) {
            HttpConnection_append_send(self->conn,
                (const uint8_t*)chunk, read_bytes);
        } else {
            ssize_t sent = self->socket->send(self->socket,
                chunk, read_bytes, NULL, 0);

            if (sent < 0) {
                break;
            }
        }

        remaining -= (long)read_bytes;
    }

    fclose(fp);

    if (self->conn) {
        HttpConnection_flush(self->conn);
    }
}

static void HttpResponse_finalize(Object* obj) {
    HttpResponse* self = (HttpResponse*)obj;

    if (self->headers) {
        RELEASE(self->headers);
    }
}

static const Class _HttpResponse_Class = {
    .name = "HttpResponse",
    .size = sizeof(HttpResponse),
    .finalize = HttpResponse_finalize
};

HttpResponse* new_HttpResponse(Socket* sock, struct HttpConnection* conn) {
    HttpResponse* self = (HttpResponse*)calloc(1, sizeof(HttpResponse));

    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &_HttpResponse_Class);

    self->socket = sock;
    self->conn = conn;
    self->status_code = 200;
    self->headers = new_HashMap(16);

    if (!self->headers) {
        RELEASE(self);
        return NULL;
    }

    self->setStatus = impl_setStatus;
    self->sendStatus = impl_sendStatus;
    self->sendText = impl_sendText;
    self->sendJson = impl_sendJson;
    self->sendFile = impl_sendFile;
    self->setHeader = impl_setHeader;
    self->redirect = impl_redirect;

    return self;
}