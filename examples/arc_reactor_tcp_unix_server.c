/**
 * @file arc_reactor_tcp_unix_server.c
 * @brief 🇰🇷 EventLoop를 활용하여 TCP와 Unix 소켓을 동시에 처리하는 멀티플렉싱 서버 예제입니다.
 * 🇬🇧 Multiplexing server example handling TCP and Unix sockets simultaneously using EventLoop.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "libcore.h"

static volatile sig_atomic_t keep_running = 1;
void sigint_handler(int d) { (void)d; keep_running = 0; }

// 데이터 반사 (통합 인터페이스 적용)
void client_on_readable(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    char buf[8192];
    while (1) {
        // [통합]: host/port 생략 가능
        ssize_t n = self->recv(self, buf, sizeof(buf), NULL, NULL);
        if (n > 0) {
            self->send(self, buf, (size_t)n, NULL, 0);
        } else if (n == SOCKET_WOULD_BLOCK) {
            break;
        } else {
            printf("🔌 [Reactor] 연결 종료 (FD:%d)\n", self->fd);
            loop->delSocket(loop, self);
            break;
        }
    }
}

// 접속 수락 (통합 분기 적용)
void server_on_readable(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    while (1) {
        Socket* client = NULL;
        if (self->protocol == SOCKET_TCP) {
             char ip[INET_ADDRSTRLEN]; int port;
             client = (Socket*)((TcpSocket*)self)->accept((TcpSocket*)self, ip, &port);
             if (client) printf("🤝 [TCP] 신규 접속 수락 [%s:%d]\n", ip, port);
        } else if (self->protocol == SOCKET_UNIX) {
             char upath[108];
             client = (Socket*)((UnixSocket*)self)->accept((UnixSocket*)self, upath);
             if (client) printf("🤝 [UNIX] 신규 로컬 접속 수락\n");
        }
        
        if (!client) break;
        client->on_readable = client_on_readable;
        loop->addSocket(loop, client, EV_READ);
        RELEASE(client);
    }
}

int main() {
    signal(SIGINT, sigint_handler);

    EventLoop* loop = event_loop_create();
    TcpSocket* tcp_svr = new_TcpServer("0.0.0.0", 8080);
    UnixSocket* unix_svr = new_UnixServer("/tmp/arc_reactor.sock");

    if (!loop || !tcp_svr || !unix_svr) return 1;

    printf("🏰 [Reactor] 요새 가동 (TCP:8080, UNIX:/tmp/arc_reactor.sock)...\n");

    tcp_svr->base.on_readable = server_on_readable;
    unix_svr->base.on_readable = server_on_readable;

    loop->addSocket(loop, &tcp_svr->base, EV_READ);
    loop->addSocket(loop, &unix_svr->base, EV_READ);

    while (keep_running && loop->running) {
        loop->poll(loop, 1000);
    }

    event_loop_stop(loop);

    loop->delSocket(loop, &tcp_svr->base);
    loop->delSocket(loop, &unix_svr->base);

    RELEASE(tcp_svr);
    RELEASE(unix_svr);
    RELEASE(loop);

    printf("🧹 [Reactor] 자원 소각 및 자동 청소 완료.\n");
    return 0;
}
