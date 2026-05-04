/*
 * src/event_loop.c
 * ────────────────────────────────────────────────────────────────────────
 * [투스it홀딩스 제국 코어 엔진 - 클순 부장 초정밀 시력 보호 모드]
 * 1. 누수 원천 차단 : tracked_objs 명부를 통한 소멸자(Finalize) 일괄 해제 ✅
 * 2. 시력 보호 정렬 : 모든 연산자 및 변수 선언 수직 칼각 정렬 유지 ✅
 * 3. 댕글링 방어    : event_buffer 및 FD 해제 시 NULL/초기화 보장 ✅
 * ────────────────────────────────────────────────────────────────────────
 */

#define _GNU_SOURCE
#include "event_loop.h"
#include "logger.h"
#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

extern Logger* logger;
#define LOG_D(fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(logger,  fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) LOG_WARN(logger,  fmt, ##__VA_ARGS__)

/* ────────────────────────────────────────
 * 내부 유틸리티 (Internal Utilities)
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

/* ────────────────────────────────────────
 * 소멸자: 🚨 누수 완전 소각로 🚨
 * ──────────────────────────────────────── */
static void EventLoop_finalize(Object* obj) {
    EventLoop* self = (EventLoop*)obj;

    /* 🚨 1. 명부에 등록된 모든 수감자(소켓/타이머) 강제 석방 (참조 카운트 삭감) */
    for (int i = 0; i < 65536; i++) {
        if (self->tracked_objs[i] != NULL) {
            RELEASE(self->tracked_objs[i]);
            self->tracked_objs[i] = NULL;
        }
    }

    /* 🚨 2. 커널 자원(epoll) 및 버퍼 반환 */
    if (self->epoll_fd >= 0) close(self->epoll_fd);

    if (self->event_buffer) {
        free(self->event_buffer);
        self->event_buffer = NULL; /* Dangling Pointer 원천 봉쇄 */
    }
}

/* ────────────────────────────────────────
 * Socket 등록 / 해제 (수감자 명부 연동)
 * ──────────────────────────────────────── */
static int EventLoop_addSocket_impl(EventLoop* self, Socket* sock, EventMask mask) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    struct epoll_event ev = {
        .events   = map_events_to_epoll(mask),
        .data.ptr = sock
    };

    /* 🚨 명부에 등록하고 RETAIN (생명 주기 연장) */
    if (self->tracked_objs[sock->fd] == NULL) {
        self->tracked_objs[sock->fd] = (Object*)sock;
        RETAIN((Object*)sock);
    }

    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, sock->fd, &ev) == -1) {
        if (errno == EEXIST && epoll_ctl(self->epoll_fd, EPOLL_CTL_MOD, sock->fd, &ev) == 0) return 0;

        /* 🚨 등록 실패 시 명부에서 삭제하고 RELEASE */
        self->tracked_objs[sock->fd] = NULL;
        RELEASE((Object*)sock);
        return -1;
    }
    return 0;
}

static int EventLoop_delSocket_impl(EventLoop* self, Socket* sock) {
    if (!self || !sock || sock->fd < 0 || sock->fd >= 65536) return -1;

    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, sock->fd, NULL) == -1) return -1;

    /* 🚨 명부에서 삭제하고 RELEASE (생명 주기 반환) */
    if (self->tracked_objs[sock->fd] != NULL) {
        self->tracked_objs[sock->fd] = NULL;
        RELEASE((Object*)sock);
    }
    return 0;
}

/* ────────────────────────────────────────
 * Timer 등록 / 해제 (수감자 명부 연동)
 * ──────────────────────────────────────── */
int event_loop_add_timer(EventLoop* self, Timer* timer) {
    if (!self || !timer || timer->tfd < 0 || timer->tfd >= 65536) return -1;

    struct epoll_event ev = {
        .events   = EPOLLIN | EPOLLET,
        .data.ptr = timer
    };

    if (self->tracked_objs[timer->tfd] == NULL) {
        self->tracked_objs[timer->tfd] = (Object*)timer;
        RETAIN((Object*)timer);
    }

    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, timer->tfd, &ev) == -1) {
        self->tracked_objs[timer->tfd] = NULL;
        RELEASE((Object*)timer);
        return -1;
    }
    return 0;
}

