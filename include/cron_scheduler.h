#ifndef CRON_SCHEDULER_H
#define CRON_SCHEDULER_H

#include "scheduler.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

/*
 * [CronScheduler 표현식 및 수명주기 계약]
 * 1. 표현식 형식 (정확히 6개 필드):
 *    "<sec:0-59> <min:0-59> <hour:0-23> <day:1-31> <month:1-12> <weekday:0-6>"
 *    - 필드 구분 공백으로 스페이스(' '), 탭('\t'), 개행('\r', '\n')을 허용합니다.
 *      (줄 끝 LF/CRLF뿐만 아니라 문자열 중간의 개행도 필드 구분자로 처리됩니다.)
 *    - 각 필드는 쉼표(,)로 구분된 1개 이상의 비어 있지 않은 항목으로 구성됩니다.
 *    - 허용 문법: 전체(*), 전체 스텝(별표 뒤 /step), 단일(N), 범위(N-M), 범위 스텝(N-M/step)
 *      (step >= 1, min <= N <= M <= max, 순수 ASCII 숫자만 허용)
 *    - 음수, 범위 초과, step <= 0, 비숫자, 빈 토큰(1,,2), 6개 외 필드 수,
 *      빈 비트맵(0), 단일 값 스텝(N/step)은 모두 거부되며 addCron()은 false를 반환합니다.
 *    - 기존 계약 유지: day(일)와 weekday(요일)는 AND 조건으로 결합됩니다.
 *
 * 2. 콜백 실행 및 ud 수명 계약:
 *    - 매칭된 콜백은 실행 시마다 detached 스레드(PTHREAD_CREATE_DETACHED)에서 호출됩니다.
 *    - stop()은 내부 스케줄러 워커 스레드만 종료 대기(pthread_join)하며,
 *      이미 실행된 detached 콜백 스레드의 완료는 대기하지 않습니다.
 *    - 호출자는 실행 중일 수 있는 detached 콜백이 모두 종료될 때까지
 *      ud 및 콜백이 참조하는 자원의 수명을 외부 동기화 등으로 보장해야 합니다.
 *
 * 3. 인스턴스 수명주기 직렬화 계약 (ABI 보존):
 *    - 동일 CronScheduler 인스턴스에 대한 start(), stop(), RELEASE(finalize) 호출은
 *      호출자 측에서 외부 직렬화해야 합니다.
 *    - stop()이 완료되기 전에 다른 스레드에서 start(), stop(), RELEASE()를
 *      동시 호출하지 마십시오.
 *    - addCron()/removeCron()은 작업 목록 접근에 대해 내부 동기화되지만,
 *      객체의 수명까지 보장하지 않습니다.
 *    - finalize()와 모든 인스턴스 메서드의 동시 호출은 금지합니다.
 */

typedef struct CronJob CronJob;
typedef struct CronScheduler CronScheduler;

struct CronJob {
    char     name[64];
    uint64_t seconds;
    uint64_t minutes;
    uint32_t hours;
    uint32_t days;
    uint16_t months;
    uint8_t  weekdays;
    TimerCallback cb;
    void* ud;
};

struct CronScheduler {
    Scheduler       base;
    CronJob*        jobs;
    int             job_count;
    int             job_capacity;
    pthread_t       cron_worker;
    volatile bool   running;
    pthread_mutex_t lock;

    bool (*addCron)   (CronScheduler* self, const char* name, const char* expr, TimerCallback cb, void* ud);
    bool (*removeCron)(CronScheduler* self, const char* name);
    void (*start)     (CronScheduler* self);
    void (*stop)      (CronScheduler* self);
};

CronScheduler* new_CronScheduler(void);

#endif // CRON_SCHEDULER_H