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
        self->path = NULL;
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

/* 🚀 심장부: 패킷 디스패처 (멤버 변수 pv 재사용 최적화 및 404 정제 완료) */
static void impl_dispatch(Router* self, HttpRequest* req, HttpResponse* res) {
    if (!self || !req || !res) return;

    const char* original_path = req->path ? req->path->c_str(req->path) : "/";
    char safe_path[MAX_PATH_LEN + 1] = {0}; // 정규화된 경로를 담을 버퍼

    // ✨✨ [1단계: Router 소유의 단일 PathValidator로 즉각 검증] ✨✨
    if (self->pv) {
        bool is_safe = self->pv->validate(self->pv, original_path, safe_path, sizeof(safe_path));

        if (!is_safe) {
            // 비정상/공격 경로는 여기서 400으로 조기 차단!
            Router_sendError(res, 400, "Bad_Request", "보안 정책에 의해 차단된 비정상적인 URL 경로입니다.");
            return;
        }
    } else {
        Router_sendError(res, 500, "Internal_Server_Error", "보안 모듈이 초기화되지 않았습니다.");
        return;
    }

    // ✨✨ [2단계: 안전이 보장된 정규화 경로(safe_path)로 라우팅 매칭 시작] ✨✨
    int matched = 0;
    int route_count = self->routes->getSize(self->routes);

    for (int i = 0; i < route_count; i++) {
        Route* r = (Route*)self->routes->get(self->routes, i);
        if (!r) continue;

        if (r->method != req->method && r->method != HTTP_UNKNOWN) {
            continue;
        }

        const char* route_path_str = r->path ? r->path->c_str(r->path) : "/";

        if (match_route(safe_path, route_path_str)) {
            if (r->handler) {
                r->handler(req, res, self->user_ctx);
            }
            matched = 1;
            break;
        }
    }

    /* 🚨 라우터를 통과하지 못한 요청에 대해 전역 에러 응답기 호출! (의미 정제 완료) */
    if (!matched) {
        Router_sendError(res, 404, "Route_Not_Found", "요청하신 URL 경로와 일치하는 라우트를 찾을 수 없습니다.");
    }
}

/* =========================================================
 * [3] Router 생성자 및 소멸자
 * ========================================================= */
static void Router_finalize(Object* obj) {
    Router* self = (Router*)obj;

    /* 🚨 [안전 처리] 해제 후 NULL 포인터 초기화로 Dangling Pointer 원천 차단 */
    if (self->routes) {
        RELEASE(self->routes);
        self->routes = NULL;
    }
    
    /* ✨ Router 소멸 시 수문장(pv)도 동반 퇴근 (OWNED 해제) ✨ */
    if (self->pv) {
        RELEASE(self->pv);
        self->pv = NULL;
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

    /* ✨ [ARC 최적화] 실패 시 직접 해제 금지! RELEASE(self)에게 위임 ✨ */
    self->routes = new_ArrayList(32);
    if (!self->routes) {
        RELEASE(self);
        return NULL;
    }

    /* ✨ Router 생성 시 단 한 번만 PathValidator 고용 ✨ */
    self->pv = new_PathValidator();
    if (!self->pv) {
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
