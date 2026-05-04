#ifndef LIBCORE_PATH_H
#define LIBCORE_PATH_H

#include "object.h"
#include "string_obj.h"
#include <stdbool.h>

typedef struct Path Path;
struct Path {
    Object base;
    const char* const path;   // 내부 strdup 복사본 소유, finalize에서 free

    String* (*getFileName)(Path* self);
    String* (*getBaseName)(Path* self);
    String* (*getExtension)(Path* self);
    String* (*getParent)(Path* self);

    Path* (*getCanonicalPath)(Path* self);
    Path* (*normalize)(Path* self);
    Path* (*toAbsolute)(Path* self);
    bool (*isAbsolute)(Path* self);
    bool (*isRelative)(Path* self);

    Path* (*resolve)(Path* self, const char* child);
    Path* (*sibling)(Path* self, const char* siblingName);
    Path* (*withExt)(Path* self, const char* newExt);

    bool (*equals)(Path* self, Path* other);
};

Path* new_Path(const char* pathStr);

#endif // LIBCORE_PATH_H
