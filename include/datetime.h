#ifndef DATETIME_H
#define DATETIME_H

#include "object.h"
#include "string_obj.h"
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>

typedef struct DateTime DateTime;

/* [ARCHITECTURE] 외부에는 오직 VTable(함수 포인터)만 공개하여 물리적 불변성을 강제합니다. */
struct DateTime {
    Object base;

    int (*getYear)(DateTime* self);
    int (*getMonth)(DateTime* self);
    int (*getDay)(DateTime* self);
    int (*getHour)(DateTime* self);
    int (*getMinute)(DateTime* self);
    int (*getSecond)(DateTime* self);

    // [OWNED] 반환된 새 DateTime 객체는 불변 객체이며 호출자가 RELEASE 해야 합니다.
    DateTime* (*addDays)(DateTime* self, int days);
    long (*diffSeconds)(DateTime* self, DateTime* other);

    bool (*isLeapYear)(DateTime* self);
    int (*daysInMonth)(DateTime* self);

    // [OWNED] 원본 불변성을 유지하며 새 속성이 바뀐 객체를 반환합니다.
    DateTime* (*withYear)(DateTime* self, int year);
    DateTime* (*withMonth)(DateTime* self, int month);
    DateTime* (*withDay)(DateTime* self, int day);

    // [OWNED] 반환된 String 객체는 호출자가 RELEASE 해야 합니다.
    String* (*toRFC1123)(DateTime* self);
};

// [OWNED] 생성된 DateTime 객체는 호출자가 RELEASE 해야 합니다.
DateTime* new_DateTime_from_timestamp(time_t ts, bool is_utc);
DateTime* new_DateTime_now(void);
DateTime* new_DateTime_now_utc(void);
DateTime* new_DateTime_parse(const char* str, const char* fmt, int* err);

int    datetime_time(void);
double datetime_microtime(void);
int    datetime_strtotime(const char* str);

#endif // DATETIME_H