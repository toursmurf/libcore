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

        //백엔드 대기 실패 시 루프를 종료하고 에러코드(-1)를 반환하여 호출자가 알 수 있게 전파
        if (n < 0) {
            #if !defined(_WIN32) && !defined(_WIN64)
            if (errno == EINTR) {
                if (!loop->running) {
                    break;      /* 정상 stop */
                }
                continue;       /* 다른 신호면 다시 wait */
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
 * 🪟 Windows: IOCP Backend (Proactor / Timer is Threaded Fallback)
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

    // 할당 체크 위치 최상단 이동
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
 * 🍎 macOS: kqueue Backend (Reactor + Native Timer)
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
 * 🐧 Linux: epoll Backend (Reactor + Native Timer)
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