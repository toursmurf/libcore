/**
 * @file arc_echo_server.c (or arc_echo_client.c)
 * @brief 🇰🇷 기본적인 TCP 에코 서버 및 클라이언트 구현 예제입니다.
 * 🇬🇧 Basic TCP echo server and client implementation example.
 * @note  This example strictly follows the ARC (Automatic Reference Counting) memory management rules.
 */

#include "tcp_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_SIZE (1024 * 1024) // 1MB 실탄

int main() {
    printf("🚀 [Client] 1MB 융단 폭격 준비...\n");
    //Non-blocking 클라이언트 생성
    TcpSocket* client = new_TcpClient("127.0.0.1", 8080);
    if (!client) return 1;

    // 1. 실탄(1MB 데이터) 제조
    char* send_buf = (char*)malloc(TEST_SIZE);
    char* recv_buf = (char*)malloc(TEST_SIZE);
    for (int i = 0; i < TEST_SIZE; i++) send_buf[i] = (char)(i % 256);

    // 2. 1MB 전송 
    // [통합]: send_all 대신 send() 사용. 내부에서 끝까지 밀어넣음!
    printf("[Client] 1MB 데이터 발사!\n");
    client->base.send(&client->base, send_buf, TEST_SIZE, NULL, 0);

    // 3. 1MB 수신 대기
    size_t total_recv = 0;
    while (total_recv < TEST_SIZE) {
        // [통합]: recv() 호출 시 주소 인자 NULL 처리
        ssize_t n = client->base.recv(&client->base, recv_buf + total_recv, TEST_SIZE - total_recv, NULL, NULL);
        
        if (n > 0) {
          total_recv += (size_t)n;
        } else if (n == 0) {
          printf("🔌 [Client] 서버에 의해 연결 종료.\n");
          break;
        } else if (n == SOCKET_WOULD_BLOCK) {
          // 비동기 모드이므로 데이터 올 때까지 잠시 대기
          usleep(100);
          continue;
        } else {
          printf("❌ [Client] 수신 에러 발생!\n");
          break;
        }
    }

    // 4. 무결성 검증
    if (total_recv == TEST_SIZE && memcmp(send_buf, recv_buf, TEST_SIZE) == 0) {
        printf("✅ [Client] 무결성 검증 성공: 1,048,576 bytes 일치!\n");
    } else {
        printf("❌ [Client] 데이터 오염 또는 누락 발생! (%zu/%d)\n", total_recv, TEST_SIZE);
    }

    free(send_buf);
    free(recv_buf);
    RELEASE(client); // ARC 소각 (FD 자동 폐쇄)
    return 0;
}
