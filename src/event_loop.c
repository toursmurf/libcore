#include "event_loop_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

static uint64_t get_current_ms(void) {
#if defined(_WIN32) || defined(_WIN64)
    return GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)(ts.tv_sec) * 1000ULL + (uint64_t)(ts.tv_nsec) / 1000000ULL;
#endif
}

static void SocketContext_finalize(Object* obj) {
    (void)obj;
}
static const Class _SocketContext_Class = {
    .name = "SocketContext",
    .size = sizeof(SocketContext),
    .finalize = SocketContext_finalize
};

static int _addSocket(EventLoop* self, Socket* sock, uint32_t mask) {
    return event_backend_add(self, sock, mask);
}
static int _delSocket(EventLoop* self, Socket* sock) {
    return event_backend_remove(self, sock);
}
static void _poll(EventLoop* self, int timeout_ms) {
    LibcoreEvent events[64];
    event_backend_wait(self, events, 64, timeout_ms);
}
static void _stop(EventLoop* self) {
    if (self) self->running = false;
}

static int _addTimer(EventLoop* self, Timer* timer) {
    if (!self || !self->impl || !timer) return -1;
    if (timer->platform_data != NULL) return -1;
    if (self->impl->active_timer_count >= 1024) return -1;

    if (event_backend_add_timer(self, timer) == 0) {
        RETAIN((Object*)timer);
        self->impl->active_timers[self->impl->active_timer_count++] = timer;
        return 0;
    }
    return -1;
}

static int _removeTimer(EventLoop* self, Timer* timer) {
    if (!self || !self->impl || !timer) return -1;
    for (int i = 0; i < self->impl->active_timer_count; i++) {
        if (self->impl->active_timers[i] == timer) {
            if (timer->active) {
                timer->stop(timer);
                if (timer->isActive(timer)) return -1;
            }
            if (event_backend_remove_timer(self, timer) == 0) {
                timer->platform_data = NULL;
                self->impl->active_timers[i] = self->impl->active_timers[--self->impl->active_timer_count];
                RELEASE((Object*)timer);
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

static void _deferRelease(EventLoop* self, Object* obj) {
    if (!self || !self->impl || !obj) return;

    if (self->impl->defer_count >= self->impl->defer_capacity) {
        int new_cap = self->impl->defer_capacity == 0 ? 64 : self->impl->defer_capacity * 2;
        Object** new_arr = realloc(self->impl->defer_pending, new_cap * sizeof(Object*));
        if (!new_arr) return;
        self->impl->defer_pending = new_arr;
        self->impl->defer_capacity = new_cap;
    }
    self->impl->defer_pending[self->impl->defer_count++] = obj;
}

static void flush_defer_queue(EventLoop* loop) {
    if (!loop || !loop->impl) return;
    for (int i = 0; i < loop->impl->defer_count; i++) {
        if (loop->impl->defer_pending[i]) {
            RELEASE(loop->impl->defer_pending[i]);
            loop->impl->defer_pending[i] = NULL;
        }
    }
    loop->impl->defer_count = 0;
}

static void EventLoop_finalize(Object* obj) {
    EventLoop* self = (EventLoop*)obj;
    if (self->impl) {
        flush_defer_queue(self);

        for (int i = 0; i < self->impl->active_timer_count; i++) {
            Timer* t = self->impl->active_timers[i];
            if (t) {
                if (t->active) t->stop(t);
                event_backend_remove_timer(self, t);
                t->platform_data = NULL;
                RELEASE((Object*)t);
            }
        }
        self->impl->active_timer_count = 0;
    }
    event_backend_destroy(self);
}

static const Class _EventLoop_Class = {
    .name     = "EventLoop",
    .size     = sizeof(EventLoop),
    .finalize = EventLoop_finalize
};

EventLoop* event_loop_create(void) {
    EventLoop* loop = calloc(1, sizeof(EventLoop));
    if (!loop) return NULL;

    Object_Init((Object*)loop, &_EventLoop_Class);

    loop->running = 0;
    loop->thread_id = 0;
    loop->addSocket    = _addSocket;
    loop->delSocket    = _delSocket;
    loop->poll         = _poll;
    loop->stop         = _stop;
    loop->addTimer     = _addTimer;
    loop->removeTimer  = _removeTimer;
    loop->deferRelease = _deferRelease;

    if (event_backend_init(loop) < 0) {
        RELEASE((Object*)loop);
        return NULL;
    }
    return loop;
}

void event_loop_destroy(EventLoop* loop) {
    if (loop) RELEASE((Object*)loop);
}

void event_loop_stop(EventLoop* loop) {
    if (loop) loop->running = 0;
}

int event_loop_run(EventLoop* loop) {
    if (!loop) return -1;

    loop->running = 1;

    LibcoreEvent events[64];
    while (loop->running) {
        int n = event_backend_wait(loop, events, 64, 1000);

        if (n < 0) {
            #if !defined(_WIN32) && !defined(_WIN64)
            if (errno == EINTR) {
                if (!loop->running) {
                    break;
                }
                continue;
            }
            #endif
            loop->running = 0;
            return -1;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].is_timer && events[i].timer) {
                RETAIN((Object*)events[i].timer);
            }
        }

        for (int i = 0; i < n; i++) {
            if (events[i].is_timer) {
                Timer* timer = events[i].timer;
                if (timer) {
                    if (timer->platform_data == loop) {
                        on_timer_event(timer);
                    }
                    RELEASE((Object*)timer);
                }
                continue;
            }

            SocketContext* ctx = events[i].ctx;
            if (!ctx || !ctx->sock || ctx->sock->fd == -1) {
                continue;
            }

            Socket* sock = ctx->sock;

            if (events[i].mask & (EVENT_CLOSE | EVENT_ERROR)) {
                if (sock->on_readable) {
                    sock->on_readable(sock, loop);
                }
                continue;
            }
            if ((events[i].mask & EVENT_READ) && sock->on_readable) {
                sock->on_readable(sock, loop);
            }
            if (ctx->sock && ctx->sock->fd != -1 && (events[i].mask & EVENT_WRITE) && sock->on_writable) {
                sock->on_writable(sock, loop);
            }
        }
        flush_defer_queue(loop);
    }
    return 0;
}

/* =========================================================
 * 🪟 Windows: IOCP Backend
 * ========================================================= */
#if defined(LIBCORE_USE_IOCP)
int event_backend_add_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !timer) return -1;
    timer->platform_data = loop;
    return 0;
}
int event_backend_remove_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !timer) return -1;
    return 0;
}

