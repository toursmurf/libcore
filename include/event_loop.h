#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include "object.h"
#include "socket_base.h"
#include "timer.h"
#include <sys/epoll.h>
#include <stdbool.h>

/* 🚨 [핵심 패치] HAS_LIBURING 매크로에 따른 조건부 인클루드 */
#ifdef HAS_LIBURING
#include <liburing.h>
#endif

typedef enum {
    EV_READ  = 0x01,
    EV_WRITE = 0x02,
    EV_ERROR = 0x04
} EventMask;

typedef enum {
    EL_BACKEND_EPOLL = 0,
    EL_BACKEND_URING = 1
} EventLoopBackend;

typedef struct EventLoop EventLoop;

struct EventLoop {
    Object              base;
    EventLoopBackend    backend;

    int                 epoll_fd;

    /* 🚨 [핵심 패치] io_uring 구조체 조건부 컴파일 */
#ifdef HAS_LIBURING
    struct io_uring     ring;
#endif

    volatile bool       is_running;
    int                 max_events;
    struct epoll_event* event_buffer;
    struct _Logger* logger;

    Object** tracked_objs;

    int  (*addSocket)   (EventLoop* self, Socket* sock, EventMask mask);
    int  (*delSocket)   (EventLoop* self, Socket* sock);
    int  (*addTimer)    (EventLoop* self, Timer* timer);
    int  (*removeTimer) (EventLoop* self, Timer* timer);
    int  (*poll)        (EventLoop* self, int timeout_ms);

    void (*run)         (EventLoop* self);
    void (*stop)        (EventLoop* self);
};

EventLoop* new_EventLoop(int max_events);
void       event_loop_run(EventLoop* self);

#endif /* EVENT_LOOP_H */