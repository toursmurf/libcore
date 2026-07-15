#ifndef STRING_BUILDER_H
#define STRING_BUILDER_H

#include "object.h"
#include "bytebuffer.h"
#include "string_obj.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct StringBuilder StringBuilder;

struct StringBuilder {
    Object base;
    ByteBuffer* buffer;

    StringBuilder* (*append)(StringBuilder* self, const char* str);
    StringBuilder* (*appendString)(StringBuilder* self, String* str);
    StringBuilder* (*appendBytes)(StringBuilder* self, const void* buf, size_t len);
    StringBuilder* (*appendChar)(StringBuilder* self, char ch);
    StringBuilder* (*appendBool)(StringBuilder* self, int value);
    StringBuilder* (*appendInt)(StringBuilder* self, int value);
    StringBuilder* (*appendLong)(StringBuilder* self, long value);
    StringBuilder* (*appendDouble)(StringBuilder* self, double value);
    StringBuilder* (*appendFormat)(StringBuilder* self, const char* fmt, ...);
    StringBuilder* (*appendLine)(StringBuilder* self);

    StringBuilder* (*clear)(StringBuilder* self);
    StringBuilder* (*truncate)(StringBuilder* self, size_t length);
    bool           (*reserve)(StringBuilder* self, size_t capacity);

    size_t         (*length)(StringBuilder* self);
    size_t         (*capacity)(StringBuilder* self);
    bool           (*isEmpty)(StringBuilder* self);

    const char*    (*c_str)(StringBuilder* self);
    String*        (*toString)(StringBuilder* self);
};

StringBuilder* new_StringBuilder(size_t initial_capacity);

#endif