static void socket_context_try_destroy(SocketContext* ctx) {
    if (ctx && ctx->closing && ctx->pending_io == 0) free(ctx);
}

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->defer_capacity = 64;
    loop->impl->defer_pending = calloc(loop->impl->defer_capacity, sizeof(Object*));
    if (!loop->impl->defer_pending) goto fail;

    loop->impl->iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!loop->impl->iocp_handle) goto fail;

    return 0;
fail:
    event_backend_destroy(loop);
    return -1;
}

int event_backend_add(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = calloc(1, sizeof(SocketContext));
    if (!ctx) return -1;
    Object_Init((Object*)ctx, &_SocketContext_Class);
    ctx->sock = sock;
    ctx->registered_mask = mask;
    HANDLE h = CreateIoCompletionPort((HANDLE)(uintptr_t)sock->fd, loop->impl->iocp_handle, (ULONG_PTR)ctx, 0);
    if (!h) { RELEASE((Object*)ctx); return -1; }
    loop->impl->ctx_map[sock->fd] = ctx;
    if (mask & EVENT_READ) ctx->pending_io++;
    return 0;
}

int event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;
    ctx->registered_mask = mask;
    if (mask & EVENT_WRITE) ctx->pending_io++;
    return 0;
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    ctx->closing = true;
    loop->impl->ctx_map[sock->fd] = NULL;
    ctx->sock = NULL;

    loop->deferRelease(loop, (Object*)ctx);
    return 0;
}

int event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms) {
    if (!loop || !loop->impl || !events || max_events <= 0) return -1;
    DWORD bytes_transferred = 0;
    ULONG_PTR completion_key = 0;
    LPOVERLAPPED overlapped = NULL;
    BOOL res = GetQueuedCompletionStatus(loop->impl->iocp_handle, &bytes_transferred, &completion_key, &overlapped, timeout_ms);
    if (!res && overlapped == NULL) return 0;
    SocketContext* ctx = (SocketContext*)completion_key;
    if (ctx) {
        if (ctx->is_timer) {
            events[0].is_timer = 1;
            events[0].timer = ctx->timer;
            events[0].ctx = NULL;
        } else {
            events[0].is_timer = 0;
            events[0].timer = NULL;
            events[0].ctx = ctx;
        }
        events[0].mask = 0;
        events[0].timestamp_ms = get_current_ms();
        events[0].transferred = (size_t)bytes_transferred;
        ctx->pending_io--;
        if (!res || bytes_transferred == 0) {
            events[0].mask |= (EVENT_CLOSE | EVENT_ERROR);
        } else {
            events[0].mask |= EVENT_READ;
        }
        socket_context_try_destroy(ctx);
        return 1;
    }
    return 0;
}

