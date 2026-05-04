#define _GNU_SOURCE
#include "bytebuffer.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <limits.h>

// ============================================================================
// 0. 내부 확장 로직 (Dynamic Expansion & Compaction)
// ============================================================================
static int ensure_capacity(ByteBuffer* self, size_t len) {
    if (!self) return -1;
    if (len > SIZE_MAX - self->write_pos) return -1;

    // 🚨 [핵심 로직] 메모리가 부족할 때 무작정 늘리지 않고, 먼저 당기기(compact) 실행!!
    if (self->write_pos + len > self->capacity) {
        if (self->read_pos > 0) self->compact(self);
    }

    size_t needed = self->write_pos + len;
    if (needed <= self->capacity) return 0;

    size_t new_cap = self->capacity ? self->capacity : 1024;
    while (new_cap < needed) {
        if (new_cap > BB_MAX_CAPACITY / 2) return -1; // 16MB 제한 방어
        new_cap *= 2;
    }

    uint8_t* new_data = realloc(self->data, new_cap);
    if (!new_data) return -1;

    self->data = new_data;
    self->capacity = new_cap;
    return 0;
}

// ============================================================================
// 1. Write 구현부 (Network Endian 고려)
// ============================================================================
static int impl_writeByte(ByteBuffer* self, uint8_t b) {
    if (ensure_capacity(self, 1) != 0) return -1;
    self->data[self->write_pos++] = b;
    return 0;
}

static int impl_writeInt32(ByteBuffer* self, int32_t v) {
    if (ensure_capacity(self, 4) != 0) return -1;
    int32_t be = htonl(v); // 💡 호스트 → 네트워크(Big-Endian) 변환
    memcpy(self->data + self->write_pos, &be, 4);
    self->write_pos += 4;
    return 0;
}

static int impl_write(ByteBuffer* self, const void* buf, size_t len) {
    if (!buf || len == 0) return 0;
    if (ensure_capacity(self, len) != 0) return -1;
    memcpy(self->data + self->write_pos, buf, len);
    self->write_pos += len;
    return 0;
}

// ============================================================================
// 2. Read / Peek 구현부
// ============================================================================
static bool impl_readByte(ByteBuffer* self, uint8_t* out_b) {
    if (!self || !out_b || self->read_pos >= self->write_pos) return false;
    *out_b = self->data[self->read_pos++];
    return true;
}

static bool impl_readInt32(ByteBuffer* self, int32_t* out_v) {
    if (!self || !out_v || self->read_pos + 4 > self->write_pos) return false;
    int32_t be;
    memcpy(&be, self->data + self->read_pos, 4);
    self->read_pos += 4;
    *out_v = ntohl(be); // 💡 네트워크 → 호스트 변환
    return true;
}

static bool impl_peekInt32(ByteBuffer* self, int32_t* out_v) {
    if (!self || !out_v || self->read_pos + 4 > self->write_pos) return false;
    int32_t be;
    memcpy(&be, self->data + self->read_pos, 4);
    *out_v = ntohl(be);
    return true;
}

static size_t impl_read(ByteBuffer* self, void* buf, size_t len) {
    if (!self || !buf || len == 0) return 0;
    size_t available = self->write_pos - self->read_pos;
    if (len > available) len = available;
    memcpy(buf, self->data + self->read_pos, len);
    self->read_pos += len;
    return len;
}

// ============================================================================
// 3. 관리 및 탐색 로직
// ============================================================================
static void impl_skip(ByteBuffer* self, size_t len) {
    if (!self) return;
    self->read_pos = (self->read_pos + len > self->write_pos) ? self->write_pos : self->read_pos + len;
}

static ssize_t impl_indexOf(ByteBuffer* self, uint8_t target) {
    if (!self) return -1;
    for (size_t i = self->read_pos; i < self->write_pos; i++) {
        if (self->data[i] == target) return (ssize_t)(i - self->read_pos);
    }
    return -1;
}

static void impl_compact(ByteBuffer* self) {
    if (!self || self->read_pos == 0) return;
    size_t remaining = self->write_pos - self->read_pos;
    if (remaining > 0) memmove(self->data, self->data + self->read_pos, remaining);
#ifdef DEBUG
    memset(self->data + remaining, 0, self->capacity - remaining);
#endif
    self->read_pos = 0; // 🚨 읽은 만큼 당기고 리셋 완료!
    self->write_pos = remaining;
}

static void impl_rewind(ByteBuffer* self) {
    if (self) self->read_pos = 0;
}
static size_t impl_remaining(ByteBuffer* self) {
    return (self) ? (self->write_pos - self->read_pos) : 0;
}
static size_t impl_writableBytes(ByteBuffer* self) {
    return (self) ? (self->capacity - self->write_pos) : 0;
}

// ============================================================================
// 4. 슬라이싱 (소유권 독립 객체 반환)
// ============================================================================
static ByteBuffer* impl_readSlice(ByteBuffer* self, size_t len) {
    if (!self) return NULL;
    size_t available = self->write_pos - self->read_pos;
    if (len > available) len = available;

    ByteBuffer* out = new_ByteBuffer(len);
    if (!out) return NULL;

    out->write(out, self->data + self->read_pos, len);
    self->read_pos += len;
    return out;
}

// ============================================================================
// 5. ARC 및 Class 규격
// ============================================================================
static void ByteBuffer_finalize(Object* obj) {
    ByteBuffer* self = (ByteBuffer*)obj;
    if (self->data) {
        free(self->data);
        self->data = NULL;
    }
}

static const Class _ByteBufferClass = {
    .name = "ByteBuffer",
    .size = sizeof(ByteBuffer),
    .finalize = ByteBuffer_finalize
};

// ============================================================================
// 6. 생성자
// ============================================================================
ByteBuffer* new_ByteBuffer(size_t capacity) {
    if (capacity == 0) capacity = 1024;

    ByteBuffer* self = (ByteBuffer*)calloc(1, sizeof(ByteBuffer));
    if (!self) return NULL;

    Object_Init((Object*)self, &_ByteBufferClass);

    self->data = (uint8_t*)calloc(1, capacity);
    if (!self->data) {
        free(self);
        return NULL;
    }

    self->capacity = capacity;
    self->read_pos = 0;
    self->write_pos = 0;

    // VTable 바인딩
    self->writeByte = impl_writeByte;
    self->writeInt32 = impl_writeInt32;
    self->write = impl_write;

    self->readByte = impl_readByte;
    self->readInt32 = impl_readInt32;
    self->peekInt32 = impl_peekInt32;
    self->read = impl_read;

    self->skip = impl_skip;
    self->indexOf = impl_indexOf;
    self->compact = impl_compact;
    self->rewind = impl_rewind;
    self->remaining = impl_remaining;
    self->readableBytes = impl_remaining;
    self->writableBytes = impl_writableBytes;

    self->readSlice = impl_readSlice;
    return self;
}