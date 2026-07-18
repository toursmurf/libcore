#include "http_client.h"
#include "http_transport.h"
#include "http_response_parser.h"
#include "http_request_builder.h"
#include "multipart_writer.h"
#include "string_builder.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

/* --- [메소드 프로토타입] --- */
static HttpClientResponse* impl_execute(HttpClient* self, HttpClientRequest* req);
static HttpClientResponse* impl_GET(HttpClient* self, const char* url, HashMap* query_params);
static HttpClientResponse* impl_POST(HttpClient* self, const char* url, HashMap* data, PayloadType type);
static HttpClientResponse* impl_PUT(HttpClient* self, const char* url, HashMap* data, PayloadType type);
static HttpClientResponse* impl_DELETE(HttpClient* self, const char* url);
static HttpClientResponse* impl_POST_RAW(HttpClient* self, const char* url, const void* body, size_t body_len, const char* content_type);
static HttpClientResponse* impl_POST_MULTIPART(HttpClient* self, const char* url, HashMap* data, HashMap* files);
static void impl_setHeader(HttpClient* self, const char* key, const char* value);
static void impl_setBearerToken(HttpClient* self, const char* token);

/* --- [HttpClient 생명주기] --- */
static void HttpClient_finalize(Object* obj) {
    HttpClient* self = (HttpClient*)obj;
    if (self->default_headers) RELEASE((Object*)self->default_headers);
    if (self->cookie_jar) RELEASE((Object*)self->cookie_jar);
}

static const Class _HttpClient_Class = {
    .name = "HttpClient",
    .size = sizeof(HttpClient),
    .finalize = HttpClient_finalize
};

HttpClient* new_HttpClient(EventLoop* loop) {
    HttpClient* self = (HttpClient*)calloc(1, sizeof(HttpClient));
    if (!self) return NULL;
    Object_Init((Object*)self, &_HttpClient_Class);
    self->loop = loop;
    self->default_headers = new_HashMap(16);
    self->cookie_jar = new_ArrayList(4);
    if (!self->default_headers || !self->cookie_jar) {
        RELEASE((Object*)self);
        return NULL;
    }
    self->options.timeout_ms = 30000;
    self->options.max_redirects = 5;
    self->options.follow_redirects = true;

    self->execute        = impl_execute;
    self->GET            = impl_GET;
    self->POST           = impl_POST;
    self->PUT            = impl_PUT;
    self->DELETE         = impl_DELETE;
    self->POST_RAW       = impl_POST_RAW;
    self->POST_MULTIPART = impl_POST_MULTIPART;
    self->setHeader      = impl_setHeader;
    self->setBearerToken = impl_setBearerToken;
    return self;
}

static HttpClientResponse* impl_GET(HttpClient* self, const char* url, HashMap* query_params) {
    (void)query_params;
    HttpClientRequest req = { .method = "GET", .url = url, .payload_type = PAYLOAD_NONE };
    return self->execute(self, &req);
}

static HttpClientResponse* impl_POST(HttpClient* self, const char* url, HashMap* data, PayloadType type) {
    HttpClientRequest req = { .method = "POST", .url = url, .payload_type = type, .data = data };
    return self->execute(self, &req);
}

static HttpClientResponse* impl_PUT(HttpClient* self, const char* url, HashMap* data, PayloadType type) {
    HttpClientRequest req = { .method = "PUT", .url = url, .payload_type = type, .data = data };
    return self->execute(self, &req);
}

static HttpClientResponse* impl_DELETE(HttpClient* self, const char* url) {
    HttpClientRequest req = { .method = "DELETE", .url = url, .payload_type = PAYLOAD_NONE };
    return self->execute(self, &req);
}

static HttpClientResponse* impl_POST_RAW(HttpClient* self, const char* url, const void* body, size_t body_len, const char* content_type) {
    HttpClientRequest req = { .method = "POST", .url = url, .payload_type = PAYLOAD_RAW, .raw_body = body, .raw_body_len = body_len, .raw_content_type = content_type };
    return self->execute(self, &req);
}

static HttpClientResponse* impl_POST_MULTIPART(HttpClient* self, const char* url, HashMap* data, HashMap* files) {
    HttpClientRequest req = { .method = "POST", .url = url, .payload_type = PAYLOAD_MULTIPART, .data = data, .files = files };
    return self->execute(self, &req);
}

static void impl_setHeader(HttpClient* self, const char* key, const char* value) {
    if (!self || !key || !value) return;
    String* k = new_String(key);
    String* v = new_String(value);
    if (k && v) { self->default_headers->put(self->default_headers, k->c_str(k), (Object*)v); }
    if (k) RELEASE((Object*)k);
    if (v) RELEASE((Object*)v);
}