void event_backend_destroy(EventLoop* loop) {
    if (loop && loop->impl) {
        if (loop->impl->iocp_handle) CloseHandle(loop->impl->iocp_handle);
        for (int i = 0; i < 65536; i++) {
            if (loop->impl->ctx_map[i]) {
                RELEASE((Object*)loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        if (loop->impl->defer_pending) free(loop->impl->defer_pending);
        free(loop->impl);
        loop->impl = NULL;
    }
}

/* =========================================================
 * 🍎 macOS: kqueue Backend
 * ========================================================= */
#elif defined(LIBCORE_USE_KQUEUE)

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->kq_fd = -1;
    loop->impl->defer_capacity = 64;
    loop->impl->defer_pending = calloc(loop->impl->defer_capacity, sizeof(Object*));
    if (!loop->impl->defer_pending) goto fail;

    loop->impl->kq_fd = kqueue();
    if (loop->impl->kq_fd == -1) goto fail;
    return 0;
fail:
    event_backend_destroy(loop);
    return -1;
}

int event_backend_add(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    SocketContext* ctx = calloc(1, sizeof(SocketContext));
    if (!ctx) return -1;
    Object_Init((Object*)ctx, &_SocketContext_Class);

    ctx->sock = sock;
    ctx->registered_mask = mask;

    struct kevent ev[2];
    int n = 0;
    if (mask & EVENT_READ)  EV_SET(&ev[n++], sock->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, ctx);
    if (mask & EVENT_WRITE) EV_SET(&ev[n++], sock->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, ctx);

    if (n > 0 && kevent(loop->impl->kq_fd, ev, n, NULL, 0, NULL) == -1) {
        RELEASE((Object*)ctx);
        return -1;
    }
    loop->impl->ctx_map[sock->fd] = ctx;
    return 0;
}

int event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    struct kevent ev[2];
    int n = 0;
    if ((ctx->registered_mask & EVENT_READ) && !(mask & EVENT_READ)) {
        EV_SET(&ev[n++], sock->fd, EVFILT_READ, EV_DELETE, 0, 0, ctx);
    }
    if ((ctx->registered_mask & EVENT_WRITE) && !(mask & EVENT_WRITE)) {
        EV_SET(&ev[n++], sock->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ctx);
    }
    if (!(ctx->registered_mask & EVENT_READ) && (mask & EVENT_READ)) {
        EV_SET(&ev[n++], sock->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, ctx);
    }
    if (!(ctx->registered_mask & EVENT_WRITE) && (mask & EVENT_WRITE)) {
        EV_SET(&ev[n++], sock->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, ctx);
    }

    if (n > 0 && kevent(loop->impl->kq_fd, ev, n, NULL, 0, NULL) == -1) {
        return -1;
    }
    ctx->registered_mask = mask;
    return 0;
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    struct kevent ev[2];
    int n = 0;

    if (ctx->registered_mask & EVENT_READ) {
        EV_SET(&ev[n++], sock->fd, EVFILT_READ, EV_DELETE, 0, 0, ctx);
    }
    if (ctx->registered_mask & EVENT_WRITE) {
        EV_SET(&ev[n++], sock->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ctx);
    }

    if (n > 0 && kevent(loop->impl->kq_fd, ev, n, NULL, 0, NULL) == -1) {
        if (errno != ENOENT && errno != EBADF) {
            ctx->sock = NULL;
            return -1;
        }
    }

    loop->impl->ctx_map[sock->fd] = NULL;
    ctx->sock = NULL;

    loop->deferRelease(loop, (Object*)ctx);
    return 0;
}

int event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms) {
    if (!loop || !loop->impl || !events || max_events <= 0) return -1;

    struct kevent kq_events[max_events];
    struct timespec ts;
    struct timespec* pts = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        pts = &ts;
    }

    int n = kevent(loop->impl->kq_fd, NULL, 0, kq_events, max_events, pts);
    if (n < 0) return -1;

    uint64_t now = get_current_ms();

    for (int i = 0; i < n; i++) {
        if (kq_events[i].filter == EVFILT_TIMER) {
            events[i].is_timer = 1;
            events[i].timer = (Timer*)kq_events[i].udata;
            events[i].ctx = NULL;
            events[i].mask = 0;
            events[i].timestamp_ms = now;
            continue;
        }

        events[i].is_timer = 0;
        events[i].timer = NULL;
        events[i].ctx = (SocketContext*)kq_events[i].udata;
        events[i].mask = 0;
        events[i].timestamp_ms = now;

        if (kq_events[i].filter == EVFILT_READ)  events[i].mask |= EVENT_READ;
        if (kq_events[i].filter == EVFILT_WRITE) events[i].mask |= EVENT_WRITE;
        if (kq_events[i].flags & EV_EOF)         events[i].mask |= EVENT_CLOSE;
        if (kq_events[i].flags & EV_ERROR)       events[i].mask |= EVENT_ERROR;
    }
    return n;
}

