#ifndef LIBCORE_FILE_UTIL_H
#define LIBCORE_FILE_UTIL_H

#include "file.h"
#include "path.h"
#include <stdbool.h>

typedef enum { CORE_OK = 0, CORE_ERR = -1 } CoreResult;

File*       FileUtil_tmp(void);
File*       FileUtil_home(void);
File*       FileUtil_cwd(void);

CoreResult  FileUtil_copy(const File* src, const File* dest);
CoreResult  FileUtil_move(File* src, const File* dest);
File*       FileUtil_createTemp(const char* dir, const char* prefix);

bool        FileUtil_exists(const char* path);
bool        FileUtil_mkdirs(const char* path);
void        FileUtil_delete(const char* path);

#endif // LIBCORE_FILE_UTIL_H