int event_loop_remove_timer(EventLoop* self, Timer* timer) {
    if (!self || !timer || timer->tfd < 0 || timer->tfd >= 65536) return -1;

    epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, timer->tfd, NULL);

    if (self->tracked_objs[timer->tfd] != NULL) {
        self->tracked_objs[timer->tfd] = NULL;
        RELEASE((Object*)timer);
    }
    return 0;
}

/* ────────────────────────────────────────
 * 코어 루프 엔진 (The Heartbeat)
 * ──────────────────────────────────────── */
static int EventLoop_poll_impl(EventLoop* self, int timeout_ms) {
    if (!self || !self->is_running) return -1;

    int nfds = epoll_wait(self->epoll_fd, self->event_buffer, self->max_events, timeout_ms);
    if (nfds < 0) return nfds; /* -1 리턴 시 상위에서 EINTR 처리 */

    for (int i = 0; i < nfds; i++) {
        Object* obj = (Object*)self->event_buffer[i].data.ptr;

        /* 🚨 이벤트 처리 중 소멸 방지를 위한 임시 RETAIN */
        RETAIN(obj);

        if (strcmp(obj->type->name, "Timer") == 0) {
            on_timer_event((Timer*)obj);
        } else {
            Socket* sock      = (Socket*)obj;
            EventMask triggered = map_epoll_to_events(self->event_buffer[i].events);

            if (sock->is_open && (triggered & EV_READ)  && sock->on_readable) sock->on_readable(sock, self);
            if (sock->is_open && (triggered & EV_WRITE) && sock->on_writable) sock->on_writable(sock, self);
            if (sock->is_open && (triggered & EV_ERROR) && sock->on_error)    sock->on_error(sock, self);
        }

        /* 🚨 임시 RETAIN 해제 */
        RELEASE(obj);
    }
    return nfds;
}

void event_loop_run(EventLoop* self) {
    if (!self) return;

    LOG_I("[LOOP] Event loop active. Press ^C to stop.");

    while (self->is_running) {
        /* 🚨 poll이 시그널에 의해 깨어났을 때 즉시 루프 상단으로 올라가 조건 체크 */
        if (EventLoop_poll_impl(self, 100) < 0 && errno == EINTR) continue;
    }

    LOG_W("[LOOP] Event loop exit signal received.");
}

static void EventLoop_stop_impl(EventLoop* self) {
    if (self) self->is_running = false;
}

/* ────────────────────────────────────────
 * 클래스 메타데이터 및 생성자
 * ──────────────────────────────────────── */
static const Class _eventLoopClass = {
    .name     = "EventLoop",
    .size     = sizeof(EventLoop), /* 🚨 런타임 메모리 사이즈 주입 완료!! */
    .finalize = EventLoop_finalize
};

EventLoop* new_EventLoop(int max_events) {
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) return NULL;

    EventLoop* self = (EventLoop*)calloc(1, sizeof(EventLoop));
    if (!self) {
        close(epfd);
        return NULL;
    }

    Object_Init((Object*)self, &_eventLoopClass);

    /* 🚨 모든 멤버 변수 수직 칼각 초기화 */
    self->epoll_fd     = epfd;
    self->is_running   = true;
    self->max_events   = (max_events > 0) ? max_events : 64;
    self->event_buffer = (struct epoll_event*)calloc(self->max_events, sizeof(struct epoll_event));

    /* 🚨 메서드 포인터 수직 칼각 바인딩 */
    self->addSocket    = EventLoop_addSocket_impl;
    self->delSocket    = EventLoop_delSocket_impl;
    self->addTimer     = event_loop_add_timer;
    self->removeTimer  = event_loop_remove_timer;
    self->poll         = EventLoop_poll_impl;
    self->run          = event_loop_run;
    self->stop         = EventLoop_stop_impl;

    /* * calloc으로 할당했기 때문에
     * self->tracked_objs 배열은 모두 NULL로 깔끔하게 초기화되어 있음
     */

    return self;
}