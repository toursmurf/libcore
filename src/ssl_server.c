#define _GNU_SOURCE
#include "ssl_server.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

SslSocket* new_SslServer(const char* host, int port,
                         const char* cert, const char* key) {
    if (!host || port <= 0 || port > 65535) return NULL;
    if (!cert || !key) return NULL;

    /* [A] SSL_CTX 생성 — 서버가 소유!! */
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) return NULL;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx,  key,  SSL_FILETYPE_PEM) <= 0 ||
        !SSL_CTX_check_private_key(ctx)) {
        SSL_CTX_free(ctx); return NULL;
    }

    /* [B] TCP 서버 소켓 */
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { SSL_CTX_free(ctx); return NULL; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (host) inet_pton(AF_INET, host, &addr.sin_addr);
    else addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(fd, TCP_BACKLOG) != 0) {
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

    if (host) {
        size_t hlen = strlen(host);
        if (hlen >= sizeof(self->host)) hlen = sizeof(self->host) - 1;
        memcpy(self->host, host, hlen);
        self->host[hlen] = '\0';
    }

    return self;
}

SslSocket* SslSocket_accept(SslSocket* server) {
    if (!server || !server->ctx) return NULL;
    if (server->base.fd < 0)     return NULL;

    /* [A] accept4 */
    struct sockaddr_in cli_addr = {0};
    socklen_t cli_len = sizeof(cli_addr);
    int cli_fd = accept4(server->base.fd,
                         (struct sockaddr*)&cli_addr,
                         &cli_len,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cli_fd < 0) return NULL;

    /* [B] 클라이언트 SslSocket 객체 할당 */
    SslSocket* client = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!client) {
      close(cli_fd);
      return NULL;
    }

    SslSocket_init_base(client, cli_fd);

    /* ✅ SSL_CTX_up_ref 반환값 체크!! */
    if (SSL_CTX_up_ref(server->ctx) != 1) {
        RELEASE(client);
        return NULL;
    }
    client->ctx = server->ctx;

    /* [D] SSL 세션 생성 */
    client->ssl = SSL_new(client->ctx);
    if (!client->ssl) {
      RELEASE(client);
      return NULL;
    }

    /* ✅ SSL_set_fd 반환값 체크!! */
    if (SSL_set_fd(client->ssl, cli_fd) != 1) {
        RELEASE(client);
        return NULL;
    }

    /* [F] SSL Handshake (서버 측)
     * 추후: 비동기 연동 시
     *   SSL_ERROR_WANT_READ/WRITE 별도 처리 필요!! */
    if (SSL_accept(client->ssl) <= 0) {
        RELEASE(client);
        return NULL;
    }

    return client;
}
