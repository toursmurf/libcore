#ifndef LIBCORE_FILE_H
#define LIBCORE_FILE_H

#include "object.h"
#include "path.h"
#include "bytebuffer.h"
#include "arraylist.h"
#include "string_obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct File File;

typedef void (*EventCallback)(const char* path, int event);
typedef void (*TailCallback)(String* appendedText);

struct File {
    Object base;
    Path* filePath;
    int fd;
    bool is_open;

    bool    (*exists)(File* self);
    int64_t (*length)(File* self);
    bool    (*isFile)(File* self);
    bool    (*isSymlink)(File* self);
    bool    (*isReadable)(File* self);
    bool    (*isWritable)(File* self);
    bool    (*canExecute)(File* self);

    int64_t (*lastModifiedMs)(File* self);
    int64_t (*lastAccessedMs)(File* self);
    int64_t (*creationTimeMs)(File* self);

    String*     (*readAllText)(File* self);
    ByteBuffer* (*readAllBytes)(File* self);
    ArrayList*  (*readLines)(File* self);
    bool        (*writeString)(File* self, String* content);
    bool        (*appendString)(File* self, String* content);

    bool (*copyTo)(File* self, Path* destPath);
    bool (*deleteFile)(File* self);
    bool (*renameAtomic)(File* self, Path* newPath);
    bool (*fsync)(File* self);

    bool (*lockExclusive)(File* self);
    void (*unlock)(File* self);

    String* (*md5)(File* self);
    String* (*sha256)(File* self);
    bool    (*equalsContent)(File* self, File* other);

    String* (*getHumanSize)(File* self);
    String* (*guessMimeType)(File* self);
};

File* new_File(const char* pathStr);
bool File_Init(File* self, const char* path);
void File_Deinit(File* self);
#endif // LIBCORE_FILE_H
