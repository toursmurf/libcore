#include "event_loop_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

static uint64_t get_current_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

/* =========================================================
 * 공용 외부 API (Front API) 구현부
 * ========================================================= */
 /* 래퍼 함수 */
 static int _addSocket(EventLoop* self,
     Socket* sock, uint32_t mask) {
     return event_backend_add(self, sock, mask);
 }
 static int _delSocket(EventLoop* self,
     Socket* sock) {
     return event_backend_remove(self, sock);
 }
 static void _poll(EventLoop* self,
     int timeout_ms) {
     LibcoreEvent events[64];
     event_backend_wait(self, events, 64,
         timeout_ms);
 }
static void _stop(EventLoop* self) {
    if (self) self->running = false;
}

/* ── 지연 해제 — 인스턴스별 격리 (다중 Reactor 스레드 안전) ──
 * 전역 변수 사용 금지: 각 EventLoop 인스턴스가 자신의 impl->defer_* 큐 소유 */

static void _deferRelease(EventLoop* self, Object* obj) {
    if (!self || !self->impl || !obj) return;
    if (self->impl->defer_count < DEFER_CAP) {
        self->impl->defer_pending[self->impl->defer_count++] = obj;
    } else {
        /* 큐 가득 차면 즉시 해제 (Data Race보다 안전) */
        RELEASE(obj);
    }
}

static void flush_defer_queue(EventLoop* loop) {
    if (!loop || !loop->impl) return;
    for (int i = 0; i < loop->impl->defer_count; i++) {
        RELEASE(loop->impl->defer_pending[i]);
        loop->impl->defer_pending[i] = NULL;
    }
    loop->impl->defer_count = 0;
}

/* ── ARC 소멸자 ── */
static void EventLoop_finalize(Object* obj) {
    EventLoop* self = (EventLoop*)obj;
    event_backend_destroy(self);   /* impl->ctx_map + impl 해제 */
}

static const Class _EventLoop_Class = {
    .name     = "EventLoop",
    .size     = sizeof(EventLoop),
    .finalize = EventLoop_finalize
};

EventLoop* event_loop_create(void) {
    EventLoop* loop = calloc(1, sizeof(EventLoop));
    if (!loop) return NULL;

    /* ARC 체계 편입 — RELEASE() 로 해제 가능 */
    Object_Init((Object*)loop, &_EventLoop_Class);

    loop->running = 0;
    loop->thread_id = 0;
    loop->addSocket    = _addSocket;
    loop->delSocket    = _delSocket;
    loop->poll         = _poll;
    loop->stop         = _stop;
    loop->addTimer     = NULL;
    loop->removeTimer  = NULL;
    loop->deferRelease = _deferRelease;
    if (event_backend_init(loop) < 0) {
        RELEASE((Object*)loop);   /* finalize → event_backend_destroy */
        return NULL;
    }
    return loop;
}

/* event_loop_destroy: 하위 호환성 유지 — 내부적으로 RELEASE 위임 */
void event_loop_destroy(EventLoop* loop) {
    if (loop) RELEASE((Object*)loop);
}

void event_loop_stop(EventLoop* loop) {
    if (loop) {
        loop->running = 0;
    }
}

int event_loop_run(EventLoop* loop) {
    if (!loop) return -1;
    loop->running = 1;

    LibcoreEvent events[64];
    while (loop->running) {
        int n = event_backend_wait(loop, events, 64, 1000);
        if (n < 0) break;

        for (int i = 0; i < n; i++) {
            SocketContext* ctx = events[i].ctx;
            if (!ctx || !ctx->sock) continue;
            Socket* sock = ctx->sock;

            /* CLOSE/ERROR 는 on_readable 로 전달 (연결 종료 처리) */
            if (events[i].mask & (EVENT_CLOSE | EVENT_ERROR)) {
                if (sock->on_readable)
                    sock->on_readable(sock, loop);
                continue;
            }
            if ((events[i].mask & EVENT_READ) && sock->on_readable)
                sock->on_readable(sock, loop);
            if ((events[i].mask & EVENT_WRITE) && sock->on_writable)
                sock->on_writable(sock, loop);
        }
        /* 이벤트 배치 처리 완료 후 지연 해제 큐 플러시 — UAF 방지 */
        flush_defer_queue(loop);
    }
    return 0;
}


/* =========================================================
 * 🪟 Windows: IOCP Backend (Proactor)
 * ========================================================= */
#if defined(LIBCORE_USE_IOCP)

static void socket_context_try_destroy(SocketContext* ctx) {
    if (ctx && ctx->closing && ctx->pending_io == 0) {
        free(ctx);
    }
}

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->iocp_handle = NULL;
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
    ctx->sock = sock;

    HANDLE h = CreateIoCompletionPort((HANDLE)(uintptr_t)sock->fd,
                                      loop->impl->iocp_handle,
                                      (ULONG_PTR)ctx, 0);
    if (!h) {
        free(ctx);
        return -1;
    }

    loop->impl->ctx_map[sock->fd] = ctx;

    if (mask & EVENT_READ) {
        ctx->pending_io++;
    }
    return 0;
}

