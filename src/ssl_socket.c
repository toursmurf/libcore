#define _GNU_SOURCE
#include "ssl_socket.h"
#include <unistd.h>
#include <limits.h>
#include <errno.h>

static const Class _SslSocket_Class = {
    .name     = "SslSocket",
    .size     = sizeof(SslSocket),
    .finalize = SslSocket_finalize
};

const Class* ssl_socket_class_ptr(void) {
    return &_SslSocket_Class;
}

static int SslSocket_getFD_impl(Socket* s) {
    return s ? s->fd : -1;
}

static void SslSocket_close_impl(Socket* s) {
    if (!s) return;
    SslSocket* self = (SslSocket*)s;

    if (self->ssl) {
        int ret = SSL_shutdown(self->ssl);
        if (ret == 0) SSL_shutdown(self->ssl);

        /* 🚀 SSL_free는 내부적으로 BIO를 통해 fd를 닫습니다. */
        SSL_free(self->ssl);
        self->ssl = NULL;

        /* 🚨 Double Free 폭탄 제거: fd가 이미 닫혔으므로 즉시 무효화! */
        s->fd = -1;
    }
    if (self->ctx) {
        SSL_CTX_free(self->ctx);
        self->ctx = NULL;
    }

    /* 혹시라도 SSL_free를 거치지 않은 순수 Socket의 경우에만 작동합니다. */
    if (s->fd >= 0) {
        close(s->fd);
        s->fd      = -1;
    }
    s->is_open = false;
}

static ssize_t SslSocket_send_impl(Socket* s, const void* buf, size_t len,
                                   const char* host, int port) {
    (void)host; (void)port;
    if (!s || !buf || len == 0) return -1;

    SslSocket* self = (SslSocket*)s;
    if (!self->ssl || !s->is_open) return -1;

    size_t total = 0;
    const char* p = (const char*)buf;

    while (total < len) {
        size_t remain = len - total;
        if(remain > INT_MAX) remain = INT_MAX;

        int n = SSL_write(self->ssl, p + total, (int)remain);
        if (n <= 0) {
            int err = SSL_get_error(self->ssl, n);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                return (total > 0) ? (ssize_t)total : SOCKET_WOULD_BLOCK;
            }
            if (err == SSL_ERROR_SYSCALL) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    return (total > 0) ? (ssize_t)total : SOCKET_WOULD_BLOCK;
                }
            }
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static ssize_t SslSocket_recv_impl(Socket* s, void* buf, size_t len,
                                   char* host, int* port) {
    (void)host; (void)port;
    if (!s || !buf || len == 0) return -1;

    SslSocket* self = (SslSocket*)s;
    if (!self->ssl || !s->is_open) return -1;

    int n = SSL_read(self->ssl, buf, (int)len);
    if (n <= 0) {
        int err = SSL_get_error(self->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return SOCKET_WOULD_BLOCK;
        }
        if (err == SSL_ERROR_ZERO_RETURN) {
            s->is_open = false;
        } else if (err == SSL_ERROR_SYSCALL) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return SOCKET_WOULD_BLOCK;
            }
            s->is_open = false;
        }
        return -1;
    }
    return (ssize_t)n;
}

void SslSocket_finalize(Object* obj) {
    SslSocket* self = (SslSocket*)obj;
    if (self) SslSocket_close_impl(&self->base);
}

void SslSocket_init_base(SslSocket* self, int fd) {
    if (!self) return;

    Socket_init_base(&self->base, fd, SOCKET_TCP);
    self->base.base.type = &_SslSocket_Class;

    self->base.send  = SslSocket_send_impl;
    self->base.recv  = SslSocket_recv_impl;
    self->base.close = SslSocket_close_impl;
    self->base.getFD = SslSocket_getFD_impl;

    self->base.bind    = NULL;
    self->base.listen  = NULL;
    self->base.connect = NULL;
}