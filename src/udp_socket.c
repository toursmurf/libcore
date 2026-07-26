#define _GNU_SOURCE 
#include "udp_socket.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <arpa/inet.h>
#include <fcntl.h> /* 🚀 [패치] fcntl 추가 */

static void UdpSocket_finalize(Object* obj) { // 👈 void* -> Object*
    Socket_finalize(obj);
}

static const Class _udpSocketClass = {
		.name = "UdpSocket",
		.size = sizeof(UdpSocket),
    .finalize = UdpSocket_finalize
};

const Class* udp_socket_class_ptr(void) {
    return &_udpSocketClass;
}

static int UdpSocket_bind_impl(Socket* s, const char* host, int port) {
    if (!s || s->fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (host) inet_pton(AF_INET, host, &addr.sin_addr);
    else addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int opt = 1;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    return bind(s->fd, (struct sockaddr*)&addr, sizeof(addr));
}

UdpSocket* new_UdpSocket_from_fd(int fd) {
    if (fd < 0) return NULL;
    UdpSocket* self = (UdpSocket*)calloc(1, sizeof(UdpSocket));
    if (!self) return NULL;

    // [수정] 인자 3개: SOCKET_UDP 명시
    Socket_init_base(&self->base, fd, SOCKET_UDP);

    self->base.base.type = &_udpSocketClass;
    self->base.bind = UdpSocket_bind_impl;

    return self;
}

UdpSocket* new_UdpServer(const char* host, int port) {
    /* 🚀 [패치] macOS 호환성을 위한 socket 분기 처리 */
    int fd = -1;
#if defined(__linux__) || defined(__gnu_linux__)
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
    if (fd < 0) return NULL;

    UdpSocket* self = new_UdpSocket_from_fd(fd);
    if (!self) { close(fd); return NULL; }
    if (self->base.bind(&self->base, host, port) != 0) {
        RELEASE(self);
        return NULL;
    }
    return self;
}

UdpSocket* new_UdpClient(void) {
    /* 🚀 [패치] macOS 호환성을 위한 socket 분기 처리 */
    int fd = -1;
#if defined(__linux__) || defined(__gnu_linux__)
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
    if (fd < 0) return NULL;

    UdpSocket* self = new_UdpSocket_from_fd(fd);
    if (!self) { close(fd); return NULL; }
    return self;
}