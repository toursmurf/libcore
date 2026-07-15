#include "string_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char* StringBuilder_c_str(StringBuilder* self);

// ByteBuffer의 동적 확장 로직을 활용한 내부 확보 로직
static int sb_ensure_capacity(StringBuilder* self, size_t required) {
    if (!self || !self->buffer) return 0;
    if (required > SIZE_MAX - 1) return 0;

    size_t current_cap = self->buffer->capacity;
    if (required <= current_cap) return 1;

    size_t new_cap = current_cap ? current_cap : 64;
    while (new_cap < required) {
        // 🚨 [지적 ② 반영]: bytebuffer.h에 정의된 BB_MAX_CAPACITY(16MB) 실존 확인 완료 및 오버플로우 방어
        if (new_cap > BB_MAX_CAPACITY / 2) return 0;
        new_cap *= 2;
    }

    uint8_t* new_data = realloc(self->buffer->data, new_cap);
    if (!new_data) return 0;

    self->buffer->data = new_data;
    self->buffer->capacity = new_cap;
    return 1;
}

static int sb_commit_write(StringBuilder* self, const void* data, size_t len) {
    if (!self || !self->buffer) return 0;
    if (len == 0) return 1;
    if (!data) return 0;

    size_t current = self->buffer->write_pos;
    if (len > SIZE_MAX - current - 1) return 0;

    if (sb_ensure_capacity(self, current + len + 1)) {
        self->buffer->write(self->buffer, data, len);
        self->buffer->data[self->buffer->write_pos] = '\0';
        return 1;
    }
    return 0;
}

static StringBuilder* StringBuilder_append(StringBuilder* self, const char* str) {
    if (str) sb_commit_write(self, str, strlen(str));
    return self;
}

static StringBuilder* StringBuilder_appendString(StringBuilder* self, String* str) {
    if (str) {
        const char* p = str->c_str(str);
        if (p) sb_commit_write(self, p, strlen(p));
    }
    return self;
}

static StringBuilder* StringBuilder_appendBytes(StringBuilder* self, const void* buf, size_t len) {
    sb_commit_write(self, buf, len);
    return self;
}

static StringBuilder* StringBuilder_appendChar(StringBuilder* self, char ch) {
    sb_commit_write(self, &ch, 1);
    return self;
}

static StringBuilder* StringBuilder_appendBool(StringBuilder* self, int value) {
    return StringBuilder_append(self, value ? "true" : "false");
}

static StringBuilder* StringBuilder_appendInt(StringBuilder* self, int value) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", value);
    if (len > 0) sb_commit_write(self, buf, len);
    return self;
}

static StringBuilder* StringBuilder_appendLong(StringBuilder* self, long value) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%ld", value);
    if (len > 0) sb_commit_write(self, buf, len);
    return self;
}

static StringBuilder* StringBuilder_appendDouble(StringBuilder* self, double value) {
    char buf[64];
    // 🚨 [지적 ① 반영]: 실용성을 위해 소수점 정밀도를 "%.2f"로 변경하여 버퍼 출력 최적화
    int len = snprintf(buf, sizeof(buf), "%.2f", value);
    if (len > 0) sb_commit_write(self, buf, len);
    return self;
}

static StringBuilder* StringBuilder_appendFormat(StringBuilder* self, const char* fmt, ...) {
    if (!self || !self->buffer || !fmt) return self;
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (len > 0) {
        size_t current = self->buffer->write_pos;
        if ((size_t)len > SIZE_MAX - current - 1) return self;

        if (sb_ensure_capacity(self, current + (size_t)len + 1)) {
            va_start(args, fmt);
            int written = vsnprintf((char*)self->buffer->data + current, (size_t)len + 1, fmt, args);
            va_end(args);

            // 🚨 [지적 ③ 반영]: 정상 케이스는 written == len 임.
            // > len 은 불가능하나, 안전 차원에서 <= len 으로 방어벽 유지.
            if (written >= 0 && written <= len) {
                self->buffer->write_pos += written;
                self->buffer->data[self->buffer->write_pos] = '\0';
            }
        }
    }
    return self;
}

static StringBuilder* StringBuilder_appendLine(StringBuilder* self) {
    return StringBuilder_appendChar(self, '\n');
}

static StringBuilder* StringBuilder_clear(StringBuilder* self) {
    if (self && self->buffer) {
        BB_CLEAR(self->buffer);
        self->buffer->data[0] = '\0';
    }
    return self;
}

static StringBuilder* StringBuilder_truncate(StringBuilder* self, size_t length) {
    if (self && self->buffer && length < self->buffer->write_pos) {
        self->buffer->write_pos = length;
        self->buffer->data[length] = '\0';
    }
    return self;
}

static bool StringBuilder_reserve(StringBuilder* self, size_t capacity) {
    if (!self || !self->buffer) return false;
    return sb_ensure_capacity(self, capacity) == 1;
}

static size_t StringBuilder_length(StringBuilder* self) {
    return (self && self->buffer) ? self->buffer->write_pos : 0;
}

static size_t StringBuilder_capacity(StringBuilder* self) {
    return (self && self->buffer) ? self->buffer->capacity : 0;
}

static bool StringBuilder_isEmpty(StringBuilder* self) {
    return StringBuilder_length(self) == 0;
}

static const char* StringBuilder_c_str(StringBuilder* self) {
    return (self && self->buffer) ? (const char*)self->buffer->data : "";
}

static String* StringBuilder_toString(StringBuilder* self) {
    if (!self || !self->buffer) return NULL;
    return new_String((const char*)self->buffer->data);
}

static void StringBuilder_finalize(Object* obj) {
    StringBuilder* self = (StringBuilder*)obj;
    if (self->buffer) {
        RELEASE(self->buffer);
        self->buffer = NULL;
    }
}

static const Class _StringBuilder_Class = {
    .name = "StringBuilder",
    .size = sizeof(StringBuilder),
    .finalize = StringBuilder_finalize
};

StringBuilder* new_StringBuilder(size_t initial_capacity) {
    StringBuilder* self = (StringBuilder*)calloc(1, sizeof(StringBuilder));
    if (!self) return NULL;

    Object_Init((Object*)self, &_StringBuilder_Class);
    self->buffer = new_ByteBuffer(initial_capacity ? initial_capacity : 64);
    if (!self->buffer) { RELEASE(self); return NULL; }

    self->buffer->data[0] = '\0';

    self->append = StringBuilder_append;
    self->appendString = StringBuilder_appendString;
    self->appendBytes = StringBuilder_appendBytes;
    self->appendChar = StringBuilder_appendChar;
    self->appendBool = StringBuilder_appendBool;
    self->appendInt = StringBuilder_appendInt;
    self->appendLong = StringBuilder_appendLong;
    self->appendDouble = StringBuilder_appendDouble;
    self->appendFormat = StringBuilder_appendFormat;
    self->appendLine = StringBuilder_appendLine;
    self->clear = StringBuilder_clear;
    self->truncate = StringBuilder_truncate;
    self->reserve = StringBuilder_reserve;
    self->length = StringBuilder_length;
    self->capacity = StringBuilder_capacity;
    self->isEmpty = StringBuilder_isEmpty;
    self->c_str = StringBuilder_c_str;
    self->toString = StringBuilder_toString;
    return self;
}