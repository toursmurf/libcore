#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include "object.h"
#include "socket_base.h"
#include "timer.h"
#include <sys/epoll.h>
#include <liburing.h>
#include <stdbool.h>

/* ────────────────────────────────────────
 * 이벤트 마스크 (Event Mask)
 * ──────────────────────────────────────── */
typedef enum {
    EV_READ  = 0x01,
    EV_WRITE = 0x02,
    EV_ERROR = 0x04
} EventMask;

/* ────────────────────────────────────────
 * 하이브리드 백엔드 열거형
 * ──────────────────────────────────────── */
typedef enum {
    EL_BACKEND_EPOLL = 0,
    EL_BACKEND_URING = 1
} EventLoopBackend;

typedef struct EventLoop EventLoop;

/* ────────────────────────────────────────
 * EventLoop 구조체 (Hybrid Engine Core)
 * ──────────────────────────────────────── */
struct EventLoop {
    Object              base;
    EventLoopBackend    backend;

    /* 커널 리소스 */
    int                 epoll_fd;
    struct io_uring     ring;

    volatile bool       is_running;
    int                 max_events;
    struct epoll_event* event_buffer;
    struct _Logger* logger;

    /* Heap 기반 동적 배열 포인터 */
    Object** tracked_objs;

    /* ─── VTable: 다이나믹 바인딩 ─── */
    int  (*addSocket)   (EventLoop* self, Socket* sock, EventMask mask);
    int  (*delSocket)   (EventLoop* self, Socket* sock);
    int  (*addTimer)    (EventLoop* self, Timer* timer);
    int  (*removeTimer) (EventLoop* self, Timer* timer);
    int  (*poll)        (EventLoop* self, int timeout_ms);

    /* 공통 런타임 제어 */
    void (*run)         (EventLoop* self);
    void (*stop)        (EventLoop* self);
};

EventLoop* new_EventLoop           (int max_events);
void       event_loop_run          (EventLoop* self);

#endif /* EVENT_LOOP_H */