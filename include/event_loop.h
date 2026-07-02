#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include "object.h"
#include "socket_base.h"
#include "timer.h"
#include "arraylist.h"
#include <sys/epoll.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef HAS_LIBURING
#include <liburing.h>
#endif

/* 🚨 [EventLoop 동기화 율법 (Thread Contract)]
 * 본 EventLoop의 addSocket, delSocket, poll 등 모든 상태 변경 함수는
 * 반드시 'EventLoop를 실행 중인 단일 스레드'에서만 호출되어야 한다.
 */

typedef enum {
    EV_READ  = 0x01,
    EV_WRITE = 0x02,
    EV_ERROR = 0x04
} EventMask;

typedef enum {
    EL_BACKEND_EPOLL = 0,
    EL_BACKEND_URING = 1
} EventLoopBackend;

typedef struct PollContext {
    Socket* sock;
    uint32_t fd;
    uint32_t generation;
} PollContext;

typedef struct EventLoop EventLoop;

struct EventLoop {
    Object              base;
    EventLoopBackend    backend;

    int                 epoll_fd;

#ifdef HAS_LIBURING
    struct io_uring     ring;
#endif

    volatile bool       is_running;
    int                 max_events;
    struct epoll_event* event_buffer;
    struct _Logger* logger;

    PollContext* ctx_pool;              /* [OWNED] Socket 65536개 관리 풀 */
    ArrayList* deferred_cleanup_list;   /* [OWNED] 안전 지대 소각 대기열 */

    int  (*addSocket)   (EventLoop* self, Socket* sock, EventMask mask);
    int  (*delSocket)   (EventLoop* self, Socket* sock);

    void (*deferRelease)(EventLoop* self, Object* obj);

    int  (*addTimer)    (EventLoop* self, Timer* timer);
    int  (*removeTimer) (EventLoop* self, Timer* timer);

    int  (*poll)        (EventLoop* self, int timeout_ms);

    void (*run)         (EventLoop* self);
    void (*stop)        (EventLoop* self);
};

EventLoop* new_EventLoop(int max_events);
void       event_loop_run(EventLoop* self);

#endif /* EVENT_LOOP_H */