int event_backend_modify(EventLoop* loop, Socket* sock, uint32_t mask) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    if (mask & EVENT_WRITE) {
        ctx->pending_io++;
    }
    return 0;
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    ctx->closing = true;
    loop->impl->ctx_map[sock->fd] = NULL;
    socket_context_try_destroy(ctx);
    return 0;
}

int event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms) {
    if (!loop || !loop->impl || !events || max_events <= 0) return -1;

    DWORD bytes_transferred = 0;
    ULONG_PTR completion_key = 0;
    LPOVERLAPPED overlapped = NULL;

    BOOL res = GetQueuedCompletionStatus(loop->impl->iocp_handle,
                                         &bytes_transferred,
                                         &completion_key,
                                         &overlapped,
                                         timeout_ms);

    if (!res && overlapped == NULL) {
        return 0; /* 타임아웃 */
    }

    SocketContext* ctx = (SocketContext*)completion_key;
    if (ctx) {
        events[0].ctx = ctx;
        events[0].mask = 0;
        events[0].timestamp_ms = get_current_ms();
        events[0].transferred = (size_t)bytes_transferred;

        ctx->pending_io--;

        if (!res || bytes_transferred == 0) {
            events[0].mask |= (EVENT_CLOSE | EVENT_ERROR);
        } else {
            /* V1.x 단순화 모델: 읽기/쓰기 완료 시 기본 플래그 매핑 */
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
                free(loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        free(loop->impl);
        loop->impl = NULL;
    }
}


/* =========================================================
 * 🍎 macOS: kqueue Backend (Reactor)
 * ========================================================= */
#elif defined(LIBCORE_USE_KQUEUE)

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->kq_fd = -1;
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
    ctx->sock = sock;

    struct kevent ev[2];
    int n = 0;
    if (mask & EVENT_READ)  EV_SET(&ev[n++], sock->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, ctx);
    if (mask & EVENT_WRITE) EV_SET(&ev[n++], sock->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, ctx);

    if (n > 0 && kevent(loop->impl->kq_fd, ev, n, NULL, 0, NULL) == -1) {
        free(ctx);
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
    if (mask & EVENT_READ)  EV_SET(&ev[n++], sock->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, ctx);
    if (mask & EVENT_WRITE) EV_SET(&ev[n++], sock->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, ctx);

    return (n > 0) ? kevent(loop->impl->kq_fd, ev, n, NULL, 0, NULL) : 0;
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    struct kevent ev[2];
    EV_SET(&ev[0], sock->fd, EVFILT_READ, EV_DELETE, 0, 0, ctx);
    EV_SET(&ev[1], sock->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ctx);
    kevent(loop->impl->kq_fd, ev, 2, NULL, 0, NULL);

    loop->impl->ctx_map[sock->fd] = NULL;
    free(ctx);
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
                free(loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        free(loop->impl);
        loop->impl = NULL;
    }
}


/* =========================================================
 * 🐧 Linux: epoll Backend (Reactor)
 * ========================================================= */
#else

int event_backend_init(EventLoop* loop) {
    loop->impl = calloc(1, sizeof(struct EventLoopImpl));
    if (!loop->impl) goto fail;

    loop->impl->epoll_fd = -1;
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
    ctx->sock = sock;

    struct epoll_event ev = {0};
    if (mask & EVENT_READ) ev.events |= EPOLLIN;
    if (mask & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.data.ptr = ctx;

    if (epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_ADD, sock->fd, &ev) == -1) {
        free(ctx);
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
    return epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_MOD, sock->fd, &ev);
}

int event_backend_remove(EventLoop* loop, Socket* sock) {
    if (!loop || !loop->impl || !sock) return -1;
    if (sock->fd < 0 || sock->fd >= 65536) return -1;

    SocketContext* ctx = loop->impl->ctx_map[sock->fd];
    if (!ctx) return -1;

    epoll_ctl(loop->impl->epoll_fd, EPOLL_CTL_DEL, sock->fd, NULL);
    loop->impl->ctx_map[sock->fd] = NULL;

    free(ctx);
    return 0;
}

int event_backend_wait(EventLoop* loop, LibcoreEvent* events, int max_events, int timeout_ms) {
    if (!loop || !loop->impl || !events || max_events <= 0) return -1;

    struct epoll_event ep_events[max_events];
    int n = epoll_wait(loop->impl->epoll_fd, ep_events, max_events, timeout_ms);
    if (n < 0) return -1;

    uint64_t now = get_current_ms();

    for (int i = 0; i < n; i++) {
        events[i].ctx = (SocketContext*)ep_events[i].data.ptr;
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
                free(loop->impl->ctx_map[i]);
                loop->impl->ctx_map[i] = NULL;
            }
        }
        free(loop->impl);
        loop->impl = NULL;
    }
}

#endif