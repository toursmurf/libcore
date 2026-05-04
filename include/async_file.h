#ifndef ASYNC_FILE_H
#define ASYNC_FILE_H

#include "file.h"
#include "ring_buffer.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct AsyncFile AsyncFile;

typedef struct {
    char* data;
    size_t len;
} AsyncWriteItem;

struct AsyncFile {
    File          base;
    RingBuffer* queue;
    pthread_t     worker;
    volatile bool running;
    bool          rotate_daily;

    int  (*writeAsync)(AsyncFile* self, const char* data, size_t len);
    void (*flush)     (AsyncFile* self);
    void (*start)     (AsyncFile* self);
    void (*stop)      (AsyncFile* self);
};

AsyncFile* new_AsyncFile(const char* path, bool rotate_daily);

#endif // ASYNC_FILE_H