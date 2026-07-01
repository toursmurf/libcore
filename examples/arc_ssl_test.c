/**
 * arc_ssl_test.c
 * libcore v1.4.1 — SSL Client 실전 테스트
 * getaddrinfo() 기반 — 도메인 직접 입력!!
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

static void ssl_http_get(const char* label,
                         const char* host,
                         int         port) {
    printf("\n%s[TEST] %s (https://%s:%d)%s\n",
           YELLOW, label, host, port, RESET);

    /* [1] SSL 연결 — 도메인으로!! */
    SslSocket* sock = new_SslClient(host, port);
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

    /* [3] 응답 수신 */
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

    /* [4] ARC 해제 */
    RELEASE(sock);
    printf("  ✓ RELEASE 완료\n");
}

int main(void) {
    printf("\n");
    printf("============================================\n");
    printf("  libcore v1.4.1 SSL Client 실전 테스트\n");
    printf("  getaddrinfo() 기반 도메인 연결!!\n");
    printf("============================================\n");

    ssl_http_get("NAVER",  "www.naver.com",  443);
    ssl_http_get("GOOGLE", "www.google.com", 443);
    ssl_http_get("GITHUB", "github.com",     443);

    printf("\n============================================\n");
    printf("  테스트 완료!!\n");
    printf("============================================\n\n");

    return 0;
}