void event_backend_destroy(EventLoop* loop) {
    if (loop && loop->impl) {
        if (loop->impl->kq_fd != -1) close(loop->impl->kq_fd);
        for (int i = 0; i < 65536; i++) {
            if (loop->impl->ctx_map[i]) {
                RELEASE((Object*)loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        if (loop->impl->defer_pending) free(loop->impl->defer_pending);
        free(loop->impl);
        loop->impl = NULL;
    }
}

int event_backend_add_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !loop->impl || !timer) return -1;
    timer->platform_data = loop;
    return 0;
}

int event_backend_remove_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !loop->impl || !timer) return -1;
    if (timer->active) {
        struct kevent ev;
        EV_SET(&ev, (uintptr_t)timer, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        if (kevent(loop->impl->kq_fd, &ev, 1, NULL, 0, NULL) == -1) {
            if (errno != ENOENT && errno != EBADF) return -1;
        }
    }
    return 0;
}

bool kqueue_update_timer(Timer* timer, bool active) {
    if (!timer) return false;
    EventLoop* loop = (EventLoop*)timer->platform_data;
    if (loop && loop->impl && loop->impl->kq_fd != -1) {
        struct kevent ev;
        if (active) {
            uint16_t flags = EV_ADD | EV_ENABLE;
            if (!timer->repeating) flags |= EV_ONESHOT;
            EV_SET(&ev, (uintptr_t)timer, EVFILT_TIMER, flags, 0, timer->interval_ms, timer);
        } else {
            EV_SET(&ev, (uintptr_t)timer, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        }
        return kevent(loop->impl->kq_fd, &ev, 1, NULL, 0, NULL) != -1;
    }
    return false;
}

/* =========================================================
 * 🚀 Linux: io_uring Backend (V6.2 ROCKY 준비 완료)
 * ========================================================= */
#elif defined(LIBCORE_USE_IOURING)

static int uring_flush(EventLoop* loop) {
    int ret = io_uring_submit(&loop->impl->ring);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

static int add_to_rearm_queue(EventLoop* loop, SocketContext* ctx) {
    if (loop->impl->rearm_count >= loop->impl->rearm_capacity) {
        int new_cap = loop->impl->rearm_capacity == 0 ? 64 : loop->impl->rearm_capacity * 2;
        SocketContext** new_arr = realloc(loop->impl->rearm_pending, new_cap * sizeof(SocketContext*));
        if (!new_arr) {
            errno = ENOMEM;
            return -1;
        }
        loop->impl->rearm_pending = new_arr;
        loop->impl->rearm_capacity = new_cap;
    }
    loop->impl->rearm_pending[loop->impl->rearm_count++] = ctx;
    return 0;
}

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->defer_capacity = 64;
    loop->impl->defer_pending = calloc(loop->impl->defer_capacity, sizeof(Object*));
    if (!loop->impl->defer_pending) goto fail;

    loop->impl->rearm_capacity = 64;
    loop->impl->rearm_pending = calloc(loop->impl->rearm_capacity, sizeof(SocketContext*));
    if (!loop->impl->rearm_pending) goto fail;

    if (io_uring_queue_init(1024, &loop->impl->ring, 0) < 0) {
        goto fail;
    }

    loop->impl->ring_initialized = true;
    loop->impl->uring_pending_total = 0;
    loop->impl->rearm_count = 0;
    return 0;
fail:
    event_backend_destroy(loop);
    return -1;
}

int event_backend_add(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    if (loop->impl->ctx_map[sock->fd] != NULL) {
        errno = EEXIST;
        return -1;
    }

    SocketContext* ctx = calloc(1, sizeof(SocketContext));
    if (!ctx) return -1;
    Object_Init((Object*)ctx, &_SocketContext_Class);

    ctx->sock = sock;
    ctx->registered_mask = mask;
    ctx->closing = false;
    ctx->pending_ops = 0;

    ctx->poll_armed = true;
    ctx->rearm_queued = false;

    ctx->poll_op.type = URING_OP_POLL_ADD;
    ctx->poll_op.data = ctx;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
    if (!sqe) {
        RELEASE((Object*)ctx);
        errno = EAGAIN;
        return -1;
    }

    short poll_mask = 0;
    if (mask & EVENT_READ)  poll_mask |= POLLIN;
    if (mask & EVENT_WRITE) poll_mask |= POLLOUT;

    io_uring_prep_poll_add(sqe, sock->fd, poll_mask);
    io_uring_sqe_set_data(sqe, &ctx->poll_op);

    RETAIN((Object*)ctx);
    ctx->pending_ops++;
    loop->impl->uring_pending_total++;

    io_uring_submit(&loop->impl->ring);

    loop->impl->ctx_map[sock->fd] = ctx;
    return 0;
}

int event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx || ctx->closing) return -1;

    if (!ctx->poll_armed) {
        ctx->registered_mask = mask;

        if (mask != 0 && !ctx->rearm_queued) {
            if (add_to_rearm_queue(loop, ctx) < 0) {
                return -1;
            }
            RETAIN((Object*)ctx);   /* rearm_queue ownership */
            ctx->rearm_queued = true;
        }
        return 0;
    }

    ctx->update_op.type = URING_OP_POLL_UPDATE;
    ctx->update_op.data = ctx;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
    if (!sqe) {
        io_uring_submit(&loop->impl->ring);
        sqe = io_uring_get_sqe(&loop->impl->ring);
        if (!sqe) {
            errno = EAGAIN;
            return -1;
        }
    }

    short poll_mask = 0;
    if (mask & EVENT_READ)  poll_mask |= POLLIN;
    if (mask & EVENT_WRITE) poll_mask |= POLLOUT;

    io_uring_prep_poll_update(sqe, (__u64)(uintptr_t)&ctx->poll_op, 0, poll_mask, IORING_POLL_UPDATE_EVENTS);
    io_uring_sqe_set_data(sqe, &ctx->update_op);

    RETAIN((Object*)ctx);
    ctx->pending_ops++;
    loop->impl->uring_pending_total++;

    io_uring_submit(&loop->impl->ring);
    ctx->registered_mask = mask;
    return 0;
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx || ctx->closing) return -1;

    if (ctx->poll_armed) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
        if (!sqe) {
            io_uring_submit(&loop->impl->ring);
            sqe = io_uring_get_sqe(&loop->impl->ring);
        }

        if (!sqe) {
            errno = EAGAIN;
            return -1;
        }

        ctx->remove_op.type = URING_OP_POLL_REMOVE;
        ctx->remove_op.data = ctx;

        io_uring_prep_poll_remove(sqe, (__u64)(uintptr_t)&ctx->poll_op);
        io_uring_sqe_set_data(sqe, &ctx->remove_op);

        RETAIN((Object*)ctx);
        ctx->pending_ops++;
        loop->impl->uring_pending_total++;

        io_uring_submit(&loop->impl->ring);
    }

    ctx->closing = true;
    loop->impl->ctx_map[sock->fd] = NULL;
    ctx->sock = NULL;

    /*
     * Drop ctx_map ownership only.
     * POLL_REMOVE owns a separate RETAIN until its CQE is consumed.
     * An outstanding POLL_ADD also owns its own RETAIN.
     */
    RELEASE((Object*)ctx);
    return 0;
}

