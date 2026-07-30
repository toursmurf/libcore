/**
 * @file arc_reactor_multi_server.c
 * @brief 🇰🇷 다양한 통신 프로토콜을 단일 스레드(EventLoop)에서 논블로킹으로 처리하는 고성능 원자로(Reactor) 패턴 서버 데모입니다.
 * 🇬🇧 High-performance Reactor pattern server demo handling various communication protocols non-blocking on a single thread (EventLoop).
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

// ---------------------------------------------------------
// [방아쇠 1] 데이터 에코 (TCP/UNIX/UDP 공통 사용 가능!)
// ---------------------------------------------------------
void client_on_readable(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    char buf[8192];
    
    while (1) {
        // [통합 인터페이스 적용]: TCP/UNIX는 주소 인자 무시 (NULL, NULL)
        ssize_t n = self->recv(self, buf, sizeof(buf), NULL, NULL);
        
        if (n > 0) {
            // [통합]: send_all 루프는 이제 베이스의 send()가 내부에서 처리함!
            self->send(self, buf, (size_t)n, NULL, 0);
        } else if (n == SOCKET_WOULD_BLOCK) {
            break;
        } else {
            printf("🔌 [Reactor] 스트림 연결 종료 (FD:%d)\n", self->fd);
            loop->delSocket(loop, self); 
            break;
        }
    }
}

// ---------------------------------------------------------
// 🚨 [방아쇠 2] UDP 서버용 데이터 처리 (통합 인터페이스 버전)
// ---------------------------------------------------------
void udp_server_on_readable(Socket* self, void* loop_ptr) {
    (void)loop_ptr;
    char buf[8192];
    char ip[INET_ADDRSTRLEN];
    int port;

    while (1) {
        // [통합]: recvfrom 대신 recv()를 쓰되 주소 버퍼를 넘김
        ssize_t n = self->recv(self, buf, sizeof(buf), ip, &port);
        
        if (n > 0) {
            printf("🎯 [UDP] 포착! [%s:%d] 에서 %zd 바이트 수신 -> 즉시 반격(Echo)\n", ip, port, n);
            // [통합]: sendto 대신 send()를 쓰고 목적지 주소 주입
            self->send(self, buf, (size_t)n, ip, port);
        } else {
            break;
        }
    }
}

// ---------------------------------------------------------
// [방아쇠 3] 서버용 접속 수락 (TCP/UNIX 공통)
// ---------------------------------------------------------
void server_on_readable(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    while (1) {
        Socket* client = NULL;
        if (self->protocol == SOCKET_TCP) {
             char ip[INET_ADDRSTRLEN]; int port;
             client = (Socket*)((TcpSocket*)self)->accept((TcpSocket*)self, ip, &port);
             if (client) printf("🤝 [TCP] 신규 접속 [%s:%d]\n", ip, port);
        } else if (self->protocol == SOCKET_UNIX) {
             char upath[108]; // [수정] Unix용 경로 버퍼 추가
             client = (Socket*)((UnixSocket*)self)->accept((UnixSocket*)self, upath);
             if (client) printf("🤝 [UNIX] 신규 로컬 접속 수락\n");
        }
        
        if (!client) break;
        
        client->on_readable = client_on_readable;
        loop->addSocket(loop, client, EV_READ);
        RELEASE(client); // 관제탑(Loop)이 소유권을 가졌으므로 팩토리 소유권 해제
    }
}

int main() {
    signal(SIGINT, sigint_handler);

    EventLoop* loop = event_loop_create();
    TcpSocket* tcp_svr = new_TcpServer("0.0.0.0", 8080);
    UnixSocket* unix_svr = new_UnixServer("/tmp/arc_ipc.sock");
    UdpSocket* udp_svr = new_UdpServer("0.0.0.0", 9000);

    if (!loop || !tcp_svr || !unix_svr || !udp_svr) return 1;

    printf("🏰 [Reactor] 트라이-프로토콜 요새 가동!\n");
    printf("   - TCP : 8080\n   - UDP : 9000\n   - UNIX: /tmp/arc_ipc.sock\n");

    tcp_svr->base.on_readable = server_on_readable;
    unix_svr->base.on_readable = server_on_readable;
    udp_svr->base.on_readable  = udp_server_on_readable;

    loop->addSocket(loop, &tcp_svr->base, EV_READ);
    loop->addSocket(loop, &unix_svr->base, EV_READ);
    loop->addSocket(loop, &udp_svr->base, EV_READ);

    while (keep_running && loop->running) {
        loop->poll(loop, 1000);
    }

    event_loop_stop(loop);

    // 자원 정리
    loop->delSocket(loop, &tcp_svr->base);
    loop->delSocket(loop, &unix_svr->base);
    loop->delSocket(loop, &udp_svr->base);

    RELEASE(tcp_svr);
    RELEASE(unix_svr);
    RELEASE(udp_svr);
    RELEASE(loop);

    printf("🧹 [Reactor] 모든 프로토콜 자원 소각 완료.\n");
    return 0;
}
