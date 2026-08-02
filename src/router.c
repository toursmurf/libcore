/*
 * router.c
 * 라우터 — libcore OOP 구현체 (v1.7.1 Router params 2-Pass 엔진 + OOM/NPD 완벽 방어)
 * 🌿 Eye-Care Mode: 1 Line = 1 Statement
 */

#include "router.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern Logger* logger;

/* =========================================================
 * ✨ [글로벌 에러 응답기] 모든 요청에서 재사용 가능한 표준 에러 출력 ✨
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
 * [2] Router VTable 구현부 (2-Pass 알고리즘)
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

/* ── ① split_segments: "/" 기준 분할 배열 생성 (OOM 방어) ── */
static int split_segments(const char* path, char* segments[], int max_segs) {
    if (!path) {
        return 0;
    }
    
    char* copy = strdup(path);
    if (!copy) {
        return 0;
    }
    
    int count = 0;
    char* saveptr = NULL;
    char* token = strtok_r(copy, "/", &saveptr);
    
    while (token) {
        if (count >= max_segs) {
            break;
        }
        
        /* 🚨 [Fail Fast 패치] strdup 실패 시 즉시 분할 중단 */
        char* dup = strdup(token);
        if (!dup) {
            break;
        }
        
        segments[count] = dup;
        count++;
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    free(copy);
    return count;
}

/* ── ② route_match: 세그먼트 비교 및 파라미터 추출 (NPD 원천 차단) ── */
static bool route_match(const char* route_path,
                        const char* req_path,
                        HashMap*    params,
                        bool        is_param_pass) {
    char* r_segs[32] = {0};
    char* q_segs[32] = {0};
    
    int r_count = split_segments(route_path, r_segs, 32);
    int q_count = split_segments(req_path, q_segs, 32);

    bool match = true;

    if (r_count != q_count) {
        match = false;
    }

    /* 1패스: 정적 라우트 검사 (완전 일치) */
    if (match && !is_param_pass) {
        for (int i = 0; i < r_count; i++) {
            /* 🚨 [NPD 패치] 포인터가 NULL이면 즉시 매칭 실패 처리 */
            if (!r_segs[i] || !q_segs[i]) {
                match = false;
                break;
            }
            if (r_segs[i][0] == ':') {
                match = false;
                break;
            }
            if (strcmp(r_segs[i], q_segs[i]) != 0) {
                match = false;
                break;
            }
        }
    }

    /* 2패스: 동적 라우트 검사 (:param 매칭) */
    if (match && is_param_pass) {
        for (int i = 0; i < r_count; i++) {
            /* 🚨 [NPD 패치] 포인터가 NULL이면 즉시 매칭 실패 처리 */
            if (!r_segs[i] || !q_segs[i]) {
                match = false;
                break;
            }
            if (r_segs[i][0] != ':') {
                if (strcmp(r_segs[i], q_segs[i]) != 0) {
                    match = false;
                    break;
                }
            }
        }
    }

    /* 2패스 매칭 성공 시 파라미터 추출 및 주입 */
    if (match && is_param_pass && params) {
        for (int i = 0; i < r_count; i++) {
            /* 🚨 [NPD 패치] 포인터 유효성 재검증 */
            if (!r_segs[i] || !q_segs[i]) {
                continue;
            }
            if (r_segs[i][0] == ':') {
                const char* key = r_segs[i] + 1;
                String* val = new_String(q_segs[i]);
                if (val) {
                    params->put(params, key, (Object*)val);
                    RELEASE((Object*)val);
                }
            }
        }
    }

    /* ARC: 임시 할당 메모리 무결점 소각 (NULL 체크 후 free) */
    for (int i = 0; i < r_count; i++) {
        if (r_segs[i]) {
            free(r_segs[i]);
        }
    }
    for (int i = 0; i < q_count; i++) {
        if (q_segs[i]) {
            free(q_segs[i]);
        }
    }

    return match;
}

/* 🚀 ③ 심장부: 2-Pass 패킷 디스패처 (PathValidator 검증 융합 완료) */
static void impl_dispatch(Router* self, HttpRequest* req, HttpResponse* res) {
    if (!self || !req || !res) return;

    const char* original_path = req->path ? req->path->c_str(req->path) : "/";
    char safe_path[MAX_PATH_LEN + 1] = {0};

    // ✨✨ [1단계: Router 소유의 단일 PathValidator로 즉각 검증] ✨✨
    if (self->pv) {
        bool is_safe = self->pv->validate(self->pv, original_path, safe_path, sizeof(safe_path));

        if (!is_safe) {
            Router_sendError(res, 400, "Bad_Request", "보안 정책에 의해 차단된 비정상적인 URL 경로입니다.");
            return;
        }
    } else {
        Router_sendError(res, 500, "Internal_Server_Error", "보안 모듈이 초기화되지 않았습니다.");
        return;
    }

    // ✨✨ [2단계: 안전이 보장된 정규화 경로(safe_path)로 2-Pass 라우팅 시작] ✨✨
    int route_count = self->routes->getSize(self->routes);
    HttpMethod req_method = req->method;

    /* 1패스: 정적 라우트 우선 매칭 (예: /board/list) */
    for (int i = 0; i < route_count; i++) {
        Route* r = (Route*)self->routes->get(self->routes, i);
        if (!r) continue;

        if (r->method == req_method || r->method == HTTP_UNKNOWN) {
            const char* route_path_str = r->path ? r->path->c_str(r->path) : "/";
            if (route_match(route_path_str, safe_path, NULL, false)) {
                if (r->handler) {
                    r->handler(req, res, self->user_ctx);
                }
                return;
            }
        }
    }

    /* 2패스: 파라미터 동적 라우트 매칭 (예: /board/:id) */
    for (int i = 0; i < route_count; i++) {
        Route* r = (Route*)self->routes->get(self->routes, i);
        if (!r) continue;

        if (r->method == req_method || r->method == HTTP_UNKNOWN) {
            const char* route_path_str = r->path ? r->path->c_str(r->path) : "/";
            if (route_match(route_path_str, safe_path, req->params, true)) {
                if (r->handler) {
                    r->handler(req, res, self->user_ctx);
                }
                return;
            }
        }
    }

    /* 3패스: 매칭 실패 시 전역 에러 응답기(404) 호출 */
    if (logger) {
        LOG_WARN(logger, "[Router] 404 Not Found: %s", safe_path);
    }
    Router_sendError(res, 404, "Route_Not_Found", "요청하신 URL 경로와 일치하는 라우트를 찾을 수 없습니다.");
}

/* =========================================================
 * [3] Router 생성자 및 소멸자
 * ========================================================= */
static void Router_finalize(Object* obj) {
    Router* self = (Router*)obj;

    if (self->routes) {
        RELEASE(self->routes);
        self->routes = NULL;
    }
    
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

    self->routes = new_ArrayList(32);
    if (!self->routes) {
        RELEASE(self);
        return NULL;
    }

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