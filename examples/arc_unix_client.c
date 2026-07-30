/**
 * @file arc_unix_server.c (or arc_unix_client.c)
 * @brief 🇰🇷 프로세스 간 통신(IPC)을 위한 Unix Domain Socket 사용 예제입니다.
 * 🇬🇧 Unix Domain Socket usage example for Inter-Process Communication (IPC).
 * @note  This example strictly follows the ARC memory management rules.
 */

#include "unix_socket.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    const char* sock_path = "/tmp/arc_ipc.sock";
    printf("🚀 [UNIX Client] 로컬 IPC 타격 준비...\n");

    // [1] 파일 경로를 타겟으로 조준하여 비동기 접속 시도
    UnixSocket* client = new_UnixClient(sock_path);
    if (!client) {
        printf("❌ [UNIX Client] 접속 실패. 서버 요새가 가동 중인지 확인하십시오.\n");
        return 1;
    }

    const char* msg = "인동 이사님, 빛의 속도로 IPC 타격 완료!";
    printf("🔥 [UNIX Client] 발사: %s\n", msg);

    // [🚨 통합]: send_all 대신 통합 send() 사용 (TCP와 동일한 문법!)
    client->base.send(&client->base, msg, strlen(msg), NULL, 0);

    char buf[4096];
    int retry = 0;
    while (retry < 5000) {
        // [🚨 통합]: recv() 호출 (주소 정보 불필요 시 NULL, NULL)
        ssize_t n = client->base.recv(&client->base, buf, sizeof(buf) - 1, NULL, NULL);

        if (n > 0) {
            buf[n] = '\0';
            printf("✅ [UNIX Client] 반사 확인: %s\n", buf);
            break;
        } else if (n == 0) {
            printf("🔌 [UNIX Client] 서버 요새 연결 끊김\n");
            break;
        } else if (n == SOCKET_WOULD_BLOCK) {
            usleep(1000); // 1ms 대기하며 폴링
            retry++;
        } else {
            printf("❌ [UNIX Client] 수신 에러\n");
            break;
        }
    }

    if (retry >= 5000) printf("⏱️ [UNIX Client] 타임아웃 발생!\n");

    // [W1 제국 표준]: 자원 소각
    RELEASE(client);
    return 0;
}
