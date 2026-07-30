/**
 * @file arc_udp_server.c (or arc_udp_client.c)
 * @brief 🇰🇷 UDP 소켓을 이용한 데이터 송수신 예제입니다.
 * 🇬🇧 Data transmission and reception example using UDP sockets.
 * @note  This example strictly follows the ARC (Automatic Reference Counting) memory management rules.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "libcore.h"

int main() {
    printf("🚀 [UDP Client] 정밀 타격 준비...\n");

    // 🚨 연결(connect) 없이 발신용 객체만 즉시 생성
    UdpSocket* client = new_UdpClient();
    if (!client) return 1;

    const char* msg = "인동 이사님, UDP 타격 완료!";
    const char* target_ip = "127.0.0.1";
    int target_port = 9000;

    printf("🔥 [UDP Client] 발사: %s\n", msg);

    // 🚨 다형성 3: 사전 연결 없이 목적지 지정 타격 (send_to)
    client->base.send(&client->base, msg, strlen(msg), target_ip, target_port);

    char buf[4096];
    char server_ip[INET_ADDRSTRLEN];
    int server_port;

    int retry = 0;
    while (retry < 5000) { // 타임아웃 5초 설정 (5000 * 1ms)
        // 🚨 응답 대기
        ssize_t n = client->base.recv(&client->base, buf, sizeof(buf) - 1, server_ip, &server_port);

        if (n > 0) {
            buf[n] = '\0';
            printf("✅ [UDP Client] 반사 확인 [%s:%d] : %s\n", server_ip, server_port, buf);
            break;
        } else if (n == SOCKET_WOULD_BLOCK) {
            usleep(1000); // 1ms 대기
            retry++;
        } else {
            printf("❌ [UDP Client] 수신 에러!\n");
            break;
        }
    }

    if (retry >= 5000) {
        // UDP의 특징: 유실될 수 있음 (Unreliable)
        printf("⏱️ [UDP Client] 타임아웃: 데이터그램 유실 가능성\n");
    }

    RELEASE(client);
    return 0;
}