static void impl_setBearerToken(HttpClient* self, const char* token) {
    if (!self || !token) return;
    char buf[4096];
    snprintf(buf, sizeof(buf), "Bearer %s", token);
    self->setHeader(self, "Authorization", buf);
}

static void update_cookie_jar(ArrayList* jar, HttpCookie* new_c) {
    if (!jar || !new_c || !new_c->name) return;
    int sz = jar->getSize(jar);
    for (int i = 0; i < sz; i++) {
        HttpCookie* c = (HttpCookie*)jar->get(jar, i);
        if (!c || !c->name) continue;
        if (strcmp(c->name, new_c->name) == 0 &&
            ((!c->domain && !new_c->domain) || (c->domain && new_c->domain && strcasecmp(c->domain, new_c->domain) == 0)) &&
            ((!c->path && !new_c->path) || (c->path && new_c->path && strcmp(c->path, new_c->path) == 0))) {
            jar->removeResult(jar, i);
            break;
        }
    }
    jar->add(jar, (Object*)new_c);
}

static char* _build_raw_request(HttpClient* self, HttpClientRequest* req, HttpTransport* transport, size_t* out_len, char* out_boundary) {
    StringBuilder* sb = new_StringBuilder(1024);
    if (!sb) return NULL;
    sb->appendFormat(sb, "%s %s HTTP/1.1\r\n", req->method, transport->path);
    sb->appendFormat(sb, "Host: %s\r\n", transport->host);
    bool has_ua = self->default_headers && self->default_headers->get(self->default_headers, "User-Agent");
    bool has_accept = self->default_headers && self->default_headers->get(self->default_headers, "Accept");
    if (!has_ua) sb->append(sb, "User-Agent: WebCore-Client/1.6\r\n");
    if (!has_accept) sb->append(sb, "Accept: */*\r\n");
    if (self->default_headers) {
        ArrayList* keys = self->default_headers->keys(self->default_headers);
        if (keys) {
            for (int i = 0; i < keys->getSize(keys); i++) {
                String* k = (String*)keys->get(keys, i);
                String* v = (String*)self->default_headers->get(self->default_headers, k->c_str(k));
                if (k && v) {
                    char tmp[4096];
                    snprintf(tmp, sizeof(tmp), "%s: %s\r\n", k->c_str(k), v->c_str(v));
                    sb->append(sb, tmp);
                }
            }
            RELEASE((Object*)keys);
        }
    }
    if (self->cookie_jar && self->cookie_jar->getSize(self->cookie_jar) > 0) {
        int  csize = self->cookie_jar->getSize(self->cookie_jar);
        bool first = true;
        for (int i = 0; i < csize; i++) {
            HttpCookie* c = (HttpCookie*)self->cookie_jar->get(self->cookie_jar, i);
            if (!c || !c->name || !c->value) continue;
            char tmp[4096];
            if (first) { sb->append(sb, "Cookie: "); snprintf(tmp, sizeof(tmp), "%s=%s", c->name, c->value); first = false; }
            else { snprintf(tmp, sizeof(tmp), "; %s=%s", c->name, c->value); }
            sb->append(sb, tmp);
        }
        if (!first) sb->append(sb, "\r\n");
    }
    char*       body_buf     = NULL;
    size_t      body_len     = 0;
    const char* content_type = NULL;
    bool        owned_body   = false;
    if (req->payload_type == PAYLOAD_FORM) {
        body_buf     = build_form_body(req->data, &body_len);
        content_type = "application/x-www-form-urlencoded";
        owned_body   = true;
    } else if (req->payload_type == PAYLOAD_JSON) {
        body_buf     = build_json_body(req->data, &body_len);
        content_type = "application/json";
        owned_body   = true;
    } else if (req->payload_type == PAYLOAD_RAW) {
        body_buf     = (char*)req->raw_body;
        body_len     = req->raw_body_len;
        content_type = req->raw_content_type;
        owned_body   = false;
    } else if (req->payload_type == PAYLOAD_MULTIPART) {
        generate_multipart_boundary(out_boundary, 64);
        char tmp_ct[128];
        snprintf(tmp_ct, sizeof(tmp_ct), "multipart/form-data; boundary=%s", out_boundary);
        sb->appendFormat(sb, "Content-Type: %s\r\n", tmp_ct);
        ssize_t calc_len = (ssize_t)MultipartWriter_calculate_length(req->data, req->files, out_boundary);
        if (calc_len < 0) { RELEASE((Object*)sb); return NULL; }
        sb->appendFormat(sb, "Content-Length: %zu\r\n", (size_t)calc_len);
    }
    if (req->payload_type != PAYLOAD_MULTIPART) {
        if (body_len > 0 && content_type) {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "Content-Type: %s\r\n", content_type);
            sb->append(sb, tmp);
            snprintf(tmp, sizeof(tmp), "Content-Length: %zu\r\n", body_len);
            sb->append(sb, tmp);
        } else { sb->append(sb, "Content-Length: 0\r\n"); }
    }
    sb->append(sb, "Connection: close\r\n\r\n");
    if (body_buf && body_len > 0) sb->appendBytes(sb, body_buf, body_len);
    *out_len = sb->length(sb);
    char* req_buf = safe_strdup(sb->c_str(sb), *out_len);
    if (owned_body && body_buf) free(body_buf);
    RELEASE((Object*)sb);
    return req_buf;
}

