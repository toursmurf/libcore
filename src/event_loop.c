#define _GNU_SOURCE
#include "event_loop.h"
#include "logger.h"
#include <sys/epoll.h>
#include <liburing.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

extern Logger* logger;
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger,  fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) LOG_WARN(logger,  fmt, ##__VA_ARGS__)

/* ────────────────────────────────────────
 * [커널 지원 자동 감지 - Scout Pattern]
 * ──────────────────────────────────────── */
static bool detect_uring(void) {
    struct io_uring ring;
    int ret = io_uring_queue_init(1, &ring, 0);
    if (ret == 0) {
        io_uring_queue_exit(&ring);
        return true;
    }
    return false;
}

/* ────────────────────────────────────────
 * 내부 유틸리티
 * ──────────────────────────────────────── */
static uint32_t map_events_to_epoll(EventMask mask) {
    uint32_t events = 0;
    if (mask & EV_READ)  events |= EPOLLIN;
    if (mask & EV_WRITE) events |= EPOLLOUT;
    events |= (EPOLLERR | EPOLLHUP | EPOLLET);
    return events;
}

static uint32_t map_events_to_poll(EventMask mask) {
    uint32_t events = 0;
    if (mask & EV_READ)  events |= POLLIN;
    if (mask & EV_WRITE) events |= POLLOUT;
    events |= (POLLERR | POLLHUP);
    return events;
}

static EventMask map_epoll_to_events(uint32_t events) {
    EventMask mask = 0;
    if (events & EPOLLIN)               mask |= EV_READ;
    if (events & EPOLLOUT)              mask |= EV_WRITE;
    if (events & (EPOLLERR | EPOLLHUP)) mask |= EV_ERROR;
    return mask;
}

static EventMask map_poll_to_events(uint32_t events) {
    EventMask mask = 0;
    if (events & POLLIN)               mask |= EV_READ;
    if (events & POLLOUT)              mask |= EV_WRITE;
    if (events & (POLLERR | POLLHUP))  mask |= EV_ERROR;
    return mask;
}

/* ────────────────────────────────────────
 * 소멸자: 🚨 백엔드 분기 및 동적 배열 소각로 🚨
 * ──────────────────────────────────────── */
static void EventLoop_finalize(Object* obj) {
    EventLoop* self = (EventLoop*)obj;

    /* 🚨 동적 배열 릴리즈 및 댕글링 방어 */
    if (self->tracked_objs) {
        for (int i = 0; i < 65536; i++) {
            if (self->tracked_objs[i] != NULL) {
                RELEASE(self->tracked_objs[i]);
                self->tracked_objs[i] = NULL;
            }
        }
        free(self->tracked_objs);
        self->tracked_objs = NULL;
    }

    if (self->backend == EL_BACKEND_URING) {
        io_uring_queue_exit(&self->ring);
    } else {
        if (self->epoll_fd >= 0) close(self->epoll_fd);
    }

    if (self->event_buffer) {
        free(self->event_buffer);
        self->event_buffer = NULL;
    }
}

/* ============================================================================
 * [EPOLL 백엔드 구현부] (Fallback)
 * ============================================================================ */
static int epoll_addSocket_impl(EventLoop* self, Socket* sock, EventMask mask) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;
    struct epoll_event ev = { .events = map_events_to_epoll(mask), .data.ptr = sock };

    if (self->tracked_objs[sock->fd] == NULL) {
        self->tracked_objs[sock->fd] = (Object*)sock;
        RETAIN((Object*)sock);
    }
    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, sock->fd, &ev) == -1) {
        if (errno == EEXIST && epoll_ctl(self->epoll_fd, EPOLL_CTL_MOD, sock->fd, &ev) == 0) return 0;
        self->tracked_objs[sock->fd] = NULL;
        RELEASE((Object*)sock);
        return -1;
    }
    return 0;
}

static int epoll_delSocket_impl(EventLoop* self, Socket* sock) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;
    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, sock->fd, NULL) == -1) return -1;

    if (self->tracked_objs[sock->fd] != NULL) {
        self->tracked_objs[sock->fd] = NULL;
        RELEASE((Object*)sock);
    }
    return 0;
}

