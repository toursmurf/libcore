#define _GNU_SOURCE
#include "ssl_client.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

static int ssl_tcp_connect(const char* host, int port) {
    if (!host) return -1;
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

SslSocket* new_SslClient(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return NULL;

    /* [A] TCP 연결 */
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

    /* ✅ SSL_CTX_set_default_verify_paths 반환값 체크!! */
    if (SSL_CTX_set_default_verify_paths(self->ctx) != 1) {
        RELEASE(self); return NULL;
    }

    /* [E] SSL 세션 생성 */
    self->ssl = SSL_new(self->ctx);
    if (!self->ssl) { RELEASE(self); return NULL; }

    /* ✅ SSL_set_fd 반환값 체크!! */
    if (SSL_set_fd(self->ssl, fd) != 1) {
        RELEASE(self); return NULL;
    }

    /* ✅ SNI 반환값 체크!! */
    if (SSL_set_tlsext_host_name(self->ssl, self->host) != 1) {
        RELEASE(self); return NULL;
    }

    /* ✅ Hostname Verification 반환값 체크!! */
    if (SSL_set1_host(self->ssl, self->host) != 1) {
        RELEASE(self); return NULL;
    }

    /* [I] Handshake
     * 현재: 동기 버전
     * 추후: 비동기 연동 시
     *   SSL_ERROR_WANT_READ/WRITE 별도 처리 필요!! */
    if (SSL_connect(self->ssl) <= 0) {
        RELEASE(self); return NULL;
    }

    return self;
}
