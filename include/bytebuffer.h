#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include "object.h"

// 🚨 정책: 최대 버퍼 용량 16MB 제한 (메모리 폭주 방지)
#define BB_MAX_CAPACITY (1024 * 1024 * 16)

typedef struct ByteBuffer ByteBuffer;

struct ByteBuffer {
    Object   base;
    uint8_t* data;
    size_t   capacity;
    size_t   read_pos;
    size_t   write_pos;

    /* ===== Write (쓰기) ===== */
    int (*writeByte)  (ByteBuffer* self, uint8_t b);
    int (*writeInt32) (ByteBuffer* self, int32_t v);
    int (*write)      (ByteBuffer* self, const void* buf, size_t len);

    /* ===== Read / Peek (읽기 및 확인) ===== */
    bool    (*readByte)   (ByteBuffer* self, uint8_t* out_b);
    bool    (*readInt32)  (ByteBuffer* self, int32_t* out_v);
    bool    (*peekInt32)  (ByteBuffer* self, int32_t* out_v);
    size_t  (*read)       (ByteBuffer* self, void* buf, size_t len);

    /* ===== 관리 및 탐색 ===== */
    void    (*compact)       (ByteBuffer* self);
    void    (*rewind)        (ByteBuffer* self);
    void    (*skip)          (ByteBuffer* self, size_t len);
    ssize_t (*indexOf)       (ByteBuffer* self, uint8_t target);
    size_t  (*remaining)     (ByteBuffer* self);
    size_t  (*readableBytes) (ByteBuffer* self);
    size_t  (*writableBytes) (ByteBuffer* self);

    /* ===== 소유권 이전 (Consume) ===== */
    ByteBuffer* (*readSlice)(ByteBuffer* self, size_t len);
};

ByteBuffer* new_ByteBuffer(size_t capacity);

// 매크로 유틸리티
#define BB_REMAINING(bb) ((bb)->write_pos - (bb)->read_pos)
#define BB_WRITABLE(bb)  ((bb)->capacity - (bb)->write_pos)
#define BB_CLEAR(bb)     do { (bb)->read_pos = 0; (bb)->write_pos = 0; } while(0)

#endif // BYTEBUFFER_H