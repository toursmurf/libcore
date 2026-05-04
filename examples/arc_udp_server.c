/**
 * @file arc_udp_server.c (or arc_udp_client.c)
 * @brief 🇰🇷 UDP 소켓을 이용한 데이터 송수신 예제입니다.
 * 🇬🇧 Data transmission and reception example using UDP sockets.
 * @note  This example strictly follows the ARC (Automatic Reference Counting) memory management rules.
 */

#include "libcore.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// 🚨 1. C 표준에 맞춘 가장 안전한 시그널 플래그 타입
static volatile sig_atomic_t keep_running = 1;

// 🚨 2. 시그널 핸들러 (경고 완벽 박멸 및 비동기 안전 버전)
void sigint_handler(int dummy) {
    (void)dummy;
    const char msg[] = "\n🚨 [UDP Server] 퇴각 명령(Ctrl+C) 수신. 안전하게 자원을 소각합니다...\n";

    // write의 반환값을 명시적으로 받아주고, 컴파일러가 안심하도록 (void) 캐스팅
    ssize_t ret = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)ret;

    keep_running = 0; // 퇴근 깃발을 올림
}

int main() {
    signal(SIGINT, sigint_handler);

    printf("🛸 [UDP Server] 데이터그램 요새 가동 (Port: 8081)...\n");

    // 🚨 [팩토리]: UDP 서버 생성 (내부에서 SOCKET_UDP 타입 각인 완료)
    UdpSocket* server = new_UdpServer("0.0.0.0", 9000);
    if (!server) return 1;

    char buf[4096];
    char client_ip[INET_ADDRSTRLEN];
    int client_port;

    while (server->base.is_open && keep_running) {
        // 🚨 [🚨 통합]: recv_from -> 통합 recv() 사용
        // UDP 타입이므로 내부에서 recvfrom()을 호출하여 발신자 정보를 채워줍니다.
        ssize_t n = server->base.recv(&server->base, buf, sizeof(buf) - 1, client_ip, &client_port);

        if (n > 0) {
            buf[n] = '\0';
            printf("🎯 [UDP Server] 포착! [%s:%d] : %s\n", client_ip, client_port, buf);
            
            // 🚨 [🚨 통합]: send_to -> 통합 send() 사용
            // 수신된 상대방 정보를 인자로 넘기면 즉시 반격(Echo)합니다.
            server->base.send(&server->base, buf, (size_t)n, client_ip, client_port);
        } 
        else if (n == SOCKET_WOULD_BLOCK) {
            // 비동기 모드이므로 데이터가 없으면 잠시 휴식 (폴링 전술)
            usleep(1000);
        } 
        else if (n < 0) {
            printf("❌ [UDP Server] 수신 에러 발생\n");
            break;
        }
    }

    // [W1 제국 표준]: 자원 소각 및 소켓 폐쇄
    RELEASE(server);
    
    printf("🧹 [UDP Server] 요새 자원 소각 완료 및 상황 종료.\n");
    return 0;
}
