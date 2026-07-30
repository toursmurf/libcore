#include "time_utils.h"

uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

uint64_t now_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}
void format_time_str(double elapsed_ms, char* buf, size_t size) {
    double sec = elapsed_ms / 1000.0;

    if (sec < 60.0) {
        snprintf(buf, size, "%.2f sec", sec);
    } else if (sec < 3600.0) {
        snprintf(buf, size, "%.2f min", sec / 60.0);
    } else {
        snprintf(buf, size, "%.2f hr", sec / 3600.0);
    }
}
/* ✨ 타임스탬프 공통 유틸리티 함수 추가 ✨ */
void get_current_timestamp(char* buf, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%S+09:00", tm_info);
}
