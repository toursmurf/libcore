#define _GNU_SOURCE
#include "ssl_server.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>   /* 🚀 fcntl 추가 */
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
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

SslSocket* new_SslServer(const char* host, int port,
                         const char* cert, const char* key) {
    if (!host || port <= 0 || port > 65535) return NULL;
    if (!cert || !key) return NULL;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    init_legacy_ssl();
#endif

    /* [A] SSL_CTX 생성 — 서버가 소유!! (버전 분기 처리) */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX* ctx = SSL_CTX_new(SSLv23_server_method());
#else
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
#endif

    if (!ctx) return NULL;

    /* 최소 TLS 1.2 이상 강제 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
#else
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#endif

    if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx,  key,  SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(ctx)) {
        SSL_CTX_free(ctx); return NULL;
    }

    /* 🚀 [B] TCP 서버 소켓 (macOS 호환 플래그 분기) */
    int fd = -1;
#if defined(__linux__) || defined(__gnu_linux__)
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    if (fd < 0) { SSL_CTX_free(ctx); return NULL; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (host) inet_pton(AF_INET, host, &addr.sin_addr);
    else addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(fd, 1024) != 0) {
        SSL_CTX_free(ctx); close(fd); return NULL;
    }

    /* [C] SslSocket 객체 할당 */
    SslSocket* self = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!self) {
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    SslSocket_init_base(self, fd);
    self->ctx = ctx;
    self->ssl = NULL;    /* accept 시점에 생성 */

    /* ✅ snprintf를 사용하여 호스트명 안전하게 복사 */
    if (host) {
        snprintf(self->host, sizeof(self->host), "%s", host);
    }

    return self;
}

SslSocket* SslSocket_accept(SslSocket* server) {
    if (!server || !server->ctx) return NULL;
    if (server->base.fd < 0)     return NULL;

    struct sockaddr_in cli_addr = {0};
    socklen_t cli_len = sizeof(cli_addr);

    /* 🚀 [A] accept 처리 (Linux는 accept4, macOS는 accept 후 fcntl 주입) */
    int cli_fd = -1;
#if defined(__linux__) || defined(__gnu_linux__)
    cli_fd = accept4(server->base.fd,
                     (struct sockaddr*)&cli_addr,
                     &cli_len,
                     SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    cli_fd = accept(server->base.fd, (struct sockaddr*)&cli_addr, &cli_len);
    if (cli_fd >= 0) {
        int flags = fcntl(cli_fd, F_GETFL, 0);
        fcntl(cli_fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    if (cli_fd < 0) return NULL;

    /* [B] 클라이언트 SslSocket 객체 할당 */
    SslSocket* client = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!client) {
        close(cli_fd);
        return NULL;
    }

    SslSocket_init_base(client, cli_fd);

    /* ✅ OpenSSL 버전에 따른 레퍼런스 카운트 증가 처리!! */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    /* OpenSSL 1.0.x: 내부 구조체 직접 참조 (매크로 사용) */
    CRYPTO_add(&server->ctx->references, 1, CRYPTO_LOCK_SSL_CTX);
    client->ctx = server->ctx;
#else
    /* OpenSSL 1.1.x 이상: 공식 API 사용 */
    if (SSL_CTX_up_ref(server->ctx) != 1) {
        RELEASE(client);
        return NULL;
    }
    client->ctx = server->ctx;
#endif

    /* [D] SSL 세션 생성 */
    client->ssl = SSL_new(client->ctx);
    if (!client->ssl) {
        RELEASE(client);
        return NULL;
    }

    if (SSL_set_fd(client->ssl, cli_fd) != 1) {
        RELEASE(client);
        return NULL;
    }

    /* 🚨 [V1.6 Tech Debt] 비동기 TLS 핸드셰이크 지원
     * 현재 cli_fd는 SOCK_NONBLOCK 상태이므로, SSL_accept 호출 시
     * SSL_ERROR_WANT_READ/WRITE가 발생하며 즉시 실패(NULL) 처리됩니다.
     */
    if (SSL_accept(client->ssl) <= 0) {
        RELEASE(client);
        return NULL;
    }

    return client;
}