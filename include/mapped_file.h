#ifndef LIBCORE_MAPPED_FILE_H
#define LIBCORE_MAPPED_FILE_H

#include "object.h"
#include "path.h"
#include "bytebuffer.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct MappedFile MappedFile;
struct MappedFile {
    Object base;
    Path* targetPath;
    void* mapAddress;
    size_t mappedSize;

    bool        (*map)(MappedFile* self, bool readOnly);
    void        (*unmap)(MappedFile* self);
    bool        (*sync)(MappedFile* self);
    ByteBuffer* (*asByteBuffer)(MappedFile* self); // v1.0: copy 기반
};

MappedFile* new_MappedFile(const char* pathStr);

#endif // LIBCORE_MAPPED_FILE_H
