#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "object.h"
#include "string_obj.h"
#include "hashmap.h"
#include "arraylist.h"
#include "event_loop.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct HttpMultipartFile {
    Object base;
    String* filename;
    String* content_type;
    void* data;
    size_t size;
} HttpMultipartFile;

HttpMultipartFile* new_HttpMultipartFile(const char* filename, const char* content_type, const void* data, size_t size);

typedef struct HttpCookie {
    Object base;
    char* name;
    char* value;
    char* domain;
    char* path;
    bool secure;
    bool http_only;
} HttpCookie;

HttpCookie* new_HttpCookie(const char* raw_str);

typedef struct {
    int timeout_ms;
    int max_redirects;
    bool follow_redirects;
    bool enable_compression;
    bool expect_100_continue;
} HttpClientOptions;

typedef enum { PAYLOAD_NONE, PAYLOAD_FORM, PAYLOAD_JSON, PAYLOAD_RAW, PAYLOAD_MULTIPART } PayloadType;

typedef struct {
    const char* method;
    const char* url;
    PayloadType payload_type;
    HashMap* data;
    HashMap* files;
    const void* raw_body;
    size_t raw_body_len;
    const char* raw_content_type;
} HttpClientRequest;

typedef struct HttpClientResponse {
    Object base;
    int status_code;
    HashMap* headers;
    ArrayList* cookies;
    char* body;
    size_t body_len;
} HttpClientResponse;

HttpClientResponse* new_HttpClientResponse(void);

typedef struct HttpClient HttpClient;
struct HttpClient {
    Object base;
    EventLoop* loop;
    HttpClientOptions options;
    HashMap* default_headers;
    ArrayList* cookie_jar;

    /* 🚀 client_ring 영구 제거됨 */

    HttpClientResponse* (*execute)(HttpClient* self, HttpClientRequest* req);

    HttpClientResponse* (*GET)   (HttpClient* self, const char* url, HashMap* query_params);
    HttpClientResponse* (*POST)  (HttpClient* self, const char* url, HashMap* data, PayloadType type);
    HttpClientResponse* (*PUT)   (HttpClient* self, const char* url, HashMap* data, PayloadType type);
    HttpClientResponse* (*DELETE)(HttpClient* self, const char* url);
    HttpClientResponse* (*POST_RAW)(HttpClient* self, const char* url, const void* body, size_t body_len, const char* content_type);
    HttpClientResponse* (*POST_MULTIPART)(HttpClient* self, const char* url, HashMap* data, HashMap* files);

    void (*setHeader)(HttpClient* self, const char* key, const char* value);
    void (*setBearerToken)(HttpClient* self, const char* token);
};

HttpClient* new_HttpClient(EventLoop* loop);

void execute_step7_wireless_control(EventLoop* loop);

#endif /* HTTP_CLIENT_H */