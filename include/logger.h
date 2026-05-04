#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <stdarg.h>
#include <inttypes.h>

#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
   Log Level
   ========================= */

#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_WARN  3
#define LOG_LEVEL_ERROR 4

/* =========================
   Log Event (확장 대비)
   ========================= */

typedef struct {
    int level;
    uint64_t timestamp;
    const char* message;
} LogEvent;

/* =========================
   Appender Callback
   ========================= */

typedef void (*LogAppenderCallback)(
    const LogEvent* event,
    void* userData
);

/* =========================
   Logger Object
   ========================= */

typedef struct Logger Logger;

//Object<-Logger
struct Logger {
    Object base; // ARC + Class

    int level;

    char* logFilePath;
    FILE* fileHandle;

    pthread_mutex_t lock;

    bool toConsole;
    bool toFile;

    // 확장 포인트
    LogAppenderCallback externalAppender;
    void* appenderData;

    // VTable 스타일 메서드
    void (*setLogFile)(Logger*, const char*);
    void (*setLevel)(Logger*, int);
    void (*addAppender)(Logger*, LogAppenderCallback, void*);

    void (*debug)(Logger*, const char*, ...);
    void (*info) (Logger*, const char*, ...);
    void (*warn) (Logger*, const char*, ...);
    void (*error)(Logger*, const char*, ...);
};

/* =========================
   API
   ========================= */

Logger* new_Logger(int level);
extern Logger* logger;
/* =========================
   매크로 (파일/라인 자동)
   ========================= */

#define LOG_DEBUG(logger, fmt, ...) \
    (logger)->debug((logger), "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_INFO(logger, fmt, ...) \
    (logger)->info((logger), "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_WARN(logger, fmt, ...) \
    (logger)->warn((logger), "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_ERROR(logger, fmt, ...) \
    (logger)->error((logger), "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
/* 전역 로거 외부 참조 선언 */
extern Logger* logger;
#endif // LOGGER_H