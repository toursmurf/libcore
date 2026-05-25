#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include "object.h"
#include "socket_base.h"
#include "timer.h"
#include <sys/epoll.h>
#include <stdbool.h>

/* ────────────────────────────────────────
 * 이벤트 마스크 (Event Mask)
 * ──────────────────────────────────────── */
typedef enum {
    EV_READ  = 0x01,
    EV_WRITE = 0x02,
    EV_ERROR = 0x04
} EventMask;

typedef struct EventLoop EventLoop;

/* ────────────────────────────────────────
 * EventLoop 구조체 (Iron Fortress Core)
 * ──────────────────────────────────────── */
struct EventLoop {
    Object              base;
    int                 epoll_fd;

    /* volatile: 컴파일러의 레지스터 캐싱을 방지하여 ^C 신호를 즉각 인지함 ✅ */
    volatile bool       is_running;

    int                 max_events;
    struct epoll_event* event_buffer;
    struct _Logger* logger;

    /* [누수 소각로] RETAIN 객체 수감자 명부 (Dangling Pointer 원천 봉쇄) */
    Object* tracked_objs[65536];

    /* ─── 메서드 포인터 (Method Pointers) ─── */
    int  (*addSocket)   (EventLoop* self, Socket* sock, EventMask mask);
    int  (*delSocket)   (EventLoop* self, Socket* sock);
    int  (*addTimer)    (EventLoop* self, Timer* timer);
    int  (*removeTimer) (EventLoop* self, Timer* timer);
    int  (*poll)        (EventLoop* self, int timeout_ms);
    void (*run)         (EventLoop* self);
    void (*stop)        (EventLoop* self);
};

/* ────────────────────────────────────────
 * 퍼블릭 API (Public API)
 * ──────────────────────────────────────── */
EventLoop* new_EventLoop           (int max_events);
void       event_loop_run          (EventLoop* self);
int        event_loop_add_timer    (EventLoop* self, Timer* timer);
int        event_loop_remove_timer (EventLoop* self, Timer* timer);

#endif /* EVENT_LOOP_H */