/* 🚨 uring_poll_impl 전체 교체: 자동 재장전(Auto-Reload) 엔진 탑재 */
static int uring_poll_impl(EventLoop* self, int timeout_ms) {
    struct io_uring_cqe *cqe;
    unsigned head;
    int count = 0;
    int rearm_count = 0; /* 🚀 패치: 재장전할 소켓 개수 추적 */

    struct __kernel_timespec ts = {
        .tv_sec  = timeout_ms / 1000,
        .tv_nsec = (timeout_ms % 1000) * 1000000
    };

    int ret = io_uring_wait_cqe_timeout(&self->ring, &cqe, &ts);
    if (ret < 0) return ret;

    io_uring_for_each_cqe(&self->ring, head, cqe) {
        Object* obj = (Object*)io_uring_cqe_get_data(cqe);
        if (obj != NULL && obj != (Object*)-1) {
            RETAIN(obj);
            if (strcmp(obj->type->name, "Timer") == 0) {
                /* 타이머 생략 */
            } else {
                Socket* sock = (Socket*)obj;
                EventMask triggered = map_poll_to_events(cqe->res);

                /* 1. 이벤트 처리 (격발) */
                if (sock->is_open && (triggered & EV_READ)  && sock->on_readable) sock->on_readable(sock, self);
                if (sock->is_open && (triggered & EV_WRITE) && sock->on_writable) sock->on_writable(sock, self);
                if (sock->is_open && (triggered & EV_ERROR) && sock->on_error)    sock->on_error(sock, self);

                /* 2. 🚨 [핵심 패치] 살아있는 소켓 커널 재장전 (Re-arm) */
                if (sock->is_open && self->tracked_objs[sock->fd] != NULL) {
                    bool needs_rearm = true;

                    /* 최신 커널(5.13+)의 MULTI 모드가 살아있다면 장전 생략 */
                    #ifdef IORING_CQE_F_MORE
                    if (cqe->flags & IORING_CQE_F_MORE) {
                        needs_rearm = false;
                    }
                    #endif

                    if (needs_rearm) {
                        EventMask rearm_mask = 0;
                        if (sock->on_readable) rearm_mask |= EV_READ;
                        if (sock->on_writable) rearm_mask |= EV_WRITE;

                        struct io_uring_sqe *sqe = io_uring_get_sqe(&self->ring);
                        if (sqe) {
                            io_uring_prep_poll_add(sqe, sock->fd, map_events_to_poll(rearm_mask));
                            #ifdef IORING_POLL_ADD_MULTI
                            sqe->len |= IORING_POLL_ADD_MULTI;
                            #endif
                            io_uring_sqe_set_data(sqe, sock);
                            rearm_count++;
                        }
                    }
                }
            }
            RELEASE(obj);
        }
        count++;
    }
    io_uring_cq_advance(&self->ring, count);

    /* 3. 🚨 장전된 총알들을 커널로 일제히 전송! */
    if (rearm_count > 0) {
        io_uring_submit(&self->ring);
    }

    return count;
}

/* ============================================================================
 * [IO_URING 백엔드 구현부] (High Performance)
 * ============================================================================ */
static int uring_addSocket_impl(EventLoop* self, Socket* sock, EventMask mask) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&self->ring);
    if (!sqe) return -1;

    io_uring_prep_poll_add(sqe, sock->fd, map_events_to_poll(mask));
    #ifdef IORING_POLL_ADD_MULTI
      sqe->len |= IORING_POLL_ADD_MULTI;
    #endif
    io_uring_sqe_set_data(sqe, sock);

    if (self->tracked_objs[sock->fd] == NULL) {
        self->tracked_objs[sock->fd] = (Object*)sock;
        RETAIN((Object*)sock);
    }
    io_uring_submit(&self->ring);
    return 0;
}

static int uring_delSocket_impl(EventLoop* self, Socket* sock) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&self->ring);
    if (!sqe) return -1;

    io_uring_prep_poll_remove(sqe, sock);
    io_uring_submit(&self->ring);

    if (self->tracked_objs[sock->fd] != NULL) {
        self->tracked_objs[sock->fd] = NULL;
        RELEASE((Object*)sock);
    }
    return 0;
}

