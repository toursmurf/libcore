#define _GNU_SOURCE
#include "shared_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

static ShmHeader* get_header(SharedMemory* self) {
    return (ShmHeader*)self->addr;
}

static void* get_data_area(SharedMemory* self) {
    return (uint8_t*)self->addr + sizeof(ShmHeader);
}

static void SharedMemory_lock(SharedMemory* self) {
    if (!self || !self->sem) {
        return;
    }
    sem_wait(self->sem);
}

static void SharedMemory_unlock(SharedMemory* self) {
    if (!self || !self->sem) {
        return;
    }
    sem_post(self->sem);
}

static bool SharedMemory_write(SharedMemory* self, const void* data, size_t len) {
    if (!self || !data || len == 0) {
        return false;
    }
    if (len > self->size) {
        return false;
    }

    ShmHeader* hdr  = get_header(self);
    void* area = get_data_area(self);

    memcpy(area, data, len);
    hdr->data_len = len;
    hdr->write_cnt++;

    return true;
}

static size_t SharedMemory_read(SharedMemory* self, void* buf, size_t buf_len) {
    if (!self || !buf || buf_len == 0) {
        return 0;
    }

    ShmHeader* hdr  = get_header(self);
    void* area = get_data_area(self);

    size_t copy_len = hdr->data_len;

    if (copy_len == 0) {
        return 0;
    }
    if (copy_len > buf_len) {
        copy_len = buf_len;
    }

    memcpy(buf, area, copy_len);
    return copy_len;
}

static void SharedMemory_clear(SharedMemory* self) {
    if (!self || !self->addr) {
        return;
    }

    ShmHeader* hdr = get_header(self);
    hdr->data_len  = 0;
    hdr->write_cnt = 0;

    memset(get_data_area(self), 0, self->size);
}

static size_t SharedMemory_dataLen(SharedMemory* self) {
    if (!self || !self->addr) {
        return 0;
    }
    return get_header(self)->data_len;
}

static uint32_t SharedMemory_getWriteCnt(SharedMemory* self) {
    if (!self || !self->addr) {
        return 0;
    }
    return get_header(self)->write_cnt;
}

static void SharedMemory_finalize(Object* obj) {
    SharedMemory* self = (SharedMemory*)obj;

    if (self->addr && self->addr != MAP_FAILED) {
        munmap(self->addr, sizeof(ShmHeader) + self->size);
        self->addr = NULL;
    }

    if (self->shm_fd >= 0) {
        close(self->shm_fd);
        self->shm_fd = -1;
    }

    if (self->sem && self->sem != SEM_FAILED) {
        sem_close(self->sem);
        self->sem = NULL;
    }

    if (self->is_owner) {
        shm_unlink(self->name);
        sem_unlink(self->sem_name);
    }
}

static const Class _shmClass = {
    .name     = "SharedMemory",
    .size     = sizeof(SharedMemory),
    .finalize = SharedMemory_finalize
};

SharedMemory* new_SharedMemory(const char* name, size_t size, bool create) {
    if (!name || size == 0) {
        return NULL;
    }

    SharedMemory* self = (SharedMemory*)calloc(1, sizeof(SharedMemory));
    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &_shmClass);

    strncpy(self->name, name, sizeof(self->name) - 1);
    snprintf(self->sem_name, sizeof(self->sem_name), "%s_sem", name);

    self->size     = size;
    self->shm_fd   = -1;
    self->addr     = NULL;
    self->sem      = NULL;
    self->is_owner = create;

    size_t total_size = sizeof(ShmHeader) + size;

    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;

    self->shm_fd = shm_open(name, flags, 0666);
    if (self->shm_fd < 0) {
        RELEASE((Object*)self);
        return NULL;
    }

    if (create) {
        if (ftruncate(self->shm_fd, (off_t)total_size) < 0) {
            RELEASE((Object*)self);
            return NULL;
        }
    } else {
        struct stat st;
        if (fstat(self->shm_fd, &st) == 0 && st.st_size > (off_t)sizeof(ShmHeader)) {
            total_size = (size_t)st.st_size;
            self->size = total_size - sizeof(ShmHeader);
        } else {
            RELEASE((Object*)self);
            return NULL;
        }
    }

    self->addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, self->shm_fd, 0);
    if (self->addr == MAP_FAILED) {
        self->addr = NULL;
        RELEASE((Object*)self);
        return NULL;
    }

    if (create) {
        memset(self->addr, 0, total_size);
    }

    if (create) {
        sem_unlink(self->sem_name);
        self->sem = sem_open(self->sem_name, O_CREAT | O_EXCL, 0666, 1);
    } else {
        self->sem = sem_open(self->sem_name, 0);
    }

    if (self->sem == SEM_FAILED) {
        self->sem = NULL;
        RELEASE((Object*)self);
        return NULL;
    }

    self->write       = SharedMemory_write;
    self->read        = SharedMemory_read;
    self->lock        = SharedMemory_lock;
    self->unlock      = SharedMemory_unlock;
    self->clear       = SharedMemory_clear;
    self->dataLen     = SharedMemory_dataLen;
    self->getWriteCnt = SharedMemory_getWriteCnt;

    return self;
}