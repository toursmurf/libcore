#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "object.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>

typedef struct SharedMemory SharedMemory;

typedef struct {
    size_t   data_len;
    uint32_t write_cnt;
} ShmHeader;

struct SharedMemory {
    Object   base;
    char     name[64];
    char     sem_name[72];
    size_t   size;
    void* addr;
    int      shm_fd;
    sem_t* sem;
    bool     is_owner;

    bool     (*write)      (SharedMemory* self, const void* data, size_t len);
    size_t   (*read)       (SharedMemory* self, void* buf, size_t buf_len);
    void     (*lock)       (SharedMemory* self);
    void     (*unlock)     (SharedMemory* self);
    void     (*clear)      (SharedMemory* self);
    size_t   (*dataLen)    (SharedMemory* self);
    uint32_t (*getWriteCnt)(SharedMemory* self);
};

SharedMemory* new_SharedMemory(const char* name, size_t size, bool create);

#endif // SHARED_MEMORY_H