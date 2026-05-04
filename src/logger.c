#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* =========================
   내부 유틸
   ========================= */

static uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static const char* level_str[] = {
    "",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

/**
 * 밀리초 타임스탬프를 가독성 있는 문자열로 변환
 * 예: 1776127118695 -> 2026-04-14 09:38:38.695
 */
void get_formatted_time(uint64_t ts_ms, char* out_buf, size_t buf_size) {
    time_t sec = (time_t)(ts_ms / 1000); // 초 단위 변환
    int ms = (int)(ts_ms % 1000);        // 남은 밀리초

    struct tm tm_info;
    localtime_r(&sec, &tm_info);         // Thread-safe하게 변환

    // YYYY-MM-DD HH:MM:SS 포맷팅
    strftime(out_buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);

    // 밀리초(.mmm) 추가
    char ms_buf[8];
    snprintf(ms_buf, sizeof(ms_buf), ".%03d", ms);
    strncat(out_buf, ms_buf, buf_size - strlen(out_buf) - 1);
}

Logger *logger = NULL;

/* =========================
   finalize
   ========================= */

static void Logger_finalize(Object* obj) {
    Logger* self = (Logger*)obj;

    if (self->fileHandle) {
        fclose(self->fileHandle);
    }

    if (self->logFilePath) {
        free(self->logFilePath);
    }

    pthread_mutex_destroy(&self->lock);
}

/* =========================
   Class 정의
   ========================= */

static Class Logger_Class = {
    .name = "Logger",
    .size = sizeof(Logger),

    .toString = NULL,
    .equals   = NULL,
    .hashCode = NULL,
    .finalize = Logger_finalize
};

/* =========================
   메서드 구현
   ========================= */
extern char* safe_strdup(const char* src, size_t max_len);
static void Logger_setLevel(Logger* self, int level) {
    pthread_mutex_lock(&self->lock);
    self->level = level;
    pthread_mutex_unlock(&self->lock);
}

static void Logger_setLogFile(Logger* self, const char* path) {
    pthread_mutex_lock(&self->lock);

    if (self->fileHandle) {
        fclose(self->fileHandle);
        self->fileHandle = NULL;
    }

    if (self->logFilePath) {
        free(self->logFilePath);
    }

    self->logFilePath = safe_strdup(path, 1024);;
    self->fileHandle = fopen(path, "a");

    if (self->fileHandle) {
        self->toFile = true;
    }

    pthread_mutex_unlock(&self->lock);
}

static void Logger_addAppender(
    Logger* self,
    LogAppenderCallback cb,
    void* data
) {
    pthread_mutex_lock(&self->lock);
    self->externalAppender = cb;
    self->appenderData = data;
    pthread_mutex_unlock(&self->lock);
}

static void Logger_log_internal(
    Logger* self,
    int level,
    const char* fmt,
    va_list args
) {
    if (level < self->level) return;

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    uint64_t ts = now_ms();
		// 💡 [추가] 밀리초를 가독성 있는 문자열로 변환
    char time_str[32];
    get_formatted_time(ts, time_str, sizeof(time_str));
    LogEvent event = {
        .level = level,
        .timestamp = ts,
        .message = buffer
    };

    // lock 최소화
    pthread_mutex_lock(&self->lock);

    FILE* fh = self->fileHandle;
    bool toConsole = self->toConsole;
    LogAppenderCallback appender = self->externalAppender;
    void* appenderData = self->appenderData;

    pthread_mutex_unlock(&self->lock);

    // ✅ 콘솔 출력 수정: %" PRIu64 " -> [%s]
		if (toConsole) {
			printf("[%s][%s] %s\n",
        time_str, level_str[level], buffer);
    }

    // ✅ 파일 출력 수정: %" PRIu64 " -> [%s]
    if (fh) {
      fprintf(fh, "[%s][%s] %s\n",
        time_str, level_str[level], buffer);
        fflush(fh);
      }

    // 외부 Appender (lock 밖)
    if (appender) {
        appender(&event, appenderData);
    }
}

static void Logger_debug(Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Logger_log_internal(self, LOG_LEVEL_DEBUG, fmt, args);
    va_end(args);
}

static void Logger_info(Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Logger_log_internal(self, LOG_LEVEL_INFO, fmt, args);
    va_end(args);
}

static void Logger_warn(Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Logger_log_internal(self, LOG_LEVEL_WARN, fmt, args);
    va_end(args);
}

static void Logger_error(Logger* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Logger_log_internal(self, LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}

/* =========================
   생성자
   ========================= */

Logger* new_Logger(int level) {
    Logger* self = calloc(1, sizeof(Logger));
    if (!self) return NULL;

    Object_Init((Object*)self, &Logger_Class);

    pthread_mutex_init(&self->lock, NULL);

    self->level = level;
    self->toConsole = true;

    // VTable 연결
    self->setLevel   = Logger_setLevel;
    self->setLogFile = Logger_setLogFile;
    self->addAppender = Logger_addAppender;

    self->debug = Logger_debug;
    self->info  = Logger_info;
    self->warn  = Logger_warn;
    self->error = Logger_error;

    return self;
}