/* ============================================================================
 * [EPOLL 폴링 로직 복원] (Fallback 전용)
 * ============================================================================ */
static int epoll_poll_impl(EventLoop* self, int timeout_ms) {
    int nfds = epoll_wait(self->epoll_fd, self->event_buffer, self->max_events, timeout_ms);
    if (nfds < 0) return nfds;

    for (int i = 0; i < nfds; i++) {
        Object* obj = (Object*)self->event_buffer[i].data.ptr;
        RETAIN(obj);

        if (strcmp(obj->type->name, "Timer") == 0) {
            /* 타이머 로직 생략(호출) */
        } else {
            Socket* sock = (Socket*)obj;
            /* 🚀 복원 완료: 드디어 map_epoll_to_events가 다시 호출됩니다! (Warning 소멸) */
            EventMask triggered = map_epoll_to_events(self->event_buffer[i].events);

            if (sock->is_open && (triggered & EV_READ)  && sock->on_readable) sock->on_readable(sock, self);
            if (sock->is_open && (triggered & EV_WRITE) && sock->on_writable) sock->on_writable(sock, self);
            if (sock->is_open && (triggered & EV_ERROR) && sock->on_error)    sock->on_error(sock, self);
        }
        RELEASE(obj);
    }
    return nfds;
}

/* ============================================================================
 * 코어 루프 엔진 (The Heartbeat)
 * ============================================================================ */
void event_loop_run(EventLoop* self) {
    if (!self) return;

    LOG_I("[LOOP] Event loop active. (Backend: %s). Press ^C to stop.",
          self->backend == EL_BACKEND_URING ? "io_uring" : "epoll");

    while (self->is_running) {
        int ret = self->poll(self, 100);
        if (ret < 0 && (errno == EINTR || ret == -ETIME)) continue;
    }
    LOG_W("[LOOP] Event loop exit signal received.");
}

static void EventLoop_stop_impl(EventLoop* self) {
    if (self) self->is_running = false;
}

/* ============================================================================
 * 클래스 메타데이터 및 생성자
 * ============================================================================ */
static const Class _eventLoopClass = {
    .name     = "EventLoop",
    .size     = sizeof(EventLoop),
    .finalize = EventLoop_finalize
};

EventLoop* new_EventLoop(int max_events) {
    EventLoop* self = (EventLoop*)calloc(1, sizeof(EventLoop));
    if (!self) return NULL;

    Object_Init((Object*)self, &_eventLoopClass);

    self->is_running = true;
    self->max_events = (max_events > 0) ? max_events : 64;

    self->tracked_objs = (Object**)calloc(65536, sizeof(Object*));
    if (!self->tracked_objs) {
        /* 🚨 의장님 최후의 패치: ARC 생명주기에 맞춘 안전한 소각! */
        RELEASE(self);
        return NULL;
    }

    if (detect_uring()) {
        self->backend = EL_BACKEND_URING;

        int ret = io_uring_queue_init(self->max_events, &self->ring, 0);
        if (ret < 0) {
            RELEASE(self);
            return NULL;
        }

        self->addSocket = uring_addSocket_impl;
        self->delSocket = uring_delSocket_impl;
        self->poll      = uring_poll_impl;

        self->addTimer    = NULL;
        self->removeTimer = NULL;
    } else {
        self->backend = EL_BACKEND_EPOLL;

        self->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (self->epoll_fd < 0) {
            RELEASE(self);
            return NULL;
        }

        self->event_buffer = (struct epoll_event*)calloc(self->max_events, sizeof(struct epoll_event));
        if (!self->event_buffer) {
            RELEASE(self);
            return NULL;
        }

        self->addSocket = epoll_addSocket_impl;
        self->delSocket = epoll_delSocket_impl;
        self->poll      = epoll_poll_impl;

        self->addTimer    = NULL;
        self->removeTimer = NULL;
    }

    self->run  = event_loop_run;
    self->stop = EventLoop_stop_impl;

    return self;
}