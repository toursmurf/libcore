#ifndef HTTP_MESSAGE_H
#define HTTP_MESSAGE_H

#include "object.h"
#include "string_obj.h"
#include "hashmap.h"
#include "json.h"
#include "socket_base.h"
#include "multipart_parser.h"

/* 순환 include 없이 HttpConnection 포인터 사용을 위한 전방 선언 */
struct HttpConnection;

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

    /* 🚀 [v1.7.1 신규] Express.js 스타일 동적 라우팅 파라미터 (예: /board/:id) */
    HashMap* params;

    JSONNode* json;
    HashMap* form;

    MultipartResult* multipart; /* [OWNED] multipart/form-data 파싱 결과 */
    void*            body;       /* [OWNED] raw body bytes */
    size_t           body_len;   /* body 실제 바이트 수 */
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
    struct HttpConnection* conn; /* [BORROWED] append_out_buf+flush 경로용 */

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

HttpResponse* new_HttpResponse(Socket* sock, struct HttpConnection* conn);
const char* Http_statusMessage(int code);

#endif /* HTTP_MESSAGE_H *