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
#include <signal.h>

static volatile sig_atomic_t keep_running = 1;

void sigint_handler(int dummy) {
    (void)dummy;
    const char msg[] = "\n🚨 [UNIX Server] 퇴각 명령(Ctrl+C) 수신. 자원을 소각합니다...\n";
    ssize_t ret = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)ret;
    keep_running = 0;
}

int main() {
    signal(SIGINT, sigint_handler);

    const char* sock_path = "/tmp/arc_ipc.sock";
    printf("Castle [UNIX Server] 로컬 초고속 IPC 요새 가동 (Path: %s)...\n", sock_path);

    // [1] Unix 전용 서버 소켓 생성 (내부적으로 unlink 및 bind/listen 수행)
    UnixSocket* server = new_UnixServer(sock_path);
    if (!server) {
        printf("❌ [UNIX Server] 요새 가동 실패\n");
        return 1;
    }

    while (server->base.is_open && keep_running) {
        char client_path[108]; // 상대방 경로 저장용 버퍼
        // [🚨 수정]: 최신 accept 규격 반영 (인자 2개)
        UnixSocket* client = server->accept(server, client_path);

        if (client) {
            printf("🤝 [UNIX Server] 신규 접속 포착!\n");
            char buf[4096];

            while (client->base.is_open && keep_running) {
                // [🚨 통합]: recv() 사용
                ssize_t n = client->base.recv(&client->base, buf, sizeof(buf) - 1, NULL, NULL);

                if (n > 0) {
                    buf[n] = '\0';
                    printf("🎯 [UNIX Server] 수신: %s\n", buf);
                    // [🚨 통합]: send() 사용
                    client->base.send(&client->base, buf, (size_t)n, NULL, 0);
                } else if (n == 0) {
                    printf("🔌 [UNIX Server] 클라이언트 연결 종료.\n");
                    break;
                } else if (n == SOCKET_WOULD_BLOCK) {
                    usleep(100); // 엣지 트리거 방어용 짧은 휴식
                } else {
                    break;
                }
            }
            // 클라이언트 객체 즉각 소각 (FD 자동 폐쇄)
            RELEASE(client); 
            printf("🧹 [UNIX Server] 클라이언트 자원 소각 완료.\n");
        } else {
            usleep(1000); // CPU 점유율 방어
        }
    }

    // 🚨 핵심: 수동 파일 삭제 코드가 불필요합니다.
    // RELEASE가 호출되면 오버라이딩된 소멸자(Socket_finalize -> close -> unlink)가 작동합니다.
    RELEASE(server);

    printf("🧹 [UNIX Server] 요새 자원 소각 및 소켓 파일 자동 청소 완료.\n");
    return 0;
}
