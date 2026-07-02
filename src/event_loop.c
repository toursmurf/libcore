#define _GNU_SOURCE
#include "event_loop.h"
#include "logger.h"
#include <sys/epoll.h>
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
 * [0] 지연 소각 (Deferred Release) 구현
 * ──────────────────────────────────────── */
static void impl_deferRelease(EventLoop* self, Object* obj) {
    if (!self || !obj) return;

    /* 🚨 [결함 1 패치] 소유권 수지 완벽 일치!
      */
    self->deferred_cleanup_list->add(self->deferred_cleanup_list, obj);
    RELEASE(obj);
}

/* ────────────────────────────────────────
 * [1] io_uring 전용 로직 (HAS_LIBURING 활성화 시)
 * ──────────────────────────────────────── */
#ifdef HAS_LIBURING

static bool detect_uring(void) {
    struct io_uring ring;
    int ret = io_uring_queue_init(1, &ring, 0);
    if (ret == 0) {
        io_uring_queue_exit(&ring);
        return true;
    }
    return false;
}

static uint32_t map_events_to_poll(EventMask mask) {
    uint32_t events = 0;
    if (mask & EV_READ)  events |= POLLIN;
    if (mask & EV_WRITE) events |= POLLOUT;
    events |= (POLLERR | POLLHUP);
    return events;
}

static EventMask map_poll_to_events(uint32_t events) {
    EventMask mask = 0;
    if (events & POLLIN)               mask |= EV_READ;
    if (events & POLLOUT)              mask |= EV_WRITE;
    if (events & (POLLERR | POLLHUP))  mask |= EV_ERROR;
    return mask;
}

static int uring_poll_impl(EventLoop* self, int timeout_ms) {
    struct io_uring_cqe *cqe;
    unsigned head;
    int count = 0;
    int rearm_count = 0;

    struct __kernel_timespec ts = {
        .tv_sec  = timeout_ms / 1000,
        .tv_nsec = (timeout_ms % 1000) * 1000000
    };

    int ret = io_uring_wait_cqe_timeout(&self->ring, &cqe, &ts);
    if (ret < 0) return ret;

    io_uring_for_each_cqe(&self->ring, head, cqe) {
        uint64_t user_data = cqe->user_data;
        uint32_t fd = (uint32_t)(user_data & 0xFFFFFFFF);
        uint32_t gen = (uint32_t)(user_data >> 32);

        if (fd < 65536) {
            PollContext* ctx = &self->ctx_pool[fd];

            if (ctx->sock != NULL && ctx->generation == gen) {
                Socket* sock = ctx->sock;
                RETAIN((Object*)sock);

                EventMask triggered = map_poll_to_events(cqe->res);

                if (sock->is_open && (triggered & EV_READ)  && sock->on_readable) {
                    sock->on_readable(sock, self);
                }
                if (sock->is_open && (triggered & EV_WRITE) && sock->on_writable) {
                    sock->on_writable(sock, self);
                }
                if (sock->is_open && (triggered & EV_ERROR) && sock->on_error) {
                    sock->on_error(sock, self);
                }

                if (sock->is_open && self->ctx_pool[fd].sock == sock) {
                    bool needs_rearm = true;
                    #ifdef IORING_CQE_F_MORE
                    if (cqe->flags & IORING_CQE_F_MORE) needs_rearm = false;
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
                            io_uring_sqe_set_data64(sqe, user_data);
                            rearm_count++;
                        }
                    }
                }
                RELEASE((Object*)sock);
            }
        }
        count++;
    }

    io_uring_cq_advance(&self->ring, count);

    if (rearm_count > 0) {
        io_uring_submit(&self->ring);
    }
    return count;
}

static int uring_addSocket_impl(EventLoop* self, Socket* sock, EventMask mask) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&self->ring);
    if (!sqe) return -1;

    PollContext* ctx = &self->ctx_pool[sock->fd];
    if (ctx->sock == NULL) {
        ctx->sock = sock;
        ctx->fd = sock->fd;
        RETAIN((Object*)ctx->sock);
    }

    io_uring_prep_poll_add(sqe, sock->fd, map_events_to_poll(mask));
    #ifdef IORING_POLL_ADD_MULTI
    sqe->len |= IORING_POLL_ADD_MULTI;
    #endif

    uint64_t user_data = ((uint64_t)ctx->generation << 32) | (uint64_t)sock->fd;
    io_uring_sqe_set_data64(sqe, user_data);

    io_uring_submit(&self->ring);
    return 0;
}

static int uring_delSocket_impl(EventLoop* self, Socket* sock) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&self->ring);
    if (!sqe) return -1;

    io_uring_prep_poll_remove(sqe, sock);
    io_uring_submit(&self->ring);

    PollContext* ctx = &self->ctx_pool[sock->fd];
    if (ctx->sock == sock) {
        ctx->sock = NULL;
        ctx->generation++;
        RELEASE((Object*)sock);
    }
    return 0;
}

#endif /* HAS_LIBURING */


/* ────────────────────────────────────────
 * [2] epoll 전용 로직 및 공통 유틸리티
 * ──────────────────────────────────────── */

static uint32_t map_events_to_epoll(EventMask mask) {
    uint32_t events = 0;
    if (mask & EV_READ)  events |= EPOLLIN;
    if (mask & EV_WRITE) events |= EPOLLOUT;
    events |= (EPOLLERR | EPOLLHUP | EPOLLET);
    return events;
}

static EventMask map_epoll_to_events(uint32_t events) {
    EventMask mask = 0;
    if (events & EPOLLIN)               mask |= EV_READ;
    if (events & EPOLLOUT)              mask |= EV_WRITE;
    if (events & (EPOLLERR | EPOLLHUP)) mask |= EV_ERROR;
    return mask;
}

