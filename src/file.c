#define _GNU_SOURCE
#include "file.h"
#include "arraylist.h"
#include "list.h"
#include "object.h"
#include "string_obj.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include <stdio.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

// ----------------------------------------------------
// 전방 선언 (Forward Declarations)
// ----------------------------------------------------
static bool File_exists(File* self);
static int64_t File_length(File* self);
static bool File_isFile(File* self);
static bool File_isSymlink(File* self);
static bool File_isReadable(File* self);
static bool File_isWritable(File* self);
static bool File_canExecute(File* self);

static int64_t File_lastModifiedMs(File* self);
static int64_t File_lastAccessedMs(File* self);
static int64_t File_creationTimeMs(File* self);

static String* File_readAllText(File* self);
static ByteBuffer* File_readAllBytes(File* self);
static ArrayList* File_readLines(File* self);
static bool File_writeString(File* self, String* content);
static bool File_appendString(File* self, String* content);

static bool File_copyTo(File* self, Path* destPath);
static bool File_deleteFile(File* self);
static bool File_renameAtomic(File* self, Path* newPath);
static bool File_fsync(File* self);

static bool File_lockExclusive(File* self);
static void File_unlock(File* self);

// ============================================================================
// 1. 초기화
// ============================================================================
bool File_Init(File* self, const char* path) {
    if (!self || !path) {
        return false;
    }
    self->fd = -1;
    self->is_open = false;
    self->filePath = new_Path(path);

    if (!self->filePath) {
        return false;
    }

    self->exists = File_exists;
    self->length = File_length;
    self->isFile = File_isFile;
    self->isSymlink = File_isSymlink;
    self->isReadable = File_isReadable;
    self->isWritable = File_isWritable;
    self->canExecute = File_canExecute;
    self->lastModifiedMs = File_lastModifiedMs;
    self->lastAccessedMs = File_lastAccessedMs;
    self->creationTimeMs = File_creationTimeMs;
    self->readAllText = File_readAllText;
    self->readAllBytes = File_readAllBytes;
    self->readLines = File_readLines;
    self->writeString = File_writeString;
    self->appendString = File_appendString;
    self->copyTo = File_copyTo;
    self->deleteFile = File_deleteFile;
    self->renameAtomic = File_renameAtomic;
    self->fsync = File_fsync;
    self->lockExclusive = File_lockExclusive;
    self->unlock = File_unlock;

    return true;
}

// 🚀 [보안 패치] 레거시 File_Deinit 완전히 삭제됨 (이중 해제 위험 차단!)

// ============================================================================
// 2. finalize (ARC 전담)
// ============================================================================
static void File_finalize(Object* obj) {
    File* self = (File*)obj;

    if (self->fd >= 0) {
        close(self->fd);
        self->fd = -1;
    }

    if (self->filePath) {
        RELEASE((Object*)self->filePath);
        self->filePath = NULL;
    }
}

// ============================================================================
// 3. 내부 유틸리티
// ============================================================================
static int File_open(File* self, int flags) {
    if (!self) {
        return -1;
    }
    if (self->fd >= 0) {
        return self->fd;
    }

    self->fd = open(self->filePath->path, flags, 0644);

    if (self->fd < 0) {
        return -1;
    }

    self->is_open = true;
    return self->fd;
}

// ============================================================================
// 4. 상태 확인 메서드
// ============================================================================
static bool File_exists(File* self) {
    if (!self) {
        return false;
    }
    return access(self->filePath->path, F_OK) == 0;
}

static int64_t File_length(File* self) {
    struct stat st;

    if (!self) {
        return -1;
    }
    if (stat(self->filePath->path, &st) != 0) {
        return -1;
    }

    return (int64_t)st.st_size;
}

static bool File_isFile(File* self) {
    struct stat st;

    if (!self) {
        return false;
    }
    if (stat(self->filePath->path, &st) != 0) {
        return false;
    }

    return S_ISREG(st.st_mode);
}

static bool File_isSymlink(File* self) {
    struct stat st;

    if (!self) {
        return false;
    }
    if (lstat(self->filePath->path, &st) != 0) {
        return false;
    }

    return S_ISLNK(st.st_mode);
}

static bool File_isReadable(File* self) {
    if (!self) {
        return false;
    }
    return access(self->filePath->path, R_OK) == 0;
}

static bool File_isWritable(File* self) {
    if (!self) {
        return false;
    }
    return access(self->filePath->path, W_OK) == 0;
}

static bool File_canExecute(File* self) {
    if (!self) {
        return false;
    }
    return access(self->filePath->path, X_OK) == 0;
}

