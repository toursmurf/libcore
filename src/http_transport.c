#define _GNU_SOURCE
#include "http_transport.h"
#include "ssl_client.h"
#include "tcp_socket.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#ifdef HAS_LIBURING
static int _uring_connect(HttpTransport* transport, int fd, const struct sockaddr* addr, socklen_t addrlen) {
    if (!transport->has_own_ring) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&transport->own_ring);
    if (!sqe) return -1;

    io_uring_prep_connect(sqe, fd, addr, addrlen);
    io_uring_submit(&transport->own_ring);

    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(&transport->own_ring, &cqe);
    if (ret < 0) return ret;

    int res = cqe->res;
    io_uring_cqe_seen(&transport->own_ring, cqe);

    /* 🚀 [v1.6.6 패치] 불필요한 예외 조건(-EINPROGRESS) 제거 */
    if (res < 0) return -1;

    return 0;
}
#endif

HttpTransport* HttpTransport_connect(const char* url, void* ignored_ring, int timeout_ms) {
    /* 🚀 [v1.6.6 패치] 사용하지 않는 매개변수 경고 방어 */
    (void)ignored_ring;
    (void)timeout_ms;

    if (!url) return NULL;

    HttpTransport* self = (HttpTransport*)calloc(1, sizeof(HttpTransport));
    if (!self) return NULL;

    HashMap* info = parse_url(url);
    if (!info) { free(self); return NULL; }

    const char* s_scheme = hashmap_get_str(info, "scheme");
    const char* s_host   = hashmap_get_str(info, "host");
    const char* s_port   = hashmap_get_str(info, "port");
    const char* s_path   = hashmap_get_str(info, "path");

    if (s_scheme) strncpy(self->scheme, s_scheme, sizeof(self->scheme) - 1);
    if (s_host)   strncpy(self->host, s_host, sizeof(self->host) - 1);
    if (s_path)   strncpy(self->path, s_path, sizeof(self->path) - 1);
    else          strcpy(self->path, "/");

    self->port = s_port ? atoi(s_port) : (strcmp(self->scheme, "https") == 0 ? 443 : 80);
    RELEASE((Object*)info);

#ifdef HAS_LIBURING
    if (io_uring_queue_init(32, &self->own_ring, 0) == 0) {
        self->has_own_ring = true;
        self->use_uring = true;
    }
#endif

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", self->port);

    struct addrinfo* res = NULL;
    if (getaddrinfo(self->host, port_str, &hints, &res) != 0 || !res) {
        HttpTransport_close(self);
        return NULL;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
        if (fd < 0) continue;

        bool connected = false;

#ifdef HAS_LIBURING
        if (self->use_uring && self->has_own_ring) {
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            if (_uring_connect(self, fd, p->ai_addr, p->ai_addrlen) == 0) {
                int fl = fcntl(fd, F_GETFL, 0);
                if (fl != -1) fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
                connected = true;
            }
        } else
#endif
        {
            if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
                connected = true;
            }
        }

        if (connected) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        HttpTransport_close(self);
        return NULL;
    }

    if (strcmp(self->scheme, "https") == 0) {
#ifdef HAS_LIBURING
        if (self->use_uring && self->has_own_ring) {
            self->sock = (Socket*)new_SslClient_from_fd(self->host, fd);
            if (self->sock && self->sock->fd >= 0) {
                int fl = fcntl(self->sock->fd, F_GETFL, 0);
                if (fl != -1) fcntl(self->sock->fd, F_SETFL, fl & ~O_NONBLOCK);
            }
            self->use_uring = false;
        } else
#endif
        {
            /* 🚀 [v1.6.6 패치] 이중 TCP 생성 제거 - new_SslClient_from_fd 통일 */
            self->sock = (Socket*)new_SslClient_from_fd(self->host, fd);
            if (self->sock && self->sock->fd >= 0) {
                int fl = fcntl(self->sock->fd, F_GETFL, 0);
                if (fl != -1) fcntl(self->sock->fd, F_SETFL, fl & ~O_NONBLOCK);
            }
        }
    } else {
        self->sock = (Socket*)new_TcpSocket_from_fd(fd);
        if (self->sock && self->sock->fd >= 0) {
            int fl = fcntl(self->sock->fd, F_GETFL, 0);
            if (fl != -1) fcntl(self->sock->fd, F_SETFL, fl & ~O_NONBLOCK);
        }
    }

    if (!self->sock) {
        HttpTransport_close(self);
        return NULL;
    }

    return self;
}

ssize_t HttpTransport_send(HttpTransport* self, const void* buf, size_t len) {
    if (!self || !self->sock) return -1;
    return self->sock->send(self->sock, buf, len, NULL, 0);
}

ssize_t HttpTransport_recv(HttpTransport* self, void* buf, size_t len) {
    if (!self || !self->sock) return -1;
    return self->sock->recv(self->sock, buf, len, NULL, NULL);
}

int HttpTransport_getc(HttpTransport* self) {
    if (!self) return -1;

    if (self->read_pos >= self->read_end) {
        self->read_pos = 0;
        ssize_t n = HttpTransport_recv(self, self->read_buf, sizeof(self->read_buf));
        if (n <= 0) {
            self->read_end = 0;
            return -1;
        }
        self->read_end = (int)n;
    }

    return (unsigned char)self->read_buf[self->read_pos++];
}

/* 🚀 [결함 1 복구] HTTP 헤더 파서 전용 안전한 개행 처리기 */
int HttpTransport_recv_line(HttpTransport* self, char* line_buf, size_t max_len) {
    if (!self || !line_buf || max_len == 0) return -1;

    size_t i = 0;
    while (i < max_len - 1) {
        int c = HttpTransport_getc(self);
        if (c < 0) break;
        if (c == '\n') break;       /* \n에서 끊기 (포함 안 함) */
        if (c != '\r') line_buf[i++] = (char)c;  /* \r 무시 */
    }
    line_buf[i] = '\0';
    return (int)i;   /* 블랭크 라인 → 0 반환 → while 탈출 */
}

ssize_t HttpTransport_read(HttpTransport* self, void* buf, size_t len) {
    if (!self || !buf || len == 0) return -1;

    size_t total_read = 0;
    char* dest = (char*)buf;

    if (self->read_pos < self->read_end) {
        size_t avail = (size_t)(self->read_end - self->read_pos);
        size_t chunk = (avail < len) ? avail : len;

        memcpy(dest, self->read_buf + self->read_pos, chunk);
        self->read_pos += (int)chunk;
        total_read += chunk;

        if (total_read == len) return (ssize_t)total_read;
    }

    while (total_read < len) {
        ssize_t n = HttpTransport_recv(self, dest + total_read, len - total_read);
        if (n <= 0) {
            if (n == SOCKET_WOULD_BLOCK) {
                if (total_read > 0) return (ssize_t)total_read;
                return SOCKET_WOULD_BLOCK;
            }
            return (total_read > 0) ? (ssize_t)total_read : -1;
        }
        total_read += (size_t)n;
    }

    return (ssize_t)total_read;
}

void HttpTransport_close(HttpTransport* self) {
    if (!self) return;
    if (self->sock) {
        RELEASE((Object*)self->sock);
    }
#ifdef HAS_LIBURING
    if (self->has_own_ring) {
        io_uring_queue_exit(&self->own_ring);
    }
#endif
    free(self);
}