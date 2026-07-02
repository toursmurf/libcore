#include "router.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* 🚀 심장부: 패킷 디스패처 */
static void impl_dispatch(Router* self, HttpRequest* req, HttpResponse* res) {
    if (!self || !req || !res) return;

    int matched = 0;
    int route_count = self->routes->getSize(self->routes);

    for (int i = 0; i < route_count; i++) {
        /* 🚨 get()은 BORROWED 객체를 반환하므로 절대 해제 금지! */
        Route* r = (Route*)self->routes->get(self->routes, i);
        if (!r) continue;

        /* 1단계: HTTP 메서드 매칭 */
        if (r->method != req->method && r->method != HTTP_UNKNOWN) {
            continue;
        }

        /* 2단계: URL Path 매칭 */
        const char* req_path_str = req->path ? req->path->c_str(req->path) : "/";
        const char* route_path_str = r->path ? r->path->c_str(r->path) : "/";

        if (strcmp(req_path_str, route_path_str) == 0) {
            if (r->handler) {
                /* 🚨 [Rule 6] user_ctx (BORROWED) 핸들러 관통 주입! */
                r->handler(req, res, self->user_ctx);
            }
            matched = 1;
            break;
        }
    }

    /* 🚨 [클순 마님(🔫) 패치] 404 처리 시 이중 전송(Protocol 오염) 완벽 방지! */
    if (!matched) {
        res->setStatus(res, 404);  /* 상태만 404로 장전 (헤더 발사 안 함) */
        res->sendText(res, "404 Not Found (Tus IT Engine)"); /* 단발 사격(1회 발사) */
    }
}

/* =========================================================
 * [3] Router 생성자 및 소멸자
 * ========================================================= */
static void Router_finalize(Object* obj) {
    Router* self = (Router*)obj;

    /* 🚨 user_ctx는 [BORROWED] 상태이므로 여기서 절대 해제하지 않음! */
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

    /* 🚀 외부 컨텍스트(DB/Config) [BORROWED] 저장 */
    self->user_ctx = user_ctx;

    self->addRoute = impl_addRoute;
    self->GET      = impl_GET;
    self->POST     = impl_POST;
    self->PUT      = impl_PUT;
    self->DELETE   = impl_DELETE;
    self->dispatch = impl_dispatch;

    return self;
}