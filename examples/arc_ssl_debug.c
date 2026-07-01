/**
 * arc_ssl_debug.c — SSL 연결 실패 원인 디버깅
 */
#include "ssl_client.h"
#include "ssl_socket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <unistd.h>

static void ssl_debug(const char* host, int port) {
    printf("\n[DEBUG] %s:%d\n", host, port);

    /* [1] getaddrinfo */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = NULL;
    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0) {
        printf("  ✗ getaddrinfo 실패: %s\n", gai_strerror(gai));
        return;
    }
    /* IP 출력 */
    char ip[64] = {0};
    if (res->ai_family == AF_INET)
        inet_ntop(AF_INET,
          &((struct sockaddr_in*)res->ai_addr)->sin_addr,
          ip, sizeof(ip));
    printf("  ✓ DNS 해석: %s → %s\n", host, ip);

    /* [2] TCP connect */
    int fd = socket(res->ai_family,
                    res->ai_socktype | SOCK_CLOEXEC,
                    res->ai_protocol);
    if (fd < 0) { printf("  ✗ socket() 실패\n"); freeaddrinfo(res); return; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        perror("  ✗ connect() 실패");
        close(fd); freeaddrinfo(res); return;
    }
    freeaddrinfo(res);
    printf("  ✓ TCP connect 성공 fd=%d\n", fd);

    /* [3] SSL_CTX */
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    int vp = SSL_CTX_set_default_verify_paths(ctx);
    printf("  %s SSL_CTX_set_default_verify_paths: %d\n",
           vp == 1 ? "✓" : "✗", vp);

    /* [4] SSL */
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);
    SSL_set1_host(ssl, host);

    /* [5] Handshake */
    int ret = SSL_connect(ssl);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        printf("  ✗ SSL_connect 실패 err=%d\n", err);
        /* OpenSSL 에러 스택 출력 */
        unsigned long e;
        while ((e = ERR_get_error()) != 0) {
            char buf[256];
            ERR_error_string_n(e, buf, sizeof(buf));
            printf("    OpenSSL: %s\n", buf);
        }
    } else {
        printf("  ✓ SSL Handshake 성공!!\n");
        printf("  ✓ 프로토콜: %s\n", SSL_get_version(ssl));
        printf("  ✓ 암호: %s\n", SSL_get_cipher(ssl));
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);
}

int main(void) {
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                     OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    printf("============================================\n");
    printf("  SSL 연결 디버그\n");
    printf("============================================\n");

    ssl_debug("www.naver.com",  443);
    ssl_debug("www.google.com", 443);

    return 0;
}