static int64_t File_lastModifiedMs(File* self) {
    struct stat st;

    if (!self) {
        return -1;
    }
    if (stat(self->filePath->path, &st) != 0) {
        return -1;
    }

    return (int64_t)st.st_mtime * 1000;
}

static int64_t File_lastAccessedMs(File* self) {
    struct stat st;

    if (!self) {
        return -1;
    }
    if (stat(self->filePath->path, &st) != 0) {
        return -1;
    }

    return (int64_t)st.st_atime * 1000;
}

static int64_t File_creationTimeMs(File* self) {
    struct stat st;

    if (!self) {
        return -1;
    }
    if (stat(self->filePath->path, &st) != 0) {
        return -1;
    }

    return (int64_t)st.st_ctime * 1000;
}

// ============================================================================
// 5. 읽기 (Read) 작업
// ============================================================================
static ByteBuffer* File_readAllBytes(File* self) {
    if (!self) {
        return NULL;
    }

    int fd = File_open(self, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    ByteBuffer* buf = new_ByteBuffer(1024);
    if (!buf) {
        return NULL;
    }

    char tmp[4096];

    while (1) {
        ssize_t n = read(fd, tmp, sizeof(tmp));

        if (n == 0) {
            break;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            RELEASE((Object*)buf);
            return NULL;
        }

        buf->write(buf, (uint8_t*)tmp, (size_t)n);
    }

    return buf;
}

static String* File_readAllText(File* self) {
    ByteBuffer* buf = File_readAllBytes(self);

    if (!buf) {
        return NULL;
    }

    // 🚀 [보안 패치] 원본 ByteBuffer 변형을 막고 명시적 안전 메모리 할당!
    char* safe_str = (char*)malloc(buf->write_pos + 1);

    if (!safe_str) {
        RELEASE((Object*)buf);
        return NULL;
    }

    memcpy(safe_str, buf->data, buf->write_pos);
    safe_str[buf->write_pos] = '\0'; // 안전하게 널 문자 주입!

    String* str = new_String(safe_str);

    free(safe_str);
    RELEASE((Object*)buf);

    return str;
}

static ArrayList* File_readLines(File* self) {
    ByteBuffer* buf = File_readAllBytes(self);

    if (!buf) {
        return NULL;
    }

    ArrayList* list = new_ArrayList(10);

    if (!list) {
        RELEASE((Object*)buf);
        return NULL;
    }

    size_t start = 0;

    for (size_t i = 0; i < buf->write_pos; i++) {
        if (buf->data[i] == '\n') {
            char* tmp = strndup((char*)buf->data + start, i - start);

            if (tmp) {
                list->add(list, (Object*)new_String(tmp));
                free(tmp);
            }

            start = i + 1;
        }
    }

    if (start < buf->write_pos) {
        char* tmp = strndup((char*)buf->data + start, buf->write_pos - start);

        if (tmp) {
            list->add(list, (Object*)new_String(tmp));
            free(tmp);
        }
    }

    RELEASE((Object*)buf);
    return list;
}

// ============================================================================
// 6. 쓰기 (Write) 작업
// ============================================================================
static bool write_full(int fd, const char* data, size_t len) {
    size_t written = 0;

    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        written += n;
    }

    return true;
}

static bool File_writeString(File* self, String* content) {
    if (!self || !content) {
        return false;
    }

    int fd = open(self->filePath->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        return false;
    }

    const char* data = content->c_str(content);
    bool ok = write_full(fd, data, strlen(data));

    fsync(fd);
    close(fd);

    return ok;
}

static bool File_appendString(File* self, String* content) {
    if (!self || !content) {
        return false;
    }

    int fd = open(self->filePath->path, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0) {
        return false;
    }

    const char* data = content->c_str(content);
    bool ok = write_full(fd, data, strlen(data));

    fsync(fd);
    close(fd);

    return ok;
}

// ============================================================================
// 7. 시스템 제어 및 보안 해싱
// ============================================================================
static bool File_copyTo(File* self, Path* destPath) {
    if (!self || !destPath) {
        return false;
    }

    ByteBuffer* buf = File_readAllBytes(self);

    if (!buf) {
        return false;
    }

    int fd = open(destPath->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        RELEASE((Object*)buf);
        return false;
    }

    bool ok = write_full(fd, (const char*)buf->data, buf->write_pos);

    close(fd);
    RELEASE((Object*)buf);

    return ok;
}

static bool File_deleteFile(File* self) {
    if (!self) {
        return false;
    }

    return unlink(self->filePath->path) == 0;
}

