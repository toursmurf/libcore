#define _GNU_SOURCE 
#include "socket_base.h"
#include <unistd.h> 
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

// [제국 표준 Class 명함]
static const Class _Socket_Class = {
		.name = "Socket",
		.size = sizeof(Socket),
    .finalize = Socket_finalize
};

// ----------------------------------------------------------------------------
// [구현부] 기본 유틸리티 (사령관님 최종 수정안 반영)
// ----------------------------------------------------------------------------
static int Socket_getFD_impl(Socket* s) {
    return (s) ? s->fd : -1;
}

static void Socket_close_impl(Socket* s) {
    if (s && s->fd >= 0) {
        // [사령관님 지침]: 실패 여부와 상관없이 FD를 무효화하여 이중 해제 방지
        close(s->fd);
        s->fd = -1;
        s->is_open = false;
    }
}

// ----------------------------------------------------------------------------
// [1] 통합 송신 (Java-like Polymorphism)
// ----------------------------------------------------------------------------
static ssize_t Socket_send_unified(Socket* self, const void* buf, size_t len, const char* host, int port) {
    if (!self || !self->is_open || self->fd < 0) return -1;

    // [TCP & Unix]: 연결 지향형 전송 (신뢰성 루프)
    if (self->protocol == SOCKET_TCP || self->protocol == SOCKET_UNIX) {
        size_t total = 0;
        const char* p = (const char*)buf;
        while (total < len) {
            ssize_t n = send(self->fd, p + total, len - total, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return (total > 0) ? (ssize_t)total : SOCKET_WOULD_BLOCK;
                return -1;
            }
            if (n == 0) { self->is_open = false; return -1; }
            total += (size_t)n;
        }
        return (ssize_t)total;
    }
    // [UDP]: 비연결형 전송 (Datagram 타격)
    else if (self->protocol == SOCKET_UDP) {
        if (!host) return -1; // [클순 부장] 호스트 미지정 시 방어

        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) return -1;

        ssize_t n = sendto(self->fd, buf, len, 0, (struct sockaddr*)&addr, sizeof(addr));
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return SOCKET_WOULD_BLOCK;
        return n;
    }
    return -1;
}

// ----------------------------------------------------------------------------
// [2] 통합 수신
// ----------------------------------------------------------------------------
static ssize_t Socket_recv_unified(Socket* self, void* buf, size_t len, char* host, int* port) {
    if (!self || !self->is_open || self->fd < 0) return -1;

    // [TCP & Unix]: 데이터 스트림 수신
    if (self->protocol == SOCKET_TCP || self->protocol == SOCKET_UNIX) {
        ssize_t n = recv(self->fd, buf, len, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return SOCKET_WOULD_BLOCK;
            return -1;
        }
        if (n == 0) self->is_open = false;
        return n;
    }
    // [UDP]: 발신자 주소와 함께 수신
    else {
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        ssize_t n = recvfrom(self->fd, buf, len, 0, (struct sockaddr*)&src_addr, &addr_len);

        if (n >= 0 && host && port) {
            inet_ntop(AF_INET, &src_addr.sin_addr, host, INET_ADDRSTRLEN);
            *port = ntohs(src_addr.sin_port);
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return SOCKET_WOULD_BLOCK;
        return n;
    }
}

// ----------------------------------------------------------------------------
// [3] 소멸자 및 베이스 초기화
// ----------------------------------------------------------------------------
void Socket_finalize(Object* obj) {
    Socket* self = (Socket*)obj;
    // VTable에 등록된 close 함수를 호출하여 안전하게 자원 해제
    if (self && self->fd >= 0) self->close(self);
}

void Socket_init_base(Socket* self, int fd, SocketProtocol protocol) {
    if (!self) return;

    // [W1 제국 표준]: 객체 메타데이터 초기화 (Object_Init 규격 사용)
    Object_Init((Object*)self, &_Socket_Class);

    self->fd = fd;
    self->is_open = (fd >= 0);
    self->protocol = protocol;

    // 👑 통합 인터페이스 매핑
    self->send    = Socket_send_unified;
    self->recv    = Socket_recv_unified;
    self->getFD   = Socket_getFD_impl;
    self->close   = Socket_close_impl;

    // 자식 위임 메서드는 초기값 NULL (자식 생성자에서 오버라이딩 유도)
    self->bind    = NULL;
    self->listen  = NULL;
    self->connect = NULL;

    // [v1.0 코어 전술]: 기본 Non-blocking 모드 강제
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}
