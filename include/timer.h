#ifndef TIMER_H
#define TIMER_H

#include "object.h"
#include <stdbool.h>

// [TimerCallback] 타이머가 울릴 때 실행될 로직 규격
typedef void (*TimerCallback)(void* user_data);

typedef struct Timer Timer;

struct Timer {
    Object      base;           // ARC 객체
    int         tfd;            // Linux 커널 timerfd
    bool        repeating;      // 반복 여부
    long        interval_ms;    // 실행 간격 (ms)
    TimerCallback callback;     // 실행될 로직
    void* user_data;      // [BORROWED] 콜백 인자
    bool        active;         // 현재 동작 상태
    char        name[64];       // 식별용 이름

    // [VTable] 인터페이스
    bool (*start)   (Timer* self);
    void (*stop)    (Timer* self);
    void (*reset)   (Timer* self);
    bool (*isActive)(Timer* self);
};

// 생성자
Timer* new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* user_data);
Timer* new_TimerNamed(const char* name, long interval_ms, bool repeating, TimerCallback cb, void* user_data);

// 시스템 연동용 (EventLoop 등에서 호출)
void on_timer_event(Timer* self);

#endif // TIMER_H