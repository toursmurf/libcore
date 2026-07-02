#ifndef HTTP_MESSAGE_H
#define HTTP_MESSAGE_H

#include "object.h"
#include "string_obj.h"
#include "hashmap.h"
#include "json.h"
#include "socket_base.h"

/* HTTP 메서드 완벽 분류 */
typedef enum {
    HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE,
    HTTP_PATCH, HTTP_OPTIONS, HTTP_HEAD, HTTP_UNKNOWN
} HttpMethod;

/* =========================================================
* [1] HttpRequest : 파싱 지옥을 방어하는 절대 방패
* ========================================================= */
typedef struct HttpRequest {
    Object base;

    HttpMethod method;
    String* path;

    HashMap* headers;
    HashMap* query;

    JSONNode* json;
    HashMap* form;

    void* multipart;
    void* body;
} HttpRequest;

HttpRequest* new_HttpRequest(void);

/* =========================================================
* [2] HttpResponse : Express.js를 압살할 지능형 응답 함포
* ========================================================= */
typedef struct HttpResponse HttpResponse;
struct HttpResponse {
    Object base;

    /* 🚨 [BORROWED] 절대 RELEASE 금지! HttpConnection이 소유함 */
    Socket* socket;

    int status_code;
    HashMap* headers;

    /* 🚀 Express.js 완벽 대응 VTable */
    void (*setStatus)(HttpResponse* self, int code);
    void (*sendStatus)(HttpResponse* self, int code);
    void (*sendText)(HttpResponse* self, const char* text);
    void (*sendJson)(HttpResponse* self, JSONNode* json);
    void (*sendFile)(HttpResponse* self, const char* path);
    void (*setHeader)(HttpResponse* self, const char* key, const char* value);
    void (*redirect)(HttpResponse* self, const char* url);
};

HttpResponse* new_HttpResponse(Socket* sock);
const char* Http_statusMessage(int code);

#endif /* HTTP_MESSAGE_H */