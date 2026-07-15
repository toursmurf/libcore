#ifndef ROUTER_H
#define ROUTER_H

#include "object.h"
#include "arraylist.h"
#include "string_obj.h"
#include "http_message.h"

/* =========================================================
 * [1] 제국 표준 HTTP 핸들러 시그니처 (함수 포인터)
 * 🚨 특징: 전역 변수 철폐! user_ctx를 통해 DB/Config 무한 주입 가능!
 * ========================================================= */
typedef void (*HttpHandler)(HttpRequest* req, HttpResponse* res, void* user_ctx);

/* =========================================================
 * [2] Route : 단일 경로 매핑 객체
 * ========================================================= */
typedef struct Route {
    Object base;

    HttpMethod method;
    String* path;          /* 정적 경로 (추후 정규식 regex 확장 가능) */
    HttpHandler handler;   /* 실행될 비즈니스 로직 함수 */
} Route;

Route* new_Route(HttpMethod method, const char* path, HttpHandler handler);

/* =========================================================
 * [3] Router : Express.js를 능가할 제국의 중앙 관제탑
 * ========================================================= */
typedef struct Router Router;
struct Router {
    Object base;

    ArrayList* routes; /* Route 객체들을 담을 리스트 */
    void* user_ctx;    /* 모든 핸들러에 전달될 전역 컨텍스트 (DB 등) */

    /* 🚀 Express.js 스타일 직관적 라우팅 VTable */
    void (*addRoute)(Router* self, HttpMethod method, const char* path, HttpHandler handler);
    void (*GET)(Router* self, const char* path, HttpHandler handler);
    void (*POST)(Router* self, const char* path, HttpHandler handler);
    void (*PUT)(Router* self, const char* path, HttpHandler handler);
    void (*DELETE)(Router* self, const char* path, HttpHandler handler);

    /* 🚀 심장부: 패킷을 받아 알맞은 핸들러로 꽂아주는 디스패처 */
    void (*dispatch)(Router* self, HttpRequest* req, HttpResponse* res);
};

/* 생성자 (user_ctx 주입) */
Router* new_Router(void* user_ctx);

/* =========================================================
 * ✨ [추가] 글로벌 에러 응답 유틸리티 ✨
 * 어느 핸들러(Controller)에서든 호출하면 512바이트 패딩이 포함된
 * 브라우저-프리패스(Bypass) 에러 화면을 즉각 발사합니다!
 * ========================================================= */
void Router_sendError(HttpResponse* res, int status, const char* error_code, const char* message);

#endif /* ROUTER_H */