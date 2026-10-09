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

    /*
     * [Ownership Contract]
     * SslSocket owns fd.
     * SSL/BIO does not own the underlying fd. (Borrower)
     *
     * Partial-init 상태(ssl==NULL, ctx==NULL, fd==-1)에서도
     * 언제나 안전하게 순차적으로 정리되도록 보장합니다.
     */

    if (self->ssl) {
        /* [BACKLOG] finalize에서 SSL_shutdown 수행 여부 / 정상 close 분리 여부 추후 검토 */
        int ret = SSL_shutdown(self->ssl);
        if (ret == 0) SSL_shutdown(self->ssl);

        SSL_free(self->ssl);
        self->ssl = NULL;
        /* 삭제됨: s->fd = -1; (SSL_free는 fd를 닫지 않으므로 여기서 소유권을 버리면 leak 발생!) */
    }

    if (self->ctx) {
        SSL_CTX_free(self->ctx);
        self->ctx = NULL;
    }

    /* SslSocket이 fd의 유일한 owner이므로 여기서 최종적으로 닫아줌 */
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
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

    /* [BACKLOG] len > INT_MAX일 때 직접 cast 위험 -> 이번 커밋 범위 제외 */
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