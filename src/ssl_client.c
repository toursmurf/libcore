#define _GNU_SOURCE
#include "ssl_client.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

/* ============================================================
 * [내부] getaddrinfo() 기반 TCP connect
 * IPv4/IPv6 자동 지원!!
 * ============================================================ */
static int ssl_tcp_connect(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return -1;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;    /* IPv4 / IPv6 자동!! */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        return -1;
    }

    int fd = -1;

    /* 첫 번째로 연결 가능한 주소 사용 */
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family,
                    p->ai_socktype | SOCK_CLOEXEC,
                    p->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;   /* 연결 성공!! */
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

/* ============================================================
 * new_SslClient
 * 용도: https:// ssl:// wss://
 * ============================================================ */
SslSocket* new_SslClient(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return NULL;

    /* [A] TCP 연결 — getaddrinfo 기반 */
    int fd = ssl_tcp_connect(host, port);
    if (fd < 0) return NULL;

    /* [B] 객체 할당 */
    SslSocket* self = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!self) { close(fd); return NULL; }

    SslSocket_init_base(self, fd);

    /* [C] 호스트명 저장 */
    if (host) {
        size_t hlen = strlen(host);
        if (hlen >= sizeof(self->host)) hlen = sizeof(self->host) - 1;
        memcpy(self->host, host, hlen);
        self->host[hlen] = '\0';
    }

    /* [D] SSL_CTX 생성 */
    self->ctx = SSL_CTX_new(TLS_client_method());
    if (!self->ctx) { RELEASE(self); return NULL; }

    SSL_CTX_set_min_proto_version(self->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(self->ctx, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_set_default_verify_paths(self->ctx) != 1) {
        RELEASE(self); return NULL;
    }

    /* [E] SSL 세션 생성 */
    self->ssl = SSL_new(self->ctx);
    if (!self->ssl) { RELEASE(self); return NULL; }

    /* [F] SSL ↔ fd 연결 */
    if (SSL_set_fd(self->ssl, fd) != 1) {
        RELEASE(self); return NULL;
    }

    /* [G] SNI 설정 */
    if (SSL_set_tlsext_host_name(self->ssl, self->host) != 1) {
        RELEASE(self); return NULL;
    }

    /* [H] Hostname Verification */
    if (SSL_set1_host(self->ssl, self->host) != 1) {
        RELEASE(self); return NULL;
    }

    /* [I] Handshake
     * 추후: 비동기 연동 시
     *   SSL_ERROR_WANT_READ/WRITE 별도 처리 필요!! */
    if (SSL_connect(self->ssl) <= 0) {
        RELEASE(self); return NULL;
    }

    return self;
}
