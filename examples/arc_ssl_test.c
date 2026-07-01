/**
 * arc_ssl_test.c
 * libcore v1.4.1 — SSL Client 실전 테스트
 *
 * 테스트 목표:
 *   1. 네이버 + 구글 HTTPS 동시 연결
 *   2. HTTP GET 요청 전송
 *   3. 응답 첫 줄 확인
 *   4. ASan/Valgrind 메모리 누수 검증
 */

#include "ssl_client.h"
#include "ssl_socket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[1;33m"
#define RESET  "\033[0m"

/* ============================================================
 * HTTP GET 요청 + 응답 첫 줄 출력
 * ============================================================ */
static void ssl_http_get(const char* label,
                         const char* ip,
                         int         port,
                         const char* host) {
    printf("\n%s[TEST] %s (%s:%d)%s\n", YELLOW, label, ip, port, RESET);

    /* [1] SSL 연결 */
    SslSocket* sock = new_SslClient(ip, port);
    if (!sock) {
        printf("%s  ✗ 연결 실패%s\n", RED, RESET);
        return;
    }
    printf("%s  ✓ SSL Handshake 성공%s\n", GREEN, RESET);

    /* [2] HTTP/1.1 GET 요청 */
    char req[512];
    snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: libcore/1.4.1\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n", host);

    ssize_t sent = sock->base.send(
        (Socket*)sock, req, strlen(req), NULL, 0);
    if (sent < 0) {
        printf("%s  ✗ 요청 전송 실패%s\n", RED, RESET);
        RELEASE(sock);
        return;
    }
    printf("  ✓ GET 요청 전송: %zd bytes\n", sent);

    /* [3] 응답 수신 — 첫 청크만 */
    char buf[4096] = {0};
    ssize_t n = sock->base.recv(
        (Socket*)sock, buf, sizeof(buf) - 1, NULL, NULL);

    if (n > 0) {
        /* 첫 줄만 출력 */
        char* newline = strchr(buf, '\n');
        if (newline) *newline = '\0';
        printf("%s  ✓ 응답: %s%s\n", GREEN, buf, RESET);
    } else if (n == SOCKET_WOULD_BLOCK) {
        printf("%s  ✓ WOULD_BLOCK (비동기 정상)%s\n", GREEN, RESET);
    } else {
        printf("%s  ✗ 응답 수신 실패%s\n", RED, RESET);
    }

    /* [4] ARC 해제 — SSL_shutdown + fd close */
    RELEASE(sock);
    printf("  ✓ RELEASE 완료\n");
}

/* ============================================================
 * main
 * ============================================================ */
int main(void) {
    printf("\n");
    printf("============================================\n");
    printf("  libcore v1.4.1 SSL Client 실전 테스트\n");
    printf("  네이버 + 구글 HTTPS 동시 연결\n");
    printf("============================================\n");

    /* 네이버 */
    ssl_http_get(
        "NAVER",
        "223.130.200.104",   /* www.naver.com IP */
        443,
        "www.naver.com"
    );

    /* 구글 */
    ssl_http_get(
        "GOOGLE",
        "142.250.206.196",   /* www.google.com IP */
        443,
        "www.google.com"
    );

    /* GitHub */
    ssl_http_get(
        "GITHUB",
        "140.82.121.4",      /* github.com IP */
        443,
        "github.com"
    );

    printf("\n============================================\n");
    printf("  테스트 완료!!\n");
    printf("  Valgrind로 메모리 누수 확인 권장\n");
    printf("============================================\n\n");

    return 0;
}
