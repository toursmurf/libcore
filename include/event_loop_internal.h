#ifndef EVENT_LOOP_INTERNAL_H
#define EVENT_LOOP_INTERNAL_H

#include "event_loop.h"
#include "socket_base.h"
#include "object.h"
#include <stdint.h>
#include <stdbool.h>

#define EVENT_READ   0x01u
#define EVENT_WRITE  0x02u
#define EVENT_ERROR  0x04u
#define EVENT_CLOSE  0x08u

typedef enum {
    OP_READ, OP_WRITE, OP_ACCEPT, OP_CONNECT
} AsyncOperation;

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

typedef struct {
    Object base;
    int is_timer;
    Socket* sock;
    Timer* timer;
    void* platform_data;
    uint32_t registered_mask;

#ifdef LIBCORE_USE_IOCP
    int pending_io;
    bool closing;
#endif
} SocketContext;

typedef struct {
    int is_timer;
    Timer* timer;
    SocketContext* ctx;
    uint32_t mask;
    AsyncOperation operation;
    size_t transferred;
    int error_code;
    uint64_t timestamp_ms;
} LibcoreEvent;

#define DEFER_CAP 64

struct EventLoopImpl {
#if defined(LIBCORE_USE_IOCP)
    HANDLE iocp_handle;
#elif defined(LIBCORE_USE_KQUEUE)
    int kq_fd;
#else
    int epoll_fd;
#endif
    SocketContext* ctx_map[65536];

    /* 동적 할당되는 지연 해제 큐 (Overflow 방어) */
    Object** defer_pending;
    int      defer_count;
    int      defer_capacity;

    Timer* active_timers[1024];
    int    active_timer_count;
};

int  event_backend_init(EventLoop* loop);
int  event_backend_add(EventLoop* loop, Socket* sock, uint32_t mask);
int  event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask);
int  event_backend_remove(EventLoop* loop, Socket* sock);
int  event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms);
void event_backend_destroy(EventLoop* loop);

int event_backend_add_timer(EventLoop* loop, Timer* timer);
int event_backend_remove_timer(EventLoop* loop, Timer* timer);

#endif /* EVENT_LOOP_INTERNAL_H */