static void EventLoop_finalize(Object* obj) {
    EventLoop* self = (EventLoop*)obj;

    if (self->deferred_cleanup_list) {
        RELEASE(self->deferred_cleanup_list);
    }

    if (self->ctx_pool) {
        for (int i = 0; i < 65536; i++) {
            if (self->ctx_pool[i].sock != NULL) {
                RELEASE((Object*)self->ctx_pool[i].sock);
                self->ctx_pool[i].sock = NULL;
            }
        }
        free(self->ctx_pool);
        self->ctx_pool = NULL;
    }

#ifdef HAS_LIBURING
    if (self->backend == EL_BACKEND_URING) {
        io_uring_queue_exit(&self->ring);
    } else
#endif
    {
        if (self->epoll_fd >= 0) close(self->epoll_fd);
    }

    if (self->event_buffer) {
        free(self->event_buffer);
        self->event_buffer = NULL;
    }
}

static int epoll_addSocket_impl(EventLoop* self, Socket* sock, EventMask mask) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    PollContext* ctx = &self->ctx_pool[sock->fd];

    /* 🚨 [결함 3 패치] 이번 호출에서 내가 RETAIN 한 것인지를 추적! */
    bool retained_now = false;

    if (ctx->sock == NULL) {
        ctx->sock = sock;
        ctx->fd = sock->fd;
        RETAIN((Object*)ctx->sock);
        retained_now = true;
    }

    struct epoll_event ev;
    ev.events = map_events_to_epoll(mask);
    ev.data.u64 = ((uint64_t)ctx->generation << 32) | (uint64_t)sock->fd;

    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, sock->fd, &ev) == -1) {
        if (errno == EEXIST) {
            if (epoll_ctl(self->epoll_fd, EPOLL_CTL_MOD, sock->fd, &ev) == 0) return 0;
        }

        /* 🚨 [결함 3 패치] 내가 RETAIN 한 경우에만 정확히 롤백! */
        if (retained_now) {
            ctx->sock = NULL;
            ctx->generation++; /* Stale Event 통과 차단 */
            RELEASE((Object*)sock);
        }
        return -1;
    }
    return 0;
}

static int epoll_delSocket_impl(EventLoop* self, Socket* sock) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, sock->fd, NULL);

    PollContext* ctx = &self->ctx_pool[sock->fd];
    if (ctx->sock == sock) {
        ctx->sock = NULL;
        ctx->generation++;
        RELEASE((Object*)sock);
    }
    return 0;
}

static int epoll_poll_impl(EventLoop* self, int timeout_ms) {
    int nfds = epoll_wait(self->epoll_fd, self->event_buffer, self->max_events, timeout_ms);
    if (nfds < 0) return nfds;

    for (int i = 0; i < nfds; i++) {
        uint64_t ud = self->event_buffer[i].data.u64;
        uint32_t fd = (uint32_t)(ud & 0xFFFFFFFF);
        uint32_t gen = (uint32_t)(ud >> 32);

        if (fd < 65536) {
            PollContext* ctx = &self->ctx_pool[fd];

            if (ctx->sock != NULL && ctx->generation == gen) {
                Socket* sock = ctx->sock;
                RETAIN((Object*)sock);

                EventMask triggered = map_epoll_to_events(self->event_buffer[i].events);

                if (sock->is_open && (triggered & EV_READ)  && sock->on_readable) {
                    sock->on_readable(sock, self);
                }
                if (sock->is_open && (triggered & EV_WRITE) && sock->on_writable) {
                    sock->on_writable(sock, self);
                }
                if (sock->is_open && (triggered & EV_ERROR) && sock->on_error) {
                    sock->on_error(sock, self);
                }

                RELEASE((Object*)sock);
            }
        }
    }
    return nfds;
}

void event_loop_run(EventLoop* self) {
    if (!self) return;

    LOG_I("[LOOP] Event loop active. (Backend: %s). Press ^C to stop.",
          self->backend == EL_BACKEND_URING ? "io_uring" : "epoll");

    while (self->is_running) {
        int ret = self->poll(self, 100);
        if (ret < 0 && (errno == EINTR || ret == -ETIME)) continue;

        /* 🚨 [결함 2 패치] 불필요한 for 데드코드 삭제! clear()가 전부 처리함! */
        self->deferred_cleanup_list->clear(self->deferred_cleanup_list);
    }
    LOG_W("[LOOP] Event loop exit signal received.");
}

static void EventLoop_stop_impl(EventLoop* self) {
    if (self) self->is_running = false;
}

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

    self->ctx_pool = (PollContext*)calloc(65536, sizeof(PollContext));
    self->deferred_cleanup_list = new_ArrayList(64);

    if (!self->ctx_pool || !self->deferred_cleanup_list) {
        RELEASE(self);
        return NULL;
    }

#ifdef HAS_LIBURING
    if (detect_uring()) {
        self->backend = EL_BACKEND_URING;
        int ret = io_uring_queue_init(self->max_events, &self->ring, 0);
        if (ret < 0) {
            RELEASE(self);
            return NULL;
        }
        self->addSocket   = uring_addSocket_impl;
        self->delSocket   = uring_delSocket_impl;
        self->poll        = uring_poll_impl;
        self->addTimer    = NULL;
        self->removeTimer = NULL;
    } else
#endif
    {
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
        self->addSocket   = epoll_addSocket_impl;
        self->delSocket   = epoll_delSocket_impl;
        self->poll        = epoll_poll_impl;
        self->addTimer    = NULL;
        self->removeTimer = NULL;
    }

    self->deferRelease = impl_deferRelease;
    self->run  = event_loop_run;
    self->stop = EventLoop_stop_impl;

    return self;
}