int event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms) {
    if (!loop || !loop->impl || !events || max_events <= 0) return -1;

    int fatal_error = 0;

    if (uring_flush(loop) < 0) {
        return -1;
    }

    if (loop->impl->rearm_count > 0) {
        int r_count = loop->impl->rearm_count;
        loop->impl->rearm_count = 0;

        for (int i = 0; i < r_count; i++) {
            SocketContext* ctx = loop->impl->rearm_pending[i];
            ctx->rearm_queued = false;

            if (!ctx->closing && ctx->registered_mask != 0) {
                struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
                if (!sqe) {
                    io_uring_submit(&loop->impl->ring);
                    sqe = io_uring_get_sqe(&loop->impl->ring);
                }

                if (sqe) {
                    short poll_mask = 0;
                    if (ctx->registered_mask & EVENT_READ)  poll_mask |= POLLIN;
                    if (ctx->registered_mask & EVENT_WRITE) poll_mask |= POLLOUT;

                    io_uring_prep_poll_add(sqe, ctx->is_timer ? ctx->timer->tfd : ctx->sock->fd, poll_mask);
                    io_uring_sqe_set_data(sqe, &ctx->poll_op);

                    RETAIN((Object*)ctx); /* new POLL_ADD ownership */
                    ctx->pending_ops++;
                    loop->impl->uring_pending_total++;
                    ctx->poll_armed = true;
                    io_uring_submit(&loop->impl->ring);
                } else {
                    /*
                     * Requeue needs a new queue ownership because
                     * the old queue ownership is released below.
                     */
                    if (add_to_rearm_queue(loop, ctx) == 0) {
                        RETAIN((Object*)ctx);
                        ctx->rearm_queued = true;
                    } else if (!fatal_error) {
                        fatal_error = ENOMEM;
                    }
                }
            }
            /* release old rearm_queue ownership */
            RELEASE((Object*)ctx);
        }
    }

    struct io_uring_cqe *cqe;
    struct __kernel_timespec ts;
    struct __kernel_timespec *ts_ptr = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;
        ts_ptr = &ts;
    }

    int ret = io_uring_wait_cqe_timeout(&loop->impl->ring, &cqe, ts_ptr);
    if (ret < 0 && ret != -ETIME) {
        if (!fatal_error) {
            fatal_error = -ret;
        }
    }

    uint64_t now = get_current_ms();
    int consumed_cqes = 0;
    int event_count = 0;
    bool needs_submit = false;
    unsigned head;

    io_uring_for_each_cqe(&loop->impl->ring, head, cqe) {
        UringOp* op = (UringOp*)io_uring_cqe_get_data(cqe);
        if (op && op->type == URING_OP_POLL_ADD && event_count >= max_events) {
            break;
        }

        consumed_cqes++;
        loop->impl->uring_pending_total--;

        if (!op) continue;

        SocketContext* ctx = (SocketContext*)op->data;
        if (!ctx) continue;

        if (op->type == URING_OP_POLL_ADD) {
            ctx->poll_armed = false;

            if (cqe->res != -ECANCELED && !ctx->closing) {
                if (ctx->is_timer) {
                    events[event_count].is_timer = 1;
                    events[event_count].timer = ctx->timer;
                    events[event_count].ctx = NULL;
                } else {
                    events[event_count].is_timer = 0;
                    events[event_count].timer = NULL;
                    events[event_count].ctx = ctx;
                }
                events[event_count].mask = 0;
                events[event_count].timestamp_ms = now;

                if (cqe->res < 0) {
                    events[event_count].mask |= (EVENT_CLOSE | EVENT_ERROR);
                } else {
                    if (cqe->res & POLLIN)  events[event_count].mask |= EVENT_READ;
                    if (cqe->res & POLLOUT) events[event_count].mask |= EVENT_WRITE;
                    if (cqe->res & (POLLERR | POLLHUP | POLLNVAL)) {
                        events[event_count].mask |= (EVENT_CLOSE | EVENT_ERROR);
                    }
                }
                event_count++;
            }

            ctx->pending_ops--;

            if (!ctx->closing && ctx->registered_mask != 0 && !ctx->rearm_queued) {
                if (add_to_rearm_queue(loop, ctx) == 0) {
                    RETAIN((Object*)ctx);
                    ctx->rearm_queued = true;
                } else if (!fatal_error) {
                    fatal_error = ENOMEM;
                }
            }
            /* release completed POLL_ADD ownership */
            RELEASE((Object*)ctx);
        }
        else if (op->type == URING_OP_POLL_UPDATE || op->type == URING_OP_POLL_REMOVE) {
            ctx->pending_ops--;
            RELEASE((Object*)ctx);
        }
    }

    if (consumed_cqes > 0) {
        io_uring_cq_advance(&loop->impl->ring, consumed_cqes);
    }

    if (needs_submit) {
        io_uring_submit(&loop->impl->ring);
    }

    if (fatal_error) {
        errno = fatal_error;
        return -1;
    }

    return event_count;
}

