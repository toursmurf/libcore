#ifndef SOCKET_BASE_H
#define SOCKET_BASE_H

#include "object.h"
#include "hashmap.h"
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// --- [네트워크 런타임 규격] ---
#define TCP_BACKLOG         128
#define SOCKET_WOULD_BLOCK  -2

/**
 * [의장님 제안]: 소켓 프로토콜 타입 정의
 */
typedef enum {
    SOCKET_TCP,
    SOCKET_UDP,
    SOCKET_UNIX
} SocketProtocol;

typedef struct Socket Socket;
struct Socket {
    Object  base;            // W1 ARC 엔진 (소문자 object_init 대응)
    int     fd;
    bool    is_open;
    SocketProtocol protocol; // 소켓 정체성 필드
		void* user_data;
    // --- [통합 VTable: Java Style] ---
    ssize_t (*send)(Socket* self, const void* buf, size_t len, const char* host, int port);
    ssize_t (*recv)(Socket* self, void* buf, size_t len, char* host, int* port);

    // 공통 유틸리티
    int     (*getFD)(Socket* self);
    void    (*close)(Socket* self);

    // --- [자식 위임 메서드: Delegation] ---
    int     (*bind)      (Socket* self, const char* host, int port);
    int     (*listen)    (Socket* self, int backlog);
    int     (*connect)   (Socket* self, const char* host, int port);

    // 이벤트 루프(EventLoop) 연동용 콜백
    void (*on_readable)(struct Socket* self, void* loop_ptr);
    void (*on_writable)(struct Socket* self, void* loop_ptr);
    void (*on_error)   (struct Socket* self, void* loop_ptr);
};

// 베이스 생성 및 소멸 함수
void Socket_init_base(Socket* self, int fd, SocketProtocol protocol);
void Socket_finalize(Object* obj);

// ----------------------------------------------------------------------------
// [트랜스포트 중앙화 팩토리 API 통합]
// ----------------------------------------------------------------------------
Socket* createServer(const char* url);
Socket* createUnixServer(const char* path);
HashMap* parse_url(const char* url);

#endif /* SOCKET_BASE_H */