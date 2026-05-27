#define _GNU_SOURCE 
#include "socket_base.h"
#include "tcp_socket.h"
#include "udp_socket.h"
#include "unix_socket.h"
#include "exception.h"
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
/**
 * @brief 서버 소켓 생성 마스터 팩토리 (Strict Iron Fortress Mode)
 */
Socket* createServer(const char* url, Exception** out_err) {
    // 🚨 [방어막 1] URL 자체 널 체크
    if (!url) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_URL, 0, "Server URL is NULL");
        }
        return NULL;
    }

    HashMap* info = parse_url(url);
    if (!info) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_URL, 0, "Server URL parsing failed");
        }
        return NULL;
    }

    const char* scheme = hashmap_get_str(info, "scheme");
    if (!scheme) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_SCHEME, 0, "Missing protocol scheme in URL");
        }
        RELEASE((Object*)info);
        return NULL;
    }

    Socket* sock = NULL;

    // 🚀 [프로토콜 라우팅 1] TCP Server 분기
    if (strncmp(scheme, "tcp", 16) == 0) {
        const char* host = hashmap_get_str(info, "host");
        const char* port_str = hashmap_get_str(info, "port");

        if (host && port_str) {
            int port = atoi(port_str);
            // 🚨 [방어막 2] 포트 바운더리 체크
            if (port <= 0 || port > 65535) {
                if (out_err) {
                    *out_err = throw_Exception(ERR_SOCK_PORT, 0, "TCP port out of range (1-65535)");
                }
            } else {
                sock = (Socket*)new_TcpServer(host, port);
                // 🚨 [방어막 3] 시스템 바인드/리스 실패 시 errno 낚아채기
                if (!sock && out_err) {
                    *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to initialize TCP Server Socket");
                }
            }
        } else {
            if (out_err) {
                *out_err = throw_Exception(ERR_SOCK_URL, 0, "TCP URL components (host/port) missing");
            }
        }
    }
    // 🚀 [프로토콜 라우팅 2] UDP Server 분기
    else if (strncmp(scheme, "udp", 16) == 0) {
        const char* host = hashmap_get_str(info, "host");
        const char* port_str = hashmap_get_str(info, "port");

        if (host && port_str) {
            int port = atoi(port_str);
            if (port <= 0 || port > 65535) {
                if (out_err) {
                    *out_err = throw_Exception(ERR_SOCK_PORT, 0, "UDP port out of range (1-65535)");
                }
            } else {
                sock = (Socket*)new_UdpServer(host, port);
                if (!sock && out_err) {
                    *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to initialize UDP Server Socket");
                }
            }
        } else {
            if (out_err) {
                *out_err = throw_Exception(ERR_SOCK_URL, 0, "UDP URL components (host/port) missing");
            }
        }
    }
    // 🚀 [프로토콜 라우팅 3] UNIX Domain Server 분기
    else if (strncmp(scheme, "unix", 16) == 0) {
        const char* path = hashmap_get_str(info, "path"); // 내부 파서 규격 매핑

        if (path) {
            sock = (Socket*)new_UnixServer(path);
            if (!sock && out_err) {
                *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to initialize UNIX Domain Server Socket");
            }
        } else {
            if (out_err) {
                *out_err = throw_Exception(ERR_SOCK_URL, 0, "UNIX Domain socket path missing");
            }
        }
    }
    // 🚨 미지원 프로토콜 방어막
    else {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_SCHEME, 0, "Unsupported protocol scheme");
        }
    }

    // 🧹 [제국 ARC 율법] 파싱에 쓰인 임시 HashMap 자원 즉시 소각!
    RELEASE((Object*)info);

    return sock;
}

/**
 * @brief 클라이언트 소켓 생성 마스터 팩토리 (TCP/UDP/UNIX 통합)
 * @param url "tcp://127.0.0.1:8080", "udp://127.0.0.1:9000", "unix:///tmp/app.sock"
 * @param out_err [OUT] 예외 발생 시 Exception 객체 저장 포인터 (NULL 허용)
 * @return [OWNED] Socket* (성공 시 소켓 객체 반환, 실패 시 NULL 반환하며 out_err 채움)
 */