void event_backend_destroy(EventLoop* loop) {
    if (loop && loop->impl) {
        if (loop->impl->ring_initialized) {

            /* 🚨 Phase 1: 모든 ctx를 CLOSING 상태로 확정 */
            for (int i = 0; i < 65536; i++) {
                SocketContext* ctx = loop->impl->ctx_map[i];
                if (ctx) ctx->closing = true;
            }

            /* 🚨 Phase 2: poll_armed 인 것만 POLL_REMOVE 제출 */
            for (int i = 0; i < 65536; i++) {
                SocketContext* ctx = loop->impl->ctx_map[i];
                if (ctx && ctx->poll_armed) {
                    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
                    if (!sqe) {
                        io_uring_submit(&loop->impl->ring);
                        sqe = io_uring_get_sqe(&loop->impl->ring);
                    }
                    if (sqe) {
                        ctx->remove_op.type = URING_OP_POLL_REMOVE;
                        ctx->remove_op.data = ctx;
                        io_uring_prep_poll_remove(sqe, (__u64)(uintptr_t)&ctx->poll_op);
                        io_uring_sqe_set_data(sqe, &ctx->remove_op);

                        RETAIN((Object*)ctx);
                        ctx->pending_ops++;
                        loop->impl->uring_pending_total++;
                    }
                }
            }
            io_uring_submit(&loop->impl->ring);

            /* 🚨 Phase 3: CQ drain */
            struct io_uring_cqe *cqe;
            struct __kernel_timespec ts = { .tv_sec = 0, .tv_nsec = 10000000LL }; /* 10ms wait max per iteration */

            uint64_t last_progress_ms = get_current_ms();

            while (loop->impl->uring_pending_total > 0) {
                int ret = io_uring_wait_cqe_timeout(&loop->impl->ring, &cqe, &ts);

                if (ret == 0) {
                    loop->impl->uring_pending_total--;
                    UringOp* op = (UringOp*)io_uring_cqe_get_data(cqe);
                    if (op && op->data) {
                        SocketContext* ctx = (SocketContext*)op->data;
                        ctx->pending_ops--;
                        if (op->type == URING_OP_POLL_ADD) ctx->poll_armed = false;
                        RELEASE((Object*)ctx);
                    }
                    io_uring_cqe_seen(&loop->impl->ring, cqe);
                    last_progress_ms = get_current_ms();
                    continue;
                }

                if (ret == -ETIME || ret == -EINTR) {
                    if (get_current_ms() - last_progress_ms >= 1000ULL) {
                        fprintf(stderr, "[io_uring] destroy drain stalled: pending=%d\n", loop->impl->uring_pending_total);
                        break;
                    }
                    continue;
                }

                fprintf(stderr, "[io_uring] destroy drain error: %d, pending=%d\n", -ret, loop->impl->uring_pending_total);
                break;
            }
            io_uring_queue_exit(&loop->impl->ring);
        }

        /* 🚨 Phase 4: rearm_queue refs */
        for (int i = 0; i < loop->impl->rearm_count; i++) {
            RELEASE((Object*)loop->impl->rearm_pending[i]);
        }
        if (loop->impl->rearm_pending) free(loop->impl->rearm_pending);

        /* 🚨 Phase 5: ctx_map refs
         * CQ drain above releases operation ownership (POLL_ADD / POLL_UPDATE / POLL_REMOVE).
         * ctx_map still owns the original context reference, so release that independent ownership here.
         */
        for (int i = 0; i < 65536; i++) {
            if (loop->impl->ctx_map[i]) {
                RELEASE((Object*)loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        if (loop->impl->defer_pending) free(loop->impl->defer_pending);
        free(loop->impl);
        loop->impl = NULL;
    }
}

int event_backend_add_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !loop->impl || !timer) return -1;
    int fd = timer->tfd;
    if (fd < 0 || fd >= 65536) return -1;

    if (loop->impl->ctx_map[fd] != NULL) {
        errno = EEXIST;
        return -1;
    }

    SocketContext* ctx = calloc(1, sizeof(SocketContext));
    if (!ctx) return -1;
    Object_Init((Object*)ctx, &_SocketContext_Class);

    ctx->is_timer = 1;
    ctx->timer = timer;
    ctx->registered_mask = EVENT_READ;
    ctx->closing = false;
    ctx->pending_ops = 0;

    ctx->poll_armed = true;
    ctx->rearm_queued = false;

    ctx->poll_op.type = URING_OP_POLL_ADD;
    ctx->poll_op.data = ctx;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
    if (!sqe) {
        RELEASE((Object*)ctx);
        errno = EAGAIN;
        return -1;
    }

    io_uring_prep_poll_add(sqe, fd, POLLIN);
    io_uring_sqe_set_data(sqe, &ctx->poll_op);

    RETAIN((Object*)ctx);
    ctx->pending_ops++;
    loop->impl->uring_pending_total++;

    io_uring_submit(&loop->impl->ring);

    loop->impl->ctx_map[fd] = ctx;
    timer->platform_data = loop;
    return 0;
}

int event_backend_remove_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !loop->impl || !timer) return -1;
    int fd = timer->tfd;
    if (fd < 0 || fd >= 65536) return -1;

    SocketContext* ctx = loop->impl->ctx_map[fd];
    if (!ctx || ctx->closing) return -1;

    if (ctx->poll_armed) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&loop->impl->ring);
        if (!sqe) {
            io_uring_submit(&loop->impl->ring);
            sqe = io_uring_get_sqe(&loop->impl->ring);
        }

        if (!sqe) {
            errno = EAGAIN;
            return -1;
        }

        ctx->remove_op.type = URING_OP_POLL_REMOVE;
        ctx->remove_op.data = ctx;

        io_uring_prep_poll_remove(sqe, (__u64)(uintptr_t)&ctx->poll_op);
        io_uring_sqe_set_data(sqe, &ctx->remove_op);

        RETAIN((Object*)ctx);
        ctx->pending_ops++;
        loop->impl->uring_pending_total++;

        io_uring_submit(&loop->impl->ring);
    }

    ctx->closing = true;
    loop->impl->ctx_map[fd] = NULL;

    /*
     * Drop ctx_map ownership only.
     * Outstanding io_uring operations keep their own references.
     */
    RELEASE((Object*)ctx);
    return 0;
}