static bool File_renameAtomic(File* self, Path* newPath) {
    if (!self || !newPath) {
        return false;
    }

    return rename(self->filePath->path, newPath->path) == 0;
}

static bool File_fsync(File* self) {
    if (!self || self->fd < 0) {
        return false;
    }

    return fsync(self->fd) == 0;
}

static bool File_lockExclusive(File* self) {
    int fd = File_open(self, O_RDWR);

    if (fd < 0) {
        return false;
    }

    return flock(fd, LOCK_EX) == 0;
}

static void File_unlock(File* self) {
    if (self && self->fd >= 0) {
        flock(self->fd, LOCK_UN);
    }
}

static String* File_md5(File* self) {
    ByteBuffer* buf = File_readAllBytes(self);

    if (!buf) {
        return NULL;
    }

    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5(buf->data, buf->write_pos, hash);

    char out[33] = {0};

    for (int i = 0; i < 16; i++) {
        snprintf(out + (i * 2), 3, "%02x", hash[i]);
    }

    RELEASE((Object*)buf);
    return new_String(out);
}

static String* File_sha256(File* self) {
    ByteBuffer* buf = File_readAllBytes(self);

    if (!buf) {
        return NULL;
    }

    unsigned char hash[32];
    SHA256(buf->data, buf->write_pos, hash);

    char out[65] = {0};

    for (int i = 0; i < 32; i++) {
        snprintf(out + (i * 2), 3, "%02x", hash[i]);
    }

    RELEASE((Object*)buf);
    return new_String(out);
}

static bool File_equalsContent(File* self, File* other) {
    ByteBuffer* a = File_readAllBytes(self);
    ByteBuffer* b = File_readAllBytes(other);

    if (!a || !b) {
        if (a) {
            RELEASE((Object*)a);
        }
        if (b) {
            RELEASE((Object*)b);
        }
        return false;
    }

    bool eq = (a->write_pos == b->write_pos) && (memcmp(a->data, b->data, a->write_pos) == 0);

    RELEASE((Object*)a);
    RELEASE((Object*)b);
    return eq;
}

static String* File_getHumanSize(File* self) {
    int64_t size = File_length(self);
    char buf[64];

    if (size < 1024) {
        snprintf(buf, sizeof(buf), "%ld B", (long)size);
    }
    else if (size < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f KB", size / 1024.0);
    }
    else if (size < 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f MB", size / (1024.0 * 1024));
    }
    else {
        snprintf(buf, sizeof(buf), "%.2f GB", size / (1024.0 * 1024 * 1024.0));
    }

    return new_String(buf);
}

static String* File_guessMimeType(File* self) {
    const char* ext = strrchr(self->filePath->path, '.');

    if (!ext) {
        return new_String("application/octet-stream");
    }

    if (strcmp(ext, ".html") == 0) {
        return new_String("text/html");
    }

    if (strcmp(ext, ".json") == 0) {
        return new_String("application/json");
    }

    if (strcmp(ext, ".txt") == 0) {
        return new_String("text/plain");
    }

    return new_String("application/octet-stream");
}

const Class File_Class = {
    .name = "File",
    .size = sizeof(File),
    .finalize = File_finalize
};

// ============================================================================
// 8. 생성자
// ============================================================================
File* new_File(const char* pathStr) {
    if (!pathStr) {
        return NULL;
    }

    File* self = (File*)calloc(1, sizeof(File));

    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &File_Class);

    self->fd = -1;
    self->is_open = false;
    self->filePath = new_Path(pathStr);

    if (!self->filePath) {
        free(self);
        return NULL;
    }

    self->exists = File_exists;
    self->length = File_length;
    self->isFile = File_isFile;
    self->isSymlink = File_isSymlink;
    self->isReadable = File_isReadable;
    self->isWritable = File_isWritable;
    self->canExecute = File_canExecute;
    self->lastModifiedMs = File_lastModifiedMs;
    self->lastAccessedMs = File_lastAccessedMs;
    self->creationTimeMs = File_creationTimeMs;
    self->readAllText = File_readAllText;
    self->readAllBytes = File_readAllBytes;
    self->readLines = File_readLines;
    self->writeString = File_writeString;
    self->appendString = File_appendString;
    self->copyTo = File_copyTo;
    self->deleteFile = File_deleteFile;
    self->renameAtomic = File_renameAtomic;
    self->fsync = File_fsync;
    self->lockExclusive = File_lockExclusive;
    self->unlock = File_unlock;
    self->md5 = File_md5;
    self->sha256 = File_sha256;
    self->equalsContent = File_equalsContent;
    self->getHumanSize = File_getHumanSize;
    self->guessMimeType = File_guessMimeType;

    return self;
}