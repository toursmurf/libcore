#ifndef ASYNC_LOGGER_H
#define ASYNC_LOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "logger.h"
#include "queue.h"
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AsyncLogger AsyncLogger;

//Object<-Logger<-AsyncLogger
struct AsyncLogger {
    Logger base; // 상속

    Queue* queue;

    pthread_t worker;
    pthread_mutex_t lock;
    pthread_cond_t cond;

    bool running;
    size_t batch_size;

    // 실제 출력 위임
    Logger* inner;

    void (*start)(AsyncLogger*);
    void (*stop)(AsyncLogger*);
};

/* =========================
   API
   ========================= */

AsyncLogger* new_AsyncLogger(int level);

void AsyncLogger_log(
    AsyncLogger* self,
    int level,
    const char* file,
    int line,
    const char* fmt,
    ...
);

/* =========================
   매크로
   ========================= */
#define ALOG_DEBUG(logger, fmt, ...) \
    AsyncLogger_log(logger, LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define ALOG_INFO(logger, fmt, ...) \
    AsyncLogger_log(logger, LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define ALOG_WARN(logger, fmt, ...) \
    AsyncLogger_log(logger, LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define ALOG_ERROR(logger, fmt, ...) \
    AsyncLogger_log(logger, LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#ifdef __cplusplus
}
#endif

#endif // ASYNC_LOGGER_H
