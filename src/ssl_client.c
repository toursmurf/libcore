#define _GNU_SOURCE
#include "ssl_client.h"
#include <stdio.h>       /* snprintf 직접 사용 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

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

static int ssl_set_peer_name(SSL* ssl, const char* host) {
    unsigned char buf[sizeof(struct in6_addr)];
    int is_ip = inet_pton(AF_INET, host, buf) == 1 ||
                inet_pton(AF_INET6, host, buf) == 1;

#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 4
    return is_ip ? SSL_set1_ipaddr(ssl, host)
                 : SSL_set1_dnsname(ssl, host);
#else
    (void)is_ip;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    /* [A-1 Backlog] DNS/IP 검증 계약 정리는 VERIFY_PEER 작업 시 별도 진행 */
    return SSL_set1_host(ssl, host);
#else
    (void)ssl; (void)host;
    return 1;
#endif
#endif
}

static int ssl_tcp_connect(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return -1;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return -1;

    int fd = -1;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
#if defined(__linux__) || defined(__gnu_linux__)
        fd = socket(p->ai_family,
                    p->ai_socktype | SOCK_CLOEXEC,
                    p->ai_protocol);
#else
        fd = socket(p->ai_family,
                    p->ai_socktype,
                    p->ai_protocol);
#endif
        if (fd < 0) continue;

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

SslSocket* new_SslClient(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return NULL;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    init_legacy_ssl();
#endif

    int fd = ssl_tcp_connect(host, port);
    if (fd < 0) return NULL;

    /*
     * TCP connect 성공 후 SslSocket 객체 조기 생성
     * 객체 생성 전 calloc 실패 시에만 수동 close(fd) (ARC 등록 전)
     */
    SslSocket* self = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!self) {
        close(fd);
        return NULL;
    }

    /*
     * 이 시점부터 fd ownership은 SslSocket(self)가 가짐
     * 이후 모든 실패 경로는 RELEASE(self); return NULL; 로 통일
     */
    SslSocket_init_base(self, fd);

    /* [Patch V4] Silent Truncation 원천 차단 (Atomic Reject) */
    if (host) {
        int n = snprintf(self->host, sizeof(self->host), "%s", host);
        if (n < 0 || (size_t)n >= sizeof(self->host)) {
            RELEASE(self);
            return NULL;
        }
    }

    /* self->ctx 바로 멤버에 저장 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    self->ctx = SSL_CTX_new(SSLv23_client_method());
#else
    self->ctx = SSL_CTX_new(TLS_client_method());
#endif

    if (!self->ctx) {
        RELEASE(self);
        return NULL;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX_set_options(self->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
#else
    SSL_CTX_set_min_proto_version(self->ctx, TLS1_2_VERSION);
#endif

    /* VERIFY_PEER는 이번 범위에서 제외 */
    SSL_CTX_set_verify(self->ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_default_verify_paths(self->ctx);

    /* self->ssl 바로 멤버에 저장 */
    self->ssl = SSL_new(self->ctx);
    if (!self->ssl) {
        RELEASE(self);
        return NULL;
    }

    /*
     * A-1 계약: SslSocket owns fd / SSL/BIO borrows fd
     * SSL_set_fd 실패 시 RELEASE
     */
    if (SSL_set_fd(self->ssl, fd) != 1) {
        RELEASE(self);
        return NULL;
    }

    /* SNI 실패 시 RELEASE */
    if (SSL_set_tlsext_host_name(self->ssl, host) != 1) {
        RELEASE(self);
        return NULL;
    }

    /* peer_name 반환값 검사: 1이 아니면 RELEASE */
    if (ssl_set_peer_name(self->ssl, host) != 1) {
        RELEASE(self);
        return NULL;
    }

    /* Socket_init_base가 O_NONBLOCK을 켬 → 핸드셰이크 동안만 블로킹 */
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl != -1) fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }
    /* SSL_connect 실패 시 RELEASE */
    if (SSL_connect(self->ssl) <= 0) {
        RELEASE(self);
        return NULL;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return self;
}