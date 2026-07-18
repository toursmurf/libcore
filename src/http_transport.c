#include "http_transport.h"
#include "socket_base.h"
#include "ssl_client.h"
#include "tcp_socket.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h> 

#ifdef HAS_LIBURING
#include <liburing.h>

static int _do_async_connect(HttpTransport* transport, int fd, struct sockaddr* addr, socklen_t addrlen) {
    struct io_uring* ring = (struct io_uring*)transport->ring;
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    if (!sqe) return -1;

    io_uring_prep_connect(sqe, fd, addr, addrlen);
    io_uring_submit(ring);

    struct io_uring_cqe* cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) return ret;

    int res = cqe->res; 
    io_uring_cqe_seen(ring, cqe);
    return res; 
}

static ssize_t _do_async_send(HttpTransport* transport, int fd, const void* buf, size_t len) {
    struct io_uring* ring = (struct io_uring*)transport->ring;
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    if (!sqe) return -1;

    io_uring_prep_send(sqe, fd, buf, len, 0);
    io_uring_submit(ring);

    struct io_uring_cqe* cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) return ret;

    ssize_t res = cqe->res;
    io_uring_cqe_seen(ring, cqe);
    return res;
}

static ssize_t _do_async_recv(HttpTransport* transport, int fd, void* buf, size_t len) {
    struct io_uring* ring = (struct io_uring*)transport->ring;
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    if (!sqe) return -1;

    io_uring_prep_recv(sqe, fd, buf, len, 0);
    io_uring_submit(ring);

    struct io_uring_cqe* cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) return ret;

    ssize_t res = cqe->res;
    io_uring_cqe_seen(ring, cqe);
    return res;
}
#endif

HttpTransport* HttpTransport_connect(const char* url, void* ring, int timeout_ms) {
    (void)timeout_ms;
    HttpTransport* transport = calloc(1, sizeof(HttpTransport));
    if (!transport) return NULL;

    HashMap* url_info = parse_url(url);
    if (!url_info) { free(transport); return NULL; }
    
    transport->host = safe_strdup(hashmap_get_str(url_info, "host"), 256);
    transport->path = safe_strdup(hashmap_get_str(url_info, "path") ? hashmap_get_str(url_info, "path") : "/", 2048);
    
    const char* scheme_val = hashmap_get_str(url_info, "scheme");
    if (scheme_val) {
        strncpy(transport->scheme, scheme_val, sizeof(transport->scheme) - 1);
    }
    
    int port = atoi(hashmap_get_str(url_info, "port"));
    if (port == 0) port = (strcmp(transport->scheme, "https") == 0) ? 443 : 80;
    transport->port = port;
    
    bool is_ssl = (strcmp(transport->scheme, "https") == 0 || strcmp(transport->scheme, "wss") == 0 || strcmp(transport->scheme, "ssl") == 0);
    RELEASE((Object*)url_info);

    if (is_ssl) {
        transport->use_uring = false;
        transport->sock = (Socket*)new_SslClient(transport->host, port);
        if (!transport->sock) {
            HttpTransport_close(transport);
            return NULL;
        }
    } else {
        struct addrinfo hints = {0}, *res_ai;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(transport->host, port_str, &hints, &res_ai) != 0) {
            HttpTransport_close(transport);
            return NULL;
        }

        int fd = socket(res_ai->ai_family, res_ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, res_ai->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(res_ai);
            HttpTransport_close(transport);
            return NULL;
        }

        transport->sock = (Socket*)new_TcpSocket_from_fd(fd);
        
        if (!transport->sock) {
            close(fd);  
            freeaddrinfo(res_ai);
            HttpTransport_close(transport);
            return NULL;
        }

#ifdef HAS_LIBURING
        if (ring != NULL) {
            transport->ring = ring;
            transport->use_uring = true;
            
            int res = _do_async_connect(transport, fd, res_ai->ai_addr, res_ai->ai_addrlen);
            if (res < 0) {
                freeaddrinfo(res_ai);
                HttpTransport_close(transport);
                return NULL;
            }
        } else 
#endif
        {
            transport->use_uring = false;
            int ret = connect(fd, res_ai->ai_addr, res_ai->ai_addrlen);
            if (ret < 0 && errno != EINPROGRESS) {
                freeaddrinfo(res_ai);
                HttpTransport_close(transport);
                return NULL;
            }
        }
        freeaddrinfo(res_ai);
    }

    return transport;
}