/* =========================================================
 * 🐧 Linux: epoll Backend
 * ========================================================= */
#else

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->epoll_fd = -1;
    loop->impl->defer_capacity = 64;
    loop->impl->defer_pending = calloc(loop->impl->defer_capacity, sizeof(Object*));
    if (!loop->impl->defer_pending) goto fail;

    loop->impl->epoll_fd = epoll_create1(0);
    if (loop->impl->epoll_fd == -1) goto fail;
    return 0;
fail:
    event_backend_destroy(loop);
    return -1;
}

int event_backend_add(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    SocketContext* ctx = calloc(1, sizeof(SocketContext));
    if (!ctx) return -1;
    Object_Init((Object*)ctx, &_SocketContext_Class);

    ctx->sock = sock;
    ctx->registered_mask = mask;

    struct epoll_event ev = {0};
    if (mask & EVENT_READ) ev.events |= EPOLLIN;
    if (mask & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.data.ptr = ctx;

    if (epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_ADD, sock->fd, &ev) == -1) {
        RELEASE((Object*)ctx);
        return -1;
    }
    loop->impl->ctx_map[sock->fd] = ctx;
    return 0;
}

int event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    struct epoll_event ev = {0};
    if (mask & EVENT_READ) ev.events |= EPOLLIN;
    if (mask & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.data.ptr = ctx;

    if (epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_MOD, sock->fd, &ev) == -1) {
        return -1;
    }
    ctx->registered_mask = mask;
    return 0;
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;
    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    if (epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_DEL, sock->fd, NULL) == -1) {
        if (errno != ENOENT && errno != EBADF) {
            ctx->sock = NULL;
            return -1;
        }
    }

    loop->impl->ctx_map[sock->fd] = NULL;
    ctx->sock = NULL;

    loop->deferRelease(loop, (Object*)ctx);
    return 0;
}

