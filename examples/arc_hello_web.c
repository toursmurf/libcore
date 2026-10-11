/*
 * File: examples/arc_hello_web.c
 * libcore 최소 웹서버 — README Quick Start 골격 (Vol.19 강의 기준)
 * 실행: ./examples/arc_hello_web  →  curl -i http://127.0.0.1:8080/
 */
#include "libcore.h"
#include <signal.h>
#include <stdio.h>

static void hello(HttpRequest* req, HttpResponse* res, void* ctx) {
    (void)req; (void)ctx;
    res->sendText(res, "hello libcore\n");
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    Router* router = new_Router(NULL);
    router->GET(router, "/", hello);

    EventLoop*  loop   = event_loop_create();
    HttpServer* server = new_HttpServer(loop, router);
    RELEASE(router);                       /* server가 RETAIN 했음 */

    if (server->listen(server, 8080) != 0) {
        fprintf(stderr, "listen 8080 failed\n");
        RELEASE(server); event_loop_destroy(loop);
        return 1;
    }
    printf("http://127.0.0.1:8080/\n");
    event_loop_run(loop);

    RELEASE(server);
    event_loop_destroy(loop);
    return 0;
}