ssize_t HttpTransport_send(HttpTransport* self, const void* buf, size_t len) {
    if (!self || !self->sock) return -1;

#ifdef HAS_LIBURING
    if (self->use_uring) {
        return _do_async_send(self, self->sock->fd, buf, len);
    }
#endif
    if (self->sock->send) {
        return self->sock->send(self->sock, buf, len, NULL, 0);
    }
    return send(self->sock->fd, buf, len, MSG_NOSIGNAL);
}

ssize_t HttpTransport_recv(HttpTransport* self, void* buf, size_t len) {
    if (!self || !self->sock) return -1;

    /* ============================================================
     * 🛡️ [HTTPS 방어막]: io_uring 원시 수신 우회 (OpenSSL 복호화 필수)
     * ============================================================ */
    if (strcmp(self->scheme, "https") == 0) {
        ssize_t n;
        while (1) {
            // 다형성(Polymorphism)을 통해 안전하게 SSL_read 호출
            n = self->sock->recv(self->sock, buf, len, NULL, NULL);
            
            // 수신 성공
            if (n >= 0) return n;
            
            // 논블로킹 소켓 대기: 데이터가 아직 오지 않았을 때 CPU를 보호하며 대기
            if (errno == EAGAIN || errno == EWOULDBLOCK || n == SOCKET_WOULD_BLOCK) {
                usleep(1000); // 1ms 휴식 후 재시도
                continue;
            }
            
            // 소켓 끊김 등 진짜 에러 발생 시 즉시 탈출
            return -1; 
        }
    }

#ifdef HAS_LIBURING
    /* ============================================================
     * 🚀 [io_uring 풀파워]: 일반 HTTP (TCP) 통신 전용 초고속 비동기 수신
     * ============================================================ */
    if (self->use_uring) {
        ssize_t res;
        do {
            res = _do_async_recv(self, self->sock->fd, buf, len);
        } while (res == -EAGAIN || res == -EINTR);
        return res;
    }
#endif

    /* ============================================================
     * ⚓ [최후의 보루]: io_uring 미사용 HTTP 통신을 위한 Fallback
     * ============================================================ */
    if (self->sock->recv) {
        return self->sock->recv(self->sock, buf, len, NULL, NULL);
    }
    
    return recv(self->sock->fd, buf, len, 0);
}


int HttpTransport_getc(HttpTransport* self) {
    if (self->read_pos >= self->read_end) {
        ssize_t n = HttpTransport_recv(self, self->read_buf, sizeof(self->read_buf));
        if (n <= 0) return -1;
        self->read_pos = 0;
        self->read_end = (int)n;
    }
    return (unsigned char)self->read_buf[self->read_pos++];
}



int HttpTransport_recv_line(HttpTransport* self, char* line_buf, int max_len) {
    int i = 0;
    int c;
    while (i < max_len - 1) {
        c = HttpTransport_getc(self);
        if (c < 0) return (i > 0) ? i : -1; // 더 이상 읽을 게 없거나 에러
        
        if (c == '\n') break; // 줄바꿈 발견!
        if (c != '\r') line_buf[i++] = c; // \r은 무시하고 \n에서 끊음
    }
    line_buf[i] = '\0';
    return i;
}

void HttpTransport_close(HttpTransport* self) {
    if (self) {
        if (self->host) free(self->host);
        if (self->path) free(self->path);
        if (self->sock) RELEASE((Object*)self->sock);
        free(self);
    }
}
ssize_t HttpTransport_read(HttpTransport* transport, void* buffer, size_t size) {
    if (!transport || !transport->sock) return -1;

    // 1. SSL 통신(HTTPS)인지 확인
    if (strcmp(transport->scheme, "https") == 0) {
        // [주의] 여기에 Socket 객체의 SSL 읽기 메서드를 호출해야 합니다.
        // 만약 Socket 객체에 SSL 기능이 내장되어 있다면 transport->sock->read(...) 형태일 것입니다.
        // 현재 SslClient가 정의되지 않았으므로, 일단 소켓의 파일 디스크립터를 활용합니다.
        // HTTPS는 단순히 recv로 읽으면 암호화된 데이터가 나옵니다. 
        // 만약 소켓 라이브러리에 SSL_read 래퍼가 있다면 그걸 쓰셔야 합니다!
        return recv(transport->sock->fd, buffer, size, 0); 
    } 
    
    // 2. 일반 TCP 통신
    return recv(transport->sock->fd, buffer, size, 0);
}

