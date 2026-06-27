#define _GNU_SOURCE
#include "datetime.h"
#include <stdlib.h>

/* [ARCHITECTURE] 헤더에서 숨겨진 내부 구현체입니다. 이제 사용자는 timestamp에 직접 접근할 수 없습니다. */
typedef struct {
    DateTime public_api;
    time_t timestamp;
    bool is_utc;
} DateTimeImpl;

static void DateTime_finalize(Object* self) {
    (void)self;
}

static void DateTime_toString(Object* self, char* buffer, size_t len) {
    DateTimeImpl* impl = (DateTimeImpl*)self;
    struct tm tm_info;

    if (impl->is_utc) {
        gmtime_r(&impl->timestamp, &tm_info);
        strftime(buffer, len, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    } else {
        localtime_r(&impl->timestamp, &tm_info);
        strftime(buffer, len, "%Y-%m-%dT%H:%M:%S%z", &tm_info);
    }
}

static bool DateTime_equals(Object* self, Object* other) {
    if (!instanceOf(other, self->type)) return false;

    DateTimeImpl* dt1 = (DateTimeImpl*)self;
    DateTimeImpl* dt2 = (DateTimeImpl*)other;

    return (dt1->timestamp == dt2->timestamp) && (dt1->is_utc == dt2->is_utc);
}

static int DateTime_hashCode(Object* self) {
    DateTimeImpl* impl = (DateTimeImpl*)self;

    return (int)(impl->timestamp ^ (impl->timestamp >> 32)) ^ (impl->is_utc ? 1 : 0);
}

static Class _DateTimeClass = {
    .name = "DateTime",
    .size = sizeof(DateTimeImpl),
    .toString = DateTime_toString,
    .equals = DateTime_equals,
    .hashCode = DateTime_hashCode,
    .finalize = DateTime_finalize
};

static void _get_tm(DateTimeImpl* impl, struct tm* tm_info) {
    if (impl->is_utc) gmtime_r(&impl->timestamp, tm_info);
    else localtime_r(&impl->timestamp, tm_info);
}

static int dt_getYear(DateTime* self) { struct tm tm; _get_tm((DateTimeImpl*)self, &tm); return tm.tm_year + 1900; }
static int dt_getMonth(DateTime* self) { struct tm tm; _get_tm((DateTimeImpl*)self, &tm); return tm.tm_mon + 1; }
static int dt_getDay(DateTime* self) { struct tm tm; _get_tm((DateTimeImpl*)self, &tm); return tm.tm_mday; }
static int dt_getHour(DateTime* self) { struct tm tm; _get_tm((DateTimeImpl*)self, &tm); return tm.tm_hour; }
static int dt_getMinute(DateTime* self) { struct tm tm; _get_tm((DateTimeImpl*)self, &tm); return tm.tm_min; }
static int dt_getSecond(DateTime* self) { struct tm tm; _get_tm((DateTimeImpl*)self, &tm); return tm.tm_sec; }

static DateTime* dt_addDays(DateTime* self, int days) {
    DateTimeImpl* impl = (DateTimeImpl*)self;
    struct tm tm_info;
    _get_tm(impl, &tm_info);

    tm_info.tm_mday += days;

    /* [REFACTOR] 어설픈 수학 계산을 소각하고, Rocky Linux 표준이자 강력한 네이티브 timegm을 신뢰합니다. */
    time_t new_ts = impl->is_utc ? timegm(&tm_info) : mktime(&tm_info);

    return new_DateTime_from_timestamp(new_ts, impl->is_utc);
}

static long dt_diffSeconds(DateTime* self, DateTime* other) {
    DateTimeImpl* dt1 = (DateTimeImpl*)self;
    DateTimeImpl* dt2 = (DateTimeImpl*)other;

    return (long)(dt1->timestamp - dt2->timestamp);
}

static bool dt_isLeapYear(DateTime* self) {
    int year = self->getYear(self);

    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int dt_daysInMonth(DateTime* self) {
    int month = self->getMonth(self);

    if (month == 2) return self->isLeapYear(self) ? 29 : 28;
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;

    return 31;
}

static DateTime* dt_withYear(DateTime* self, int year) {
    DateTimeImpl* impl = (DateTimeImpl*)self;
    struct tm tm_info;
    _get_tm(impl, &tm_info);

    tm_info.tm_year = year - 1900;

    return new_DateTime_from_timestamp(impl->is_utc ? timegm(&tm_info) : mktime(&tm_info), impl->is_utc);
}

static DateTime* dt_withMonth(DateTime* self, int month) {
    DateTimeImpl* impl = (DateTimeImpl*)self;
    struct tm tm_info;
    _get_tm(impl, &tm_info);

    tm_info.tm_mon = month - 1;

    return new_DateTime_from_timestamp(impl->is_utc ? timegm(&tm_info) : mktime(&tm_info), impl->is_utc);
}

static DateTime* dt_withDay(DateTime* self, int day) {
    DateTimeImpl* impl = (DateTimeImpl*)self;
    struct tm tm_info;
    _get_tm(impl, &tm_info);

    tm_info.tm_mday = day;

    return new_DateTime_from_timestamp(impl->is_utc ? timegm(&tm_info) : mktime(&tm_info), impl->is_utc);
}

static String* dt_toRFC1123(DateTime* self) {
    DateTimeImpl* impl = (DateTimeImpl*)self;
    char buf[128];
    struct tm tm_info;

    gmtime_r(&impl->timestamp, &tm_info);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_info);

    return new_String(buf);
}

DateTime* new_DateTime_from_timestamp(time_t ts, bool is_utc) {
    DateTimeImpl* impl = (DateTimeImpl*)malloc(sizeof(DateTimeImpl));

    if (!impl) return NULL;

    Object_Init((Object*)impl, &_DateTimeClass);

    impl->timestamp = ts;
    impl->is_utc = is_utc;

    impl->public_api.getYear = dt_getYear;
    impl->public_api.getMonth = dt_getMonth;
    impl->public_api.getDay = dt_getDay;
    impl->public_api.getHour = dt_getHour;
    impl->public_api.getMinute = dt_getMinute;
    impl->public_api.getSecond = dt_getSecond;
    impl->public_api.addDays = dt_addDays;
    impl->public_api.diffSeconds = dt_diffSeconds;
    impl->public_api.isLeapYear = dt_isLeapYear;
    impl->public_api.daysInMonth = dt_daysInMonth;
    impl->public_api.withYear = dt_withYear;
    impl->public_api.withMonth = dt_withMonth;
    impl->public_api.withDay = dt_withDay;
    impl->public_api.toRFC1123 = dt_toRFC1123;

    return (DateTime*)impl;
}

DateTime* new_DateTime_now(void) {
    return new_DateTime_from_timestamp(time(NULL), false);
}

DateTime* new_DateTime_now_utc(void) {
    return new_DateTime_from_timestamp(time(NULL), true);
}

DateTime* new_DateTime_parse(const char* str, const char* fmt, int* err) {
    struct tm tm_info;
    memset(&tm_info, 0, sizeof(struct tm));

    if (strptime(str, fmt, &tm_info) == NULL) {
        if (err) *err = -1;
        return NULL;
    }

    if (err) *err = 0;

    return new_DateTime_from_timestamp(mktime(&tm_info), false);
}

int datetime_time(void) {
    return (int)time(NULL);
}

double datetime_microtime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

int datetime_strtotime(const char* str) {
    if (!str) return 0;

    int err = 0;
    DateTime* dt = NULL;

    const char* formats[] = {
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d",
        "%Y/%m/%d %H:%M:%S",
        "%Y/%m/%d",
        NULL
    };

    for (int i = 0; formats[i] != NULL; i++) {
        dt = new_DateTime_parse(str, formats[i], &err);

        if (dt != NULL && err == 0) {
            /* Opaque Pointer 구조에 맞게 내부 구현체로 캐스팅하여 값을 추출합니다. */
            DateTimeImpl* impl = (DateTimeImpl*)dt;
            int ts = (int)impl->timestamp;
            RELEASE(dt);
            return ts;
        }
    }

    return 0;
}