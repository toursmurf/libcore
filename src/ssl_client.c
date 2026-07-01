#define _GNU_SOURCE
#include "ssl_client.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

/* ============================================================
 * [내부] getaddrinfo() 기반 TCP connect (동기!!)
 * SSL Handshake 완료 후 NONBLOCK 전환!!
 * ============================================================ */
static int ssl_tcp_connect(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return -1;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;    /* IPv4/IPv6 자동 */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return -1;

    int fd = -1;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        /* ✅ NONBLOCK 없이 생성 — SSL Handshake는 동기로!! */
        fd = socket(p->ai_family,
                    p->ai_socktype | SOCK_CLOEXEC,
                    p->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

/* ============================================================
 * new_SslClient
 * 용도: https:// ssl:// wss://
 *
 * 순서:
 *   [A] TCP connect (동기)
 *   [B~I] SSL Handshake (동기)
 *   [J] Handshake 완료 후 NONBLOCK 전환!!
 * ============================================================ */
SslSocket* new_SslClient(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return NULL;

    /* [A] TCP 연결 — 동기!! */
    int fd = ssl_tcp_connect(host, port);
    if (fd < 0) return NULL;

    /* [B] SSL_CTX 생성 */
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); return NULL; }

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [C] SSL 세션 생성 */
    SSL* ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close(fd); return NULL; }

    /* [D] SSL ↔ fd 연결 */
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [E] SNI 설정 */
    if (SSL_set_tlsext_host_name(ssl, host) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [F] Hostname Verification */
    if (SSL_set1_host(ssl, host) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [G] SSL Handshake — fd가 동기 상태에서 수행!! */
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [H] ✅ Handshake 완료 후 NONBLOCK 전환!!
     *     EventLoop 연동 준비 */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* [I] SslSocket 객체 할당 */
    SslSocket* self = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!self) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [J] Socket_init_base — NONBLOCK 중복 설정되지만 무해함 */
    SslSocket_init_base(self, fd);

    self->ctx = ctx;
    self->ssl = ssl;

    if (host) {
        size_t hlen = strlen(host);
        if (hlen >= sizeof(self->host)) hlen = sizeof(self->host) - 1;
        memcpy(self->host, host, hlen);
        self->host[hlen] = '\0';
    }

    return self;
}
