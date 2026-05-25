#define _GNU_SOURCE 
#include "socket_base.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "unix_socket.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// [제국 표준 Class 명함]
static const Class _Socket_Class = {
    .name     = "Socket",
    .size     = sizeof(Socket),
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
    if (self && self->fd >= 0) self->close(self);
}

void Socket_init_base(Socket* self, int fd, SocketProtocol protocol) {
    if (!self) return;

    // [W1 제국 표준]: 객체 메타데이터 초기화
    Object_Init((Object*)self, &_Socket_Class);

    self->fd       = fd;
    self->is_open  = (fd >= 0);
    self->protocol = protocol;

    // 통합 인터페이스 매핑
    self->send  = Socket_send_unified;
    self->recv  = Socket_recv_unified;
    self->getFD = Socket_getFD_impl;
    self->close = Socket_close_impl;

    self->bind    = NULL;
    self->listen  = NULL;
    self->connect = NULL;

    // [v1.0 코어 전술]: 기본 Non-blocking 모드 강제
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

// ----------------------------------------------------------------------------
// [4] PHP 스타일 HashMap URL 파서
// ----------------------------------------------------------------------------
HashMap* parse_url(const char* url) {
    if (!url) return NULL;

    HashMap* result = new_HashMap(30);
    if (!result) return NULL;

    const char* sep = strstr(url, "://");
    if (!sep) {
        RELEASE(result);
        return NULL;
    }

    char scheme[16] = {0};
    size_t scheme_len = (size_t)(sep - url);
    if (scheme_len >= sizeof(scheme)) {
        scheme_len = sizeof(scheme) - 1;
    }
    strncpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';
    hashmap_put_str(result, "scheme", scheme);

    const char* rest = sep + 3;

    // strncmp 16자 버퍼 경계 체크 가동 및 path 매핑
    if (strncmp(scheme, "unix", 16) == 0) {
        hashmap_put_str(result, "path", rest);
        hashmap_put_str(result, "port", "0");
        return result;
    }

    const char* path_start = strchr(rest, '/');
    if (path_start) {
        hashmap_put_str(result, "path", path_start);
    }

    size_t hostport_len = path_start ? (size_t)(path_start - rest) : strlen(rest);
    char hostport[256] = {0};
    if (hostport_len >= sizeof(hostport)) {
        hostport_len = sizeof(hostport) - 1;
    }
    strncpy(hostport, rest, hostport_len);
    hostport[hostport_len] = '\0';

    const char* port_sep = strrchr(hostport, ':');
    if (port_sep) {
        char host[256] = {0};
        size_t host_len = (size_t)(port_sep - hostport);
        if (host_len >= sizeof(host)) {
            host_len = sizeof(host) - 1;
        }
        strncpy(host, hostport, host_len);
        host[host_len] = '\0';
        hashmap_put_str(result, "host", host);
        hashmap_put_str(result, "port", port_sep + 1);
    } else {
        hashmap_put_str(result, "host", hostport);
        hashmap_put_str(result, "port", "0");
    }

    return result;
}

// ----------------------------------------------------------------------------
// [5] 다형성 소켓 통합 팩토리 (Polymorphic Factory)
// ----------------------------------------------------------------------------
Socket* createServer(const char* url) {
    if (!url) return NULL;

    HashMap* info = parse_url(url);
    if (!info) return NULL;

    const char* scheme = hashmap_get_str(info, "scheme");
    Socket* sock = NULL;

    if (!scheme) {
        RELEASE(info);
        return NULL;
    }

    // strncmp 16자 바운더리 체크 및 포트 유효성 가드 적용
    if (strncmp(scheme, "tcp", 16) == 0) {
        const char* host = hashmap_get_str(info, "host");
        const char* port_str = hashmap_get_str(info, "port");
        if (host && port_str) {
            int port = atoi(port_str);
            if (port <= 0 || port > 65535) {
                RELEASE(info);
                return NULL;
            }
            sock = (Socket*)new_TcpServer(host, port);
        }
    } else if (strncmp(scheme, "udp", 16) == 0) {
        const char* host = hashmap_get_str(info, "host");
        const char* port_str = hashmap_get_str(info, "port");
        if (host && port_str) {
            int port = atoi(port_str);
            if (port <= 0 || port > 65535) {
                RELEASE(info);
                return NULL;
            }
            sock = (Socket*)new_UdpServer(host, port);
        }
    } else if (strncmp(scheme, "unix", 16) == 0) {
        const char* path = hashmap_get_str(info, "path");
        if (path) {
            sock = (Socket*)new_UnixServer(path);
        }
    }

    RELEASE(info);
    return sock;
}

Socket* createUnixServer(const char* path) {
    if (!path) return NULL;
    return (Socket*)new_UnixServer(path);
}