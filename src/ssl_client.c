#define _GNU_SOURCE
#include "ssl_client.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/* ============================================================
 * 🚨 [OpenSSL 1.0.x 레거시 글로벌 초기화 방어막]
 * ============================================================ */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
static int g_ssl_initialized = 0;
static void init_legacy_ssl(void) {
    if (!g_ssl_initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        g_ssl_initialized = 1;
    }
}
#endif

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
 * ============================================================ */
SslSocket* new_SslClient(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return NULL;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    init_legacy_ssl();
#endif

    /* [A] TCP 연결 — 동기!! */
    int fd = ssl_tcp_connect(host, port);
    if (fd < 0) return NULL;

    /* [B] SSL_CTX 생성 (OpenSSL 버전에 따른 호환성 처리) */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX* ctx = SSL_CTX_new(SSLv23_client_method());
#else
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
#endif

    if (!ctx) {
        close(fd);
        return NULL;
    }

    /* 프로토콜 최소 버전 강제 (TLS 1.2 이상) */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
#else
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#endif

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [C] SSL 세션 생성 */
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [D] SSL ↔ fd 연결 */
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [E] SNI 설정 */
    if (SSL_set_tlsext_host_name(ssl, host) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [F] Hostname Verification */
    if (SSL_set1_host(ssl, host) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [G] SSL Handshake — fd가 동기 상태에서 수행!! */
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [H] ✅ Handshake 완료 후 NONBLOCK 전환!! */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* [I] SslSocket 객체 할당 */
    SslSocket* self = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!self) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* [J] Socket_init_base */
    SslSocket_init_base(self, fd);

    self->ctx = ctx;
    self->ssl = ssl;

    /* ✅ snprintf를 사용하여 호스트명 안전하게 복사 */
    if (host) {
        snprintf(self->host, sizeof(self->host), "%s", host);
    }

    return self;
}