#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "object.h"
#include "path.h"
#include "arraylist.h"
#include <stdbool.h>

typedef struct Directory Directory;
struct Directory {
    Object base;
    Path* dirPath;

    bool       (*exists)(Directory* self);
    bool       (*mkdirs)(Directory* self);
    ArrayList* (*listFiles)(Directory* self);
    ArrayList* (*walkTree)(Directory* self);
    bool       (*deleteRecursive)(Directory* self);
};

Directory* new_Directory(const char* pathStr);

#endif // DIRECTORY_H