Socket* createClient(const char* url, Exception** out_err) {
    // 🚨 [방어막 1] URL 자체 널 체크
    if (!url) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_URL, 0, "Client URL is NULL");
        }
        return NULL;
    }

    HashMap* info = parse_url(url);
    if (!info) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_URL, 0, "Client URL parsing failed");
        }
        return NULL;
    }

    const char* scheme = hashmap_get_str(info, "scheme");
    if (!scheme) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_SCHEME, 0, "Missing protocol scheme in URL");
        }
        RELEASE((Object*)info); // 🧹 예외 시에도 잊지 않는 ARC 소각!
        return NULL;
    }

    Socket* sock = NULL;

    // 🚀 [프로토콜 라우팅 1] TCP Client 분기
    if (strncmp(scheme, "tcp", 16) == 0) {
        const char* host = hashmap_get_str(info, "host");
        const char* port_str = hashmap_get_str(info, "port");

        if (host && port_str) {
            int port = atoi(port_str);
            // 🚨 [방어막 2] 포트 바운더리 체크
            if (port <= 0 || port > 65535) {
                if (out_err) {
                    *out_err = throw_Exception(ERR_SOCK_PORT, 0, "TCP port out of range (1-65535)");
                }
            } else {
                sock = (Socket*)new_TcpClient(host, port);
                // 🚨 [방어막 3] 접속 실패 시 errno 낚아채기
                if (!sock && out_err) {
                    *out_err = throw_Exception(ERR_SOCK_CONNECT, errno, "Failed to connect TCP Client");
                }
            }
        } else {
            if (out_err) {
                *out_err = throw_Exception(ERR_SOCK_URL, 0, "TCP URL components (host/port) missing");
            }
        }
    }
    // 🚀 [프로토콜 라우팅 2] UDP Client 분기
    else if (strncmp(scheme, "udp", 16) == 0) {
        // UDP는 비연결형이므로 host/port 바인딩 불필요 (타격 목표는 send 단에서 지정)
        sock = (Socket*)new_UdpClient();
        if (!sock && out_err) {
            *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to initialize UDP Client Socket");
        }
    }
    // 🚀 [프로토콜 라우팅 3] UNIX Domain Client 분기
    else if (strncmp(scheme, "unix", 16) == 0) {
        const char* path = hashmap_get_str(info, "path");

        if (path) {
            sock = (Socket*)new_UnixClient(path);
            if (!sock && out_err) {
                *out_err = throw_Exception(ERR_SOCK_CONNECT, errno, "Failed to connect UNIX Domain Client");
            }
        } else {
            if (out_err) {
                *out_err = throw_Exception(ERR_SOCK_URL, 0, "UNIX Domain socket path missing");
            }
        }
    }
    // 🚨 미지원 프로토콜 방어막
    else {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_SCHEME, 0, "Unsupported protocol scheme");
        }
    }

    // 🧹 [제국 ARC 율법] 합법 패턴 확인 완료된 HashMap 강제 소각 (메모리 누수 원천 차단)
    RELEASE((Object*)info);

    return sock;
}

/**
 * @brief Unix 도메인 소켓 서버 생성 팩토리 (Absolute Compliance)
 */
Socket* createUnixServer(const char* path, Exception** out_err) {
    // 🚨 [방어막] 경로 NULL 체크
    if (!path) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_URL, 0, "Unix socket path is NULL");
        }
        return NULL;
    }

    // 🚀 서버 생성 및 에러 캡처 (errno 보존)
    Socket* sock = (Socket*)new_UnixServer(path);
    if (!sock) {
        if (out_err) {
            *out_err = throw_Exception(ERR_SOCK_CREATE, errno, "Failed to create Unix server socket");
        }
        return NULL;
    }

    return sock;
}

/**
 * @brief Unix 도메인 소켓 클라이언트 전용 생성 팩토리 🚀 [NEW]
 */
Socket* createUnixClient(const char* path, Exception** out_err) {
    if (!path) {
        if (out_err) *out_err = throw_Exception(ERR_SOCK_URL, 0, "Unix socket path is NULL");
        return NULL;
    }

    Socket* sock = (Socket*)new_UnixClient(path);
    if (!sock) {
        if (out_err) *out_err = throw_Exception(ERR_SOCK_CONNECT, errno, "Failed to connect Unix client socket");
        return NULL;
    }
    return sock;
}

// ============================================================================
// [6] 멀티스레드 동기화(Sync) 소켓 통합 팩토리 (Blocking Mode)
// ============================================================================

/**
 * @brief 소켓의 논블로킹(O_NONBLOCK) 족쇄를 풀어 동기(Blocking) 모드로 전환하는 내부 유틸
 */
static void Socket_strip_nonblock(Socket* sock) {
    if (!sock || sock->fd < 0) return;
    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(sock->fd, F_SETFL, flags & ~O_NONBLOCK); // 🚨 논블로킹 플래그 제거!
    }
}

/**
 * @brief 동기(Blocking) 서버 소켓 생성 마스터 팩토리
 */
Socket* createSyncServer(const char* url, Exception** out_err) {
    Socket* sock = createServer(url, out_err);
    if (sock) Socket_strip_nonblock(sock); // 생성 성공 시 동기 모드로 변환
    return sock;
}

/**
 * @brief 동기(Blocking) 클라이언트 소켓 생성 마스터 팩토리
 */
Socket* createSyncClient(const char* url, Exception** out_err) {
    Socket* sock = createClient(url, out_err);
    if (sock) Socket_strip_nonblock(sock);
    return sock;
}

/**
 * @brief 동기(Blocking) Unix 도메인 소켓 서버 팩토리
 */
Socket* createSyncUnixServer(const char* path, Exception** out_err) {
    Socket* sock = createUnixServer(path, out_err);
    if (sock) Socket_strip_nonblock(sock);
    return sock;
}

/**
 * @brief 동기(Blocking) Unix 도메인 소켓 클라이언트 팩토리
 */
Socket* createSyncUnixClient(const char* path, Exception** out_err) {
    Socket* sock = createUnixClient(path, out_err);
    if (sock) Socket_strip_nonblock(sock);
    return sock;
}