static HttpClientResponse* impl_execute(HttpClient* self, HttpClientRequest* req) {
    if (!self || !req || !req->url) return NULL;
    char* current_url = safe_strdup(req->url, 2048);
    if (!current_url) return NULL;
    int redirect_count = 0;
    HttpClientResponse* final_res = NULL;
    while (redirect_count <= self->options.max_redirects) {
        HttpTransport* transport = HttpTransport_connect(current_url, NULL, self->options.timeout_ms);
        if (!transport) break;
        size_t req_len = 0;
        char boundary[64] = {0};
        char* req_buf = _build_raw_request(self, req, transport, &req_len, boundary);
        if (!req_buf) { HttpTransport_close(transport); break; }
        if (HttpTransport_send(transport, req_buf, req_len) < 0) { free(req_buf); HttpTransport_close(transport); break; }
        free(req_buf);
        if (req->payload_type == PAYLOAD_MULTIPART) {
            if (MultipartWriter_stream_send(req->data, req->files, boundary, transport) < 0) { HttpTransport_close(transport); break; }
        }
        final_res = HttpResponseParser_parse(transport);
        HttpTransport_close(transport);
        if (!final_res) break;
        if (final_res->cookies && self->cookie_jar) {
            for (int i = 0; i < final_res->cookies->getSize(final_res->cookies); i++) {
                String* ck_str = (String*)final_res->cookies->get(final_res->cookies, i);
                if (!ck_str || !ck_str->c_str) continue;
                HttpCookie* cookie = new_HttpCookie(ck_str->c_str(ck_str));
                if (cookie) { update_cookie_jar(self->cookie_jar, cookie); RELEASE((Object*)cookie); }
            }
        }
        if (final_res && (final_res->status_code == 301 || final_res->status_code == 302 || final_res->status_code == 307 || final_res->status_code == 308)) {
            if (self->options.follow_redirects) {
                const char* loc = hashmap_get_str(final_res->headers, "Location");
                if (loc) {
                    char* next_url = safe_strdup(loc, 2048);
                    if (!next_url) break;
                    free(current_url); current_url = next_url; RELEASE((Object*)final_res); final_res = NULL; redirect_count++; continue;
                }
            }
        }
        break;
    }
    free(current_url);
    return final_res;
}

static void HttpClientResponse_finalize(Object* obj) {
    HttpClientResponse* res = (HttpClientResponse*)obj;
    if (res->headers) RELEASE((Object*)res->headers);
    if (res->cookies) RELEASE((Object*)res->cookies);
    if (res->body) free(res->body);
}

static const Class _HttpClientResponse_Class = {
    .name = "HttpClientResponse",
    .size = sizeof(HttpClientResponse),
    .finalize = HttpClientResponse_finalize
};

HttpClientResponse* new_HttpClientResponse(void) {
    HttpClientResponse* res = (HttpClientResponse*)calloc(1, sizeof(HttpClientResponse));
    if (!res) return NULL;
    Object_Init((Object*)res, &_HttpClientResponse_Class);
    res->headers = new_HashMap(16);
    res->cookies = new_ArrayList(4);
    return res;
}

static void HttpCookie_finalize(Object* obj) {
    HttpCookie* c = (HttpCookie*)obj;
    free(c->name); free(c->value); free(c->domain); free(c->path);
}

static const Class _HttpCookie_Class = {
    .name = "HttpCookie",
    .size = sizeof(HttpCookie),
    .finalize = HttpCookie_finalize
};

HttpCookie* new_HttpCookie(const char* raw_str) {
    HttpCookie* c = (HttpCookie*)calloc(1, sizeof(HttpCookie));
    if (!c) return NULL;
    Object_Init((Object*)c, &_HttpCookie_Class);
    if (raw_str) {
        char* dup = strdup(raw_str);
        char* eq = strchr(dup, '=');
        if (eq) { *eq = '\0'; c->name = strdup(dup); c->value = strdup(eq + 1); }
        free(dup);
    }
    return c;
}