#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "timer.h"
#include "socket_base.h"

/* EV_* 상수 — 예제 호환용 */
#define EV_READ   0x01u
#define EV_WRITE  0x02u

/* 리눅스 환경에서만 epoll.h를 포함 (맥/윈도우 에러 원천 차단) */
#if defined(__linux__) || defined(__gnu_linux__)
    #include <sys/epoll.h>
#endif

/* 내부 구현체 은닉용 전방 선언 (Opaque Pointer) */
struct EventLoopImpl;

/* 외부로 노출되는 EventLoop 구조체 */
typedef struct EventLoop {
    int running;       /* 루프 실행 상태 플래그 */
    int thread_id;     /* 루프가 구동 중인 스레드 ID */

    /* OS별 내부 심장(epoll/kqueue/IOCP)과 연결되는 비밀 통로 */
    struct EventLoopImpl* impl;
		/* 편의 래퍼 API */
		int  (*addTimer)    (struct EventLoop* self, Timer* timer);
    int  (*removeTimer) (struct EventLoop* self, Timer* timer);
    int  (*addSocket)(struct EventLoop* self, Socket* sock, uint32_t mask);
    int  (*delSocket)(struct EventLoop* self, Socket* sock);
    void (*poll)(struct EventLoop* self, int timeout_ms);
    void (*stop)        (struct EventLoop* self);
    void (*deferRelease)(struct EventLoop* self, Object* obj); /* 순회 문맥 안전 지연 해제 */

} EventLoop;

/* 공용 API 프로토타입 (외부 노출용) */
EventLoop* event_loop_create(void);
void       event_loop_destroy(EventLoop* loop);
int        event_loop_run(EventLoop* loop);
void       event_loop_stop(EventLoop* loop);

#endif /* EVENT_LOOP_H */