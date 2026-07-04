#include "http_client.h"
#include "http_transport.h"
#include "http_request_builder.h"
#include "multipart_writer.h"
#include "http_response_parser.h"
#include "arraylist.h"
#include "string_obj.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* =========================================================
 * 🚨 ARC 객체 파괴자
 * ========================================================= */
static void HttpMultipartFile_finalize(Object* obj) {
    HttpMultipartFile* self = (HttpMultipartFile*)obj;
    if (self->filename) RELEASE((Object*)self->filename);
    if (self->content_type) RELEASE((Object*)self->content_type);
    if (self->data) free(self->data);
}
static const Class _HttpMultipartFile_Class = { .name = "HttpMultipartFile", .size = sizeof(HttpMultipartFile), .finalize = HttpMultipartFile_finalize };

HttpMultipartFile* new_HttpMultipartFile(const char* filename, const char* content_type, const void* data, size_t size) {
    HttpMultipartFile* self = (HttpMultipartFile*)calloc(1, sizeof(HttpMultipartFile));
    if (!self) return NULL;
    Object_Init((Object*)self, &_HttpMultipartFile_Class);

    self->filename = new_String(filename ? filename : "unknown.bin");
    if (!self->filename) { RELEASE((Object*)self); return NULL; }

    self->content_type = content_type ? new_String(content_type) : NULL;
    self->size = size;

    if (data && size > 0) {
        self->data = malloc(size);
        if (!self->data) { /* 🚨 NULL 방어막 완비 */
            RELEASE((Object*)self);
            return NULL;
        }
        memcpy(self->data, data, size);
    }
    return self;
}

static void HttpCookie_finalize(Object* obj) {
    HttpCookie* self = (HttpCookie*)obj;
    if (self->name) free(self->name);
    if (self->value) free(self->value);
    if (self->domain) free(self->domain);
    if (self->path) free(self->path);
}
static const Class _HttpCookie_Class = { .name = "HttpCookie", .size = sizeof(HttpCookie), .finalize = HttpCookie_finalize };

HttpCookie* new_HttpCookie(const char* raw_str) {
    if (!raw_str) return NULL;
    HttpCookie* cookie = (HttpCookie*)calloc(1, sizeof(HttpCookie));
    if (!cookie) return NULL;
    Object_Init((Object*)cookie, &_HttpCookie_Class);

    char* copy = strdup(raw_str);
    if (!copy) { RELEASE((Object*)cookie); return NULL; }

    char* saveptr;
    char* token = strtok_r(copy, ";", &saveptr);
    int is_first = 1;

    while (token) {
        while (isspace((unsigned char)*token)) token++;
        char* eq = strchr(token, '=');

        if (is_first) {
            if (eq) {
                *eq = '\0';
                cookie->name = strdup(token);
                cookie->value = strdup(eq + 1);
            }
            is_first = 0;
        } else {
            if (eq) {
                *eq = '\0';
                char* k = token; char* v = eq + 1;
                while (isspace((unsigned char)*v)) v++;
                if (strcasecmp(k, "Path") == 0) cookie->path = strdup(v);
                else if (strcasecmp(k, "Domain") == 0) cookie->domain = strdup(v);
            } else {
                if (strcasecmp(token, "Secure") == 0) cookie->secure = true;
                else if (strcasecmp(token, "HttpOnly") == 0) cookie->http_only = true;
            }
        }
        token = strtok_r(NULL, ";", &saveptr);
    }
    free(copy);
    if (!cookie->name) { RELEASE((Object*)cookie); return NULL; }
    return cookie;
}

static void HttpClientResponse_finalize(Object* obj) {
    HttpClientResponse* self = (HttpClientResponse*)obj;
    if (self->headers) RELEASE((Object*)self->headers);
    if (self->cookies) RELEASE((Object*)self->cookies);
    if (self->body) free(self->body);
}
static const Class _HttpClientResponse_Class = { .name = "HttpClientResponse", .size = sizeof(HttpClientResponse), .finalize = HttpClientResponse_finalize };

HttpClientResponse* new_HttpClientResponse(void) {
    HttpClientResponse* self = (HttpClientResponse*)calloc(1, sizeof(HttpClientResponse));
    if (!self) return NULL;
    Object_Init((Object*)self, &_HttpClientResponse_Class);
    self->headers = new_HashMap(16);
    self->cookies = new_ArrayList(4);
    if (!self->headers || !self->cookies) { RELEASE((Object*)self); return NULL; }
    return self;
}

static void HttpClient_finalize(Object* obj) {
    HttpClient* self = (HttpClient*)obj;
    if (self->default_headers) RELEASE((Object*)self->default_headers);
    if (self->cookie_jar) RELEASE((Object*)self->cookie_jar);
}
static const Class _HttpClient_Class = { .name = "HttpClient", .size = sizeof(HttpClient), .finalize = HttpClient_finalize };

static void impl_setHeader(HttpClient* self, const char* key, const char* value) {
    if (!self || !key || !value) return;
    String* k = new_String(key); String* v = new_String(value);
    if (k && v) self->default_headers->put(self->default_headers, k->c_str(k), (Object*)v);
    if (k) RELEASE((Object*)k);
    if (v) RELEASE((Object*)v);
}
static void impl_setBearerToken(HttpClient* self, const char* token) {
    if (!self || !token) return;
    char buf[4096]; snprintf(buf, sizeof(buf), "Bearer %s", token);
    self->setHeader(self, "Authorization", buf);
}

/* =========================================================
 * 🚨 [V1.5] 지능형 쿠키 정책
 * ========================================================= */
static void update_cookie_jar(ArrayList* jar, HttpCookie* new_c) {
    if (!jar || !new_c) return;
    for (int i = 0; i < jar->getSize(jar); i++) {
        HttpCookie* c = (HttpCookie*)jar->get(jar, i);
        if (strcmp(c->name, new_c->name) == 0 &&
            ((!c->domain && !new_c->domain) || (c->domain && new_c->domain && strcasecmp(c->domain, new_c->domain) == 0)) &&
            ((!c->path && !new_c->path) || (c->path && new_c->path && strcmp(c->path, new_c->path) == 0))) {
            jar->removeResult(jar, i);
            break;
        }
    }
    jar->add(jar, (Object*)new_c);
}

static bool cookie_matches(HttpCookie* c, const char* host, const char* path) {
    if (c->domain) {
        size_t h_len = strlen(host), d_len = strlen(c->domain);
        if (strcasecmp(c->domain, host) != 0) {
            if (c->domain[0] != '.' || h_len <= d_len || strcasecmp(host + h_len - d_len, c->domain) != 0) return false;
        }
    }
    if (c->path) {
        size_t p_len = strlen(c->path);
        if (strncmp(path, c->path, p_len) != 0) return false;
    }
    return true;
}

/* =========================================================
 * 🚨 [V1.5] 통합 오케스트레이터 엔진
 * ========================================================= */
