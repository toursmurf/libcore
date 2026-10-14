/*
* File: examples/arc_hello_web.c
 * libcore 최소 웹서버 — README Quick Start 골격 (Vol.19 강의 기준)
 * 실행: ./examples/arc_hello_web  →  curl -i http://127.0.0.1:8080/
 */
#include "libcore.h"
#include <signal.h>
#include <stdio.h>

static EventLoop* g_loop = NULL;

/* SIGINT/SIGTERM → 루프 정지 플래그만 세운다 (teardown은 main에서) */
static void on_signal(int sig) {
    (void)sig;
    if (g_loop) event_loop_stop(g_loop);
}

/* "/" 와 "/:id" 요청을 처리하는 공통 핸들러 */
static void hello(HttpRequest* req, HttpResponse* res, void* ctx) {
    (void)ctx;
    char buf[256] = {0};
    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    if (id_str) {
        snprintf(buf, sizeof(buf), "Hello, libcore: %s\n", id_str);
    } else {
        snprintf(buf, sizeof(buf), "Hello, libcore\n");
    }
    res->sendText(res, buf);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);              /* 클라이언트 끊김으로 서버가 죽지 않게 */

    g_loop = event_loop_create();
    if (!g_loop) {
        fprintf(stderr, "event_loop_create failed\n");
        return 1;
    }

    Router* router = new_Router(NULL);
    router->GET(router, "/", hello);
    /* 동적 라우팅: /1, /2, /test 등의 첫 경로 세그먼트를 id로 캡처 */
    router->GET(router, "/:id", hello);

    HttpServer* server = new_HttpServer(g_loop, router);
    RELEASE(router);                       /* server가 RETAIN 했음 */

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    if (server->listen(server, 8080) != 0) {
        fprintf(stderr, "listen 8080 failed\n");
        RELEASE(server);
        event_loop_destroy(g_loop);
        return 1;
    }
    printf("http://127.0.0.1:8080/\n");
    event_loop_run(g_loop);

    RELEASE(server);
    event_loop_destroy(g_loop);
    return 0;
}