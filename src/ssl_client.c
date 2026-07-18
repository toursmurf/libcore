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

static int ssl_tcp_connect(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return -1;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

SslSocket* new_SslClient_from_fd(const char* host, int fd) {
    if (!host || fd < 0) return NULL;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    init_legacy_ssl();
#endif

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1 && (flags & O_NONBLOCK)) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX* ctx = SSL_CTX_new(SSLv23_client_method());
#else
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
#endif

    if (!ctx) { close(fd); return NULL; }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
#else
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#endif

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) { SSL_CTX_free(ctx); close(fd); return NULL; }

    SSL* ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close(fd); return NULL; }

    if (SSL_set_fd(ssl, fd) != 1) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }
    if (SSL_set_tlsext_host_name(ssl, host) != 1) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }
    if (SSL_set1_host(ssl, host) != 1) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }

    if (SSL_connect(ssl) <= 0) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    SslSocket* self = (SslSocket*)calloc(1, sizeof(SslSocket));
    if (!self) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return NULL; }

    SslSocket_init_base(self, fd);
    self->ctx = ctx;
    self->ssl = ssl;
    if (host) snprintf(self->host, sizeof(self->host), "%s", host);

    return self;
}

SslSocket* new_SslClient(const char* host, int port) {
    if (!host || port <= 0 || port > 65535) return NULL;
    int fd = ssl_tcp_connect(host, port);
    if (fd < 0) return NULL;
    return new_SslClient_from_fd(host, fd);
}