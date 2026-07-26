#define _GNU_SOURCE 
#include "tcp_socket.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h> /* 🚨 [핵심] TCP 옵션 헤더 */

// [1] 전방 선언 및 클래스 정의
static void TcpSocket_finalize(Object* obj);

static const Class _tcpSocketClass = {
    .name = "TcpSocket",
    .size = sizeof(TcpSocket),
    .finalize = TcpSocket_finalize
};

const Class* tcp_socket_class_ptr(void) {
    return &_tcpSocketClass;
}

// ----------------------------------------------------------------------------
// [VTable 구현부] TCP 전용 무기들
// ----------------------------------------------------------------------------

static int TcpSocket_bind_impl(Socket* s, const char* ip, int port) {
    if (!s) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (ip) inet_pton(AF_INET, ip, &addr.sin_addr);
    else addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    return bind(s->fd, (struct sockaddr*)&addr, sizeof(addr));
}

static int TcpSocket_listen_impl(Socket* s, int backlog) {
    if (!s) return -1;
    return listen(s->fd, (backlog > 0) ? backlog : TCP_BACKLOG);
}

static int TcpSocket_connect_impl(Socket* s, const char* ip, int port) {
    if (!s || !ip) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int res = connect(s->fd, (struct sockaddr*)&addr, sizeof(addr));

    if (res < 0 && errno == EINPROGRESS) return SOCKET_WOULD_BLOCK;
    return res;
}

static TcpSocket* TcpSocket_accept_impl(TcpSocket* self, char* ip, int* port) {
    if (!self) return NULL;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    /* 🚀 [패치] macOS 호환성을 위한 accept 분기 처리 */
    int c_fd = -1;
#if defined(__linux__) || defined(__gnu_linux__)
    c_fd = accept4(self->base.fd, (struct sockaddr*)&addr, &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    c_fd = accept(self->base.fd, (struct sockaddr*)&addr, &addr_len);
    if (c_fd >= 0) {
        int flags = fcntl(c_fd, F_GETFL, 0);
        fcntl(c_fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    if (c_fd < 0) return NULL;

    if (ip) inet_ntop(AF_INET, &addr.sin_addr, ip, INET_ADDRSTRLEN);
    if (port) *port = ntohs(addr.sin_port);

    return new_TcpSocket_from_fd(c_fd);
}

// ----------------------------------------------------------------------------
// [팩토리 메서드]
// ----------------------------------------------------------------------------

TcpSocket* new_TcpSocket_from_fd(int fd) {
    if (fd < 0) return NULL;
    TcpSocket* self = (TcpSocket*)calloc(1, sizeof(TcpSocket));
    if (!self) return NULL;

    Socket_init_base(&self->base, fd, SOCKET_TCP);

    /* 🚨 [클순 부장님 마스터 패치]
     * 클라이언트/서버 불문하고 모든 TCP 소켓의 Nagle 족쇄를 원천 해제!
     * 클라이언트 측 버퍼링(1턴 지연) 영구 박멸! */
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

    self->base.base.type = &_tcpSocketClass;

    self->base.bind    = TcpSocket_bind_impl;
    self->base.listen  = TcpSocket_listen_impl;
    self->base.connect = TcpSocket_connect_impl;
    self->accept       = TcpSocket_accept_impl;

    return self;
}

TcpSocket* new_TcpServer(const char* ip, int port) {
    /* 🚀 [패치] macOS 호환성을 위한 socket 분기 처리 */
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
    if (fd < 0) return NULL;

    TcpSocket* self = new_TcpSocket_from_fd(fd);
    if (!self) { close(fd); return NULL; }

    if (self->base.bind(&self->base, ip, port) != 0 ||
        self->base.listen(&self->base, TCP_BACKLOG) != 0) {
        RELEASE(self);
        return NULL;
    }
    return self;
}

TcpSocket* new_TcpClient(const char* ip, int port) {
    /* 🚀 [패치] macOS 호환성을 위한 socket 분기 처리 */
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
    if (fd < 0) return NULL;

    TcpSocket* self = new_TcpSocket_from_fd(fd);
    if (!self) { close(fd); return NULL; }

    if (ip) {
        int ret = self->base.connect(&self->base, ip, port);
        if (ret < 0 && ret != SOCKET_WOULD_BLOCK) {
            RELEASE(self);
            return NULL;
        }
    }
    return self;
}

static void TcpSocket_finalize(Object* obj) {
    Socket_finalize(obj);
}