static HttpClientResponse* impl_execute(HttpClient* self, HttpClientRequest* req) {
    if (!self || !req || !req->url) return NULL;

    char current_url[2048];
    strncpy(current_url, req->url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    int redirect_count = 0;
    HttpClientResponse* res = NULL;

    while (redirect_count <= self->options.max_redirects) {
        HttpTransport* transport = HttpTransport_connect(current_url);
        if (!transport) return NULL;

        char* body_buf = NULL;
        size_t body_len = 0;
        const char* content_type = NULL;
        char boundary[64] = {0};

        bool owned_buf = false;

        if (req->payload_type == PAYLOAD_FORM) {
            body_buf = build_form_body(req->data, &body_len);
            content_type = "application/x-www-form-urlencoded";
            owned_buf = true;
        } else if (req->payload_type == PAYLOAD_JSON) {
            body_buf = build_json_body(req->data, &body_len);
            content_type = "application/json";
            owned_buf = true;
        } else if (req->payload_type == PAYLOAD_RAW) {
            body_buf = (char*)req->raw_body;
            body_len = req->raw_body_len;
            content_type = req->raw_content_type;
            owned_buf = false;
        } else if (req->payload_type == PAYLOAD_MULTIPART) {
            generate_multipart_boundary(boundary, sizeof(boundary));
            ssize_t clen = MultipartWriter_calculate_length(req->data, req->files, boundary);
            if (clen < 0) { HttpTransport_close(transport); return NULL; }
            body_len = (size_t)clen;
            owned_buf = false;
        }

        char local_ct_buf[128] = {0};
        if (req->payload_type == PAYLOAD_MULTIPART) {
            snprintf(local_ct_buf, sizeof(local_ct_buf), "multipart/form-data; boundary=%s", boundary);
            content_type = local_ct_buf;
        }

        char path_copy[1024];
        strncpy(path_copy, transport->path, sizeof(path_copy)-1);
        path_copy[sizeof(path_copy)-1] = '\0';
        normalize_path(path_copy);

        String* head_sb = new_String("");
        if (!head_sb) {
            HttpTransport_close(transport);
            if (owned_buf && body_buf) free(body_buf);
            return NULL;
        }

        char tmp[8192];
        snprintf(tmp, sizeof(tmp), "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: WebCore-Client/1.5\r\nAccept: */*\r\n",
                 req->method, path_copy, transport->host);
        head_sb->append(head_sb, tmp);

        if (self->options.enable_compression) {
            head_sb->append(head_sb, "Accept-Encoding: gzip, deflate\r\n");
        }

        if (self->default_headers) {
            ArrayList* keys = self->default_headers->keys(self->default_headers);
            if (keys) {
                for (int i = 0; i < keys->getSize(keys); i++) {
                    String* k = (String*)keys->get(keys, i);
                    String* v = (String*)self->default_headers->get(self->default_headers, k->c_str(k));
                    if (k && v) {
                        snprintf(tmp, sizeof(tmp), "%s: %s\r\n", k->c_str(k), v->c_str(v));
                        head_sb->append(head_sb, tmp);
                    }
                }
                RELEASE((Object*)keys);
            }
        }

        if (self->cookie_jar && self->cookie_jar->getSize(self->cookie_jar) > 0) {
            int sent_cookies = 0;
            String* cookie_str = new_String("Cookie: ");
            if (cookie_str) {
                for (int i = 0; i < self->cookie_jar->getSize(self->cookie_jar); i++) {
                    HttpCookie* c = (HttpCookie*)self->cookie_jar->get(self->cookie_jar, i);
                    if (c && c->name && c->value && cookie_matches(c, transport->host, path_copy)) {
                        if (sent_cookies > 0) cookie_str->append(cookie_str, "; ");
                        snprintf(tmp, sizeof(tmp), "%s=%s", c->name, c->value);
                        cookie_str->append(cookie_str, tmp);
                        sent_cookies++;
                    }
                }
                if (sent_cookies > 0) {
                    cookie_str->append(cookie_str, "\r\n");
                    head_sb->append(head_sb, cookie_str->c_str(cookie_str));
                }
                RELEASE((Object*)cookie_str);
            }
        }

        bool expect_100 = (req->payload_type == PAYLOAD_MULTIPART && self->options.expect_100_continue);
        if (expect_100) head_sb->append(head_sb, "Expect: 100-continue\r\n");

        if (body_len > 0 && content_type) {
            snprintf(tmp, sizeof(tmp), "Content-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", content_type, body_len);
        } else {
            snprintf(tmp, sizeof(tmp), "Content-Length: 0\r\nConnection: close\r\n\r\n");
        }
        head_sb->append(head_sb, tmp);

        if (HttpTransport_send(transport, head_sb->c_str(head_sb), head_sb->length_f(head_sb)) < 0) {
            RELEASE((Object*)head_sb);
            HttpTransport_close(transport);
            if (owned_buf && body_buf) free(body_buf);
            return NULL;
        }
        RELEASE((Object*)head_sb);

        if (expect_100) {
            char status_line[128];
            HttpTransport_recv_line(transport, status_line, sizeof(status_line));
            if (strstr(status_line, "100 Continue")) {
                HttpTransport_recv_line(transport, status_line, sizeof(status_line));
                if (MultipartWriter_stream_send(req->data, req->files, boundary, transport) < 0) {
                    HttpTransport_close(transport);
                    return NULL;
                }
                res = HttpResponseParser_parse(transport);
            } else {
                res = HttpResponseParser_parse_with_status(transport, status_line);
            }
        } else {
            if (req->payload_type == PAYLOAD_MULTIPART) {
                if (MultipartWriter_stream_send(req->data, req->files, boundary, transport) < 0) {
                    HttpTransport_close(transport);
                    return NULL;
                }
            } else if (body_len > 0 && body_buf) {
                if (HttpTransport_send(transport, body_buf, body_len) < 0) {
                    HttpTransport_close(transport);
                    if (owned_buf && body_buf) free(body_buf);
                    return NULL;
                }
            }
            res = HttpResponseParser_parse(transport);
        }

        if (owned_buf && body_buf) {
            free(body_buf);
            body_buf = NULL;
        }

        char last_scheme[16]; char last_host[256]; int last_port = transport->port;
        strncpy(last_scheme, transport->scheme, sizeof(last_scheme) - 1);
        strncpy(last_host, transport->host, sizeof(last_host) - 1);

        HttpTransport_close(transport);
        if (!res) break;

        if (res->cookies && res->cookies->getSize(res->cookies) > 0) {
            for (int i = 0; i < res->cookies->getSize(res->cookies); i++) {
                String* ck_str = (String*)res->cookies->get(res->cookies, i);
                HttpCookie* cookie = new_HttpCookie(ck_str->c_str(ck_str));
                if (cookie) {
                    update_cookie_jar(self->cookie_jar, cookie);
                    RELEASE((Object*)cookie);
                }
            }
        }

        if (self->options.follow_redirects &&
           (res->status_code == 301 || res->status_code == 302 || res->status_code == 307 || res->status_code == 308)) {
            const char* loc = hashmap_get_str(res->headers, "Location");
            if (loc) {
                if (loc[0] == '/') {
                    snprintf(current_url, sizeof(current_url), "%s://%s:%d%s", last_scheme, last_host, last_port, loc);
                } else if (strncmp(loc, "http", 4) == 0) {
                    strncpy(current_url, loc, sizeof(current_url) - 1);
                } else {
                    char* last_slash = strrchr(path_copy, '/');
                    if (last_slash) *(last_slash + 1) = '\0';
                    snprintf(current_url, sizeof(current_url), "%s://%s:%d%s/%s", last_scheme, last_host, last_port, path_copy, loc);
                    normalize_path(current_url);
                }
                current_url[sizeof(current_url) - 1] = '\0';

                RELEASE((Object*)res);
                redirect_count++;
                continue;
            }
        }
        break;
    }

    return res;
}

/* =========================================================
 * VTable 래퍼 모음
 * ========================================================= */
static HttpClientResponse* impl_GET(HttpClient* self, const char* url, HashMap* query_params) {
    String* s = new_String(url);
    if (query_params && query_params->getSize(query_params) > 0) {
        s->append(s, strchr(url, '?') ? "&" : "?");
        size_t ignore_len;
        char* qs = build_form_body(query_params, &ignore_len);
        if (qs) { s->append(s, qs); free(qs); }
    }
    HttpClientRequest req = { .method = "GET", .url = s->c_str(s), .payload_type = PAYLOAD_NONE };
    HttpClientResponse* res = self->execute(self, &req);
    RELEASE((Object*)s);
    return res;
}
static HttpClientResponse* impl_POST(HttpClient* self, const char* url, HashMap* data, PayloadType type) {
    HttpClientRequest req = { .method = "POST", .url = url, .data = data, .payload_type = type };
    return self->execute(self, &req);
}
static HttpClientResponse* impl_PUT(HttpClient* self, const char* url, HashMap* data, PayloadType type) {
    HttpClientRequest req = { .method = "PUT", .url = url, .data = data, .payload_type = type };
    return self->execute(self, &req);
}
static HttpClientResponse* impl_DELETE(HttpClient* self, const char* url) {
    HttpClientRequest req = { .method = "DELETE", .url = url, .payload_type = PAYLOAD_NONE };
    return self->execute(self, &req);
}
static HttpClientResponse* impl_POST_RAW(HttpClient* self, const char* url, const void* body, size_t body_len, const char* content_type) {
    HttpClientRequest req = { .method = "POST", .url = url, .raw_body = body, .raw_body_len = body_len, .raw_content_type = content_type, .payload_type = PAYLOAD_RAW };
    return self->execute(self, &req);
}
static HttpClientResponse* impl_POST_MULTIPART(HttpClient* self, const char* url, HashMap* data, HashMap* files) {
    HttpClientRequest req = { .method = "POST", .url = url, .data = data, .files = files, .payload_type = PAYLOAD_MULTIPART };
    return self->execute(self, &req);
}

HttpClient* new_HttpClient(EventLoop* loop) {
    HttpClient* self = (HttpClient*)calloc(1, sizeof(HttpClient));
    if (!self) return NULL;
    Object_Init((Object*)self, &_HttpClient_Class);

    self->loop = loop;
    self->default_headers = new_HashMap(16);
    self->cookie_jar = new_ArrayList(16);
    if (!self->default_headers || !self->cookie_jar) { RELEASE((Object*)self); return NULL; }

    self->options.timeout_ms = 5000;
    self->options.max_redirects = 5;
    self->options.follow_redirects = true;
    self->options.enable_compression = false;
    self->options.expect_100_continue = false;

    self->setHeader      = impl_setHeader;
    self->setBearerToken = impl_setBearerToken;
    self->execute        = impl_execute;
    self->GET            = impl_GET;
    self->POST           = impl_POST;
    self->PUT            = impl_PUT;
    self->DELETE         = impl_DELETE;
    self->POST_RAW       = impl_POST_RAW;
    self->POST_MULTIPART = impl_POST_MULTIPART;

    return self;
}