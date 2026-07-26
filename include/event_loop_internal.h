#ifndef EVENT_LOOP_INTERNAL_H
#define EVENT_LOOP_INTERNAL_H

#include "event_loop.h"
#include "socket_base.h"
#include <stdint.h>
#include <stdbool.h>

#define EVENT_READ   0x01u
#define EVENT_WRITE  0x02u
#define EVENT_ERROR  0x04u
#define EVENT_CLOSE  0x08u

typedef enum {
    OP_READ,
    OP_WRITE,
    OP_ACCEPT,
    OP_CONNECT
} AsyncOperation;

/* OS별 비동기 I/O 코어 스위칭 체계 */
#if defined(_WIN32) || defined(_WIN64)
    #define LIBCORE_USE_IOCP
    #include <winsock2.h>
    #include <mswsock.h>
    #include <windows.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    #ifndef LIBCORE_USE_KQUEUE
        #define LIBCORE_USE_KQUEUE
    #endif
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>
#else
    #define LIBCORE_USE_EPOLL
    #include <sys/epoll.h>
#endif

/* V1.x SocketContext */
typedef struct {
    Socket* sock;
    void* platform_data;

#ifdef LIBCORE_USE_IOCP
    int pending_io;
    bool closing;
#endif
} SocketContext;

/* V1.x LibcoreEvent */
typedef struct {
    SocketContext* ctx;
    uint32_t mask;
    AsyncOperation operation;
    size_t transferred;
    int error_code;
    uint64_t timestamp_ms;
} LibcoreEvent;

#ifdef LIBCORE_USE_IOCP
typedef struct {
    WSAOVERLAPPED overlapped;
    WSABUF buffer;
    SocketContext* socket_ctx;
    AsyncOperation operation;
} IocpContext;
#endif

/* 불투명 포인터용 실제 구현체 (fd 매핑 테이블 내장) */
struct EventLoopImpl {
#if defined(LIBCORE_USE_IOCP)
    HANDLE iocp_handle;
#elif defined(LIBCORE_USE_KQUEUE)
    int kq_fd;
#else
    int epoll_fd;
#endif
    SocketContext* ctx_map[65536];
};

/* 5대 공통 백엔드 인터페이스 */
int  event_backend_init(EventLoop* loop);
int  event_backend_add(EventLoop* loop, Socket* sock, uint32_t mask);
int  event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask);
int  event_backend_remove(EventLoop* loop, Socket* sock);
int  event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms);
void event_backend_destroy(EventLoop* loop);

#endif /* EVENT_LOOP_INTERNAL_H */