#include "router.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================
 * ✨ [글로벌 에러 응답기] 모든 요청에서 재사용 가능한 표준 에러 출력 ✨
 * 브라우저의 'Friendly Error Pages' 덮어쓰기를 원천 차단하는 512 Bytes 패딩 포함
 * ========================================================= */
void Router_sendError(HttpResponse* res, int status, const char* error_code, const char* message) {
    if (!res) return;

    res->setStatus(res, status);
    res->setHeader(res, "Content-Type", "text/html; charset=utf-8");

    char buffer[2048];
    snprintf(buffer, sizeof(buffer),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <title>%d Error - Tus IT Engine</title>\n"
        "</head>\n"
        "<body style=\"background-color: #1e1e1e; color: #00ff00; font-family: 'Courier New', Courier, monospace; padding: 40px;\">\n"
        "  <h1 style=\"color: #ff5555;\">🚨 %d %s</h1>\n"
        "  <p style=\"color: #cccccc; font-size: 16px;\">Tus IT Engine API Error Encountered.</p>\n"
        "  <div style=\"background-color: #2d2d2d; padding: 20px; border-radius: 5px; border-left: 5px solid #ff5555; margin-top: 20px;\">\n"
        "    <pre style=\"color: #e0e0e0; font-size: 14px; margin: 0;\">\n"
        "{\n"
        "  \"error\": \"%s\",\n"
        "  \"message\": \"%s\",\n"
        "  \"status\": %d\n"
        "}\n"
        "    </pre>\n"
        "  </div>\n"
        "  <!-- [Browser Bypass Padding] \n"
        "       Chrome and Edge browsers tend to override server error responses \n"
        "       with their own default friendly error pages if the response body \n"
        "       is smaller than 512 bytes. To ensure that this custom API error \n"
        "       message is correctly displayed to the client or frontend application, \n"
        "       we are padding this payload with additional text to exceed the \n"
        "       512-byte threshold. This guarantees absolute visibility of our API \n"
        "       rejection details! \n"
        "       ....................................................................... \n"
        "       ....................................................................... \n"
        "       ....................................................................... \n"
        "  -->\n"
        "</body>\n"
        "</html>\n",
        status, status, error_code, error_code, message, status
    );

    res->sendText(res, buffer);
}


/* =========================================================
 * [1] Route 내부 구현부
 * ========================================================= */
static void Route_finalize(Object* obj) {
    Route* self = (Route*)obj;
    if (self->path) {
        RELEASE(self->path);
    }
}

static const Class _Route_Class = {
    .name = "Route",
    .size = sizeof(Route),
    .finalize = Route_finalize
};

Route* new_Route(HttpMethod method, const char* path, HttpHandler handler) {
    Route* self = (Route*)calloc(1, sizeof(Route));
    if (!self) return NULL;

    Object_Init((Object*)self, &_Route_Class);

    self->method = method;
    self->handler = handler;
    self->path = new_String(path ? path : "/");
    if (!self->path) {
        RELEASE(self);
        return NULL;
    }

    return self;
}

/* =========================================================
 * [2] Router VTable 구현부
 * ========================================================= */
static void impl_addRoute(Router* self, HttpMethod method, const char* path, HttpHandler handler) {
    if (!self || !self->routes || !path || !handler) return;

    Route* route = new_Route(method, path, handler);
    if (!route) return;

    self->routes->add(self->routes, (Object*)route);
    RELEASE(route);
}

static void impl_GET(Router* self, const char* path, HttpHandler handler) {
    impl_addRoute(self, HTTP_GET, path, handler);
}

static void impl_POST(Router* self, const char* path, HttpHandler handler) {
    impl_addRoute(self, HTTP_POST, path, handler);
}

static void impl_PUT(Router* self, const char* path, HttpHandler handler) {
    impl_addRoute(self, HTTP_PUT, path, handler);
}

static void impl_DELETE(Router* self, const char* path, HttpHandler handler) {
    impl_addRoute(self, HTTP_DELETE, path, handler);
}

/* ✨ [핵심] Express.js 스타일의 동적 라우팅 매칭 알고리즘 ✨ */
static int match_route(const char* req_path, const char* route_path) {
    const char *p = req_path;
    const char *r = route_path;

    while (*p != '\0' && *r != '\0') {
        if (*r == ':') {
            while (*r != '\0' && *r != '/') r++;

            int param_len = 0;
            while (*p != '\0' && *p != '/') {
                if (!((*p >= 'a' && *p <= 'z') ||
                      (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') ||
                      *p == '-' || *p == '_')) {
                    return 0;
                }
                p++;
                param_len++;
            }

            if (param_len == 0) {
                return 0;
            }
        }
        else if (*p == *r) {
            p++;
            r++;
        }
        else {
            return 0;
        }
    }

    if (*p == '\0' && *r == '\0') return 1;
    if (*p == '/' && *(p+1) == '\0' && *r == '\0') return 1;
    if (*r == '/' && *(r+1) == '\0' && *p == '\0') return 1;

    return 0;
}

/* 🚀 심장부: 패킷 디스패처 */
static void impl_dispatch(Router* self, HttpRequest* req, HttpResponse* res) {
    if (!self || !req || !res) return;

    int matched = 0;
    int route_count = self->routes->getSize(self->routes);

    for (int i = 0; i < route_count; i++) {
        Route* r = (Route*)self->routes->get(self->routes, i);
        if (!r) continue;

        if (r->method != req->method && r->method != HTTP_UNKNOWN) {
            continue;
        }

        const char* req_path_str = req->path ? req->path->c_str(req->path) : "/";
        const char* route_path_str = r->path ? r->path->c_str(r->path) : "/";

        if (match_route(req_path_str, route_path_str)) {
            if (r->handler) {
                r->handler(req, res, self->user_ctx);
            }
            matched = 1;
            break;
        }
    }

    /* 🚨 라우터를 통과하지 못한 요청에 대해 전역 에러 응답기 호출! */
    if (!matched) {
        Router_sendError(res, 404, "Route_Not_Found", "요청하신 URL 경로를 찾을 수 없거나, 잘못된 파라미터가 포함되어 라우터에서 차단되었습니다.");
    }
}

/* =========================================================
 * [3] Router 생성자 및 소멸자
 * ========================================================= */
static void Router_finalize(Object* obj) {
    Router* self = (Router*)obj;

    if (self->routes) {
        RELEASE(self->routes);
    }
}

static const Class _Router_Class = {
    .name = "Router",
    .size = sizeof(Router),
    .finalize = Router_finalize
};

Router* new_Router(void* user_ctx) {
    Router* self = (Router*)calloc(1, sizeof(Router));
    if (!self) return NULL;

    Object_Init((Object*)self, &_Router_Class);

    self->routes = new_ArrayList(32);
    if (!self->routes) {
        RELEASE(self);
        return NULL;
    }

    self->user_ctx = user_ctx;

    self->addRoute = impl_addRoute;
    self->GET      = impl_GET;
    self->POST     = impl_POST;
    self->PUT      = impl_PUT;
    self->DELETE   = impl_DELETE;
    self->dispatch = impl_dispatch;

    return self;
}