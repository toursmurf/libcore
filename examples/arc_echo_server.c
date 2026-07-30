/**
 * @file arc_echo_server.c (or arc_echo_client.c)
 * @brief 🇰🇷 기본적인 TCP 에코 서버 및 클라이언트 구현 예제입니다.
 * 🇬🇧 Basic TCP echo server and client implementation example.
 * @note  This example strictly follows the ARC (Automatic Reference Counting) memory management rules.
 */

#include "tcp_socket.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// 🚨 시그널 방어막 장착
static volatile sig_atomic_t keep_running = 1;

void sigint_handler(int dummy) {
    (void)dummy;
    const char msg[] = "\n🚨 [TCP Server] 퇴각 명령(Ctrl+C) 수신. 자원을 소각합니다...\n";
    ssize_t ret = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)ret;
    keep_running = 0;
}

int main() {
    signal(SIGINT, sigint_handler);

    printf("Castle [Server] Echo 요새 가동 (Port: 8080)...\n");
    TcpSocket* server = new_TcpServer("0.0.0.0", 8080);
    if (!server) return 1;

    // 🚨 무한 루프 대신 keep_running 확인
    while (keep_running) {
        char client_ip[INET_ADDRSTRLEN];
        int client_port;

        TcpSocket* client = server->accept(server, client_ip, &client_port);
        if (!client) {
            usleep(1000);
            continue;
        }

        printf("🎯 [Server] 타격 지점 확인: %s:%d\n", client_ip, client_port);

        char buf[8192];
        // 🚨 내부 통신 루프도 keep_running 확인
        while (client->base.is_open && keep_running) {
            ssize_t n = client->base.recv(&client->base, buf, sizeof(buf), NULL, NULL);

            if (n > 0) {
                client->base.send(&client->base, buf, (size_t)n, NULL, 0);
            } else if (n == 0) {
                printf("🔌 [Server] 타겟 연결 종료 감지.\n");
                break;
            } else if (n == SOCKET_WOULD_BLOCK) {
                usleep(10); 
                continue; 
            } else {
                break; 
            }
        }

        RELEASE(client);
        printf("🧹 [Server] 클라이언트 자원 소각 완료.\n");
    }

    // 🚨 이제 ^C를 눌러도 여기까지 안전하게 도달합니다!!
    RELEASE(server);
    printf("🧹 [Server] 요새 자원 소각 및 상황 종료 완료.\n");
    return 0;
}