int event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms) {
    if (!loop || !loop->impl || !events || max_events <= 0) return -1;

    struct epoll_event ep_events[max_events];
    int n = epoll_wait(loop->impl->epoll_fd, ep_events, max_events, timeout_ms);
    if (n < 0) return -1;

    uint64_t now = get_current_ms();

    for (int i = 0; i < n; i++) {
        SocketContext* ctx = (SocketContext*)ep_events[i].data.ptr;
        if (ctx && ctx->is_timer) {
            events[i].is_timer = 1;
            events[i].timer = ctx->timer;
            events[i].ctx = NULL;
            events[i].mask = 0;
            events[i].timestamp_ms = now;
            continue;
        }

        events[i].is_timer = 0;
        events[i].timer = NULL;
        events[i].ctx = ctx;
        events[i].mask = 0;
        events[i].timestamp_ms = now;

        if (ep_events[i].events & EPOLLIN) events[i].mask |= EVENT_READ;
        if (ep_events[i].events & EPOLLOUT) events[i].mask |= EVENT_WRITE;
        if (ep_events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            events[i].mask |= (EVENT_CLOSE | EVENT_ERROR);
        }
    }
    return n;
}

void event_backend_destroy(EventLoop* loop) {
    if (loop && loop->impl) {
        if (loop->impl->epoll_fd != -1) close(loop->impl->epoll_fd);
        for (int i = 0; i < 65536; i++) {
            if (loop->impl->ctx_map[i]) {
                RELEASE((Object*)loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        if (loop->impl->defer_pending) free(loop->impl->defer_pending);
        free(loop->impl);
        loop->impl = NULL;
    }
}

int event_backend_add_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !loop->impl || !timer) return -1;
    int fd = timer->tfd;
    if (fd < 0 || fd >= 65536) return -1;

    SocketContext* ctx = calloc(1, sizeof(SocketContext));
    if (!ctx) return -1;
    Object_Init((Object*)ctx, &_SocketContext_Class);

    ctx->is_timer = 1;
    ctx->timer = timer;

    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.ptr = ctx;

    if (epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        RELEASE((Object*)ctx);
        return -1;
    }
    loop->impl->ctx_map[fd] = ctx;
    timer->platform_data = loop;
    return 0;
}

int event_backend_remove_timer(EventLoop* loop, Timer* timer) {
    if (!loop || !loop->impl || !timer) return -1;
    int fd = timer->tfd;
    if (fd < 0 || fd >= 65536) return -1;

    SocketContext* ctx = loop->impl->ctx_map[fd];
    if (!ctx) return -1;

    if (epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        if (errno != ENOENT && errno != EBADF) return -1;
    }

    loop->impl->ctx_map[fd] = NULL;
    RELEASE((Object*)ctx);
    return 0;
}

#endif