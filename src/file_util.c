#define _GNU_SOURCE
#include "file_util.h"
#include "file.h"
#include "path.h"
#include "directory.h"
#include "object.h"      // Object 시스템 연동

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

// ============================================================================
// [1] 내부 유틸: write_full (EINTR + partial write 처리)
// ============================================================================
static bool write_full(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t total = 0;

    while (total < len) {
        ssize_t n = write(fd, p + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        total += (size_t)n;
    }
    return true;
}

// ============================================================================
// [2] 표준 경로 유틸리티
// ============================================================================
File* FileUtil_tmp(void) {
    return new_File("/tmp");
}

File* FileUtil_home(void) {
    const char* h = getenv("HOME");
    return h ? new_File(h) : NULL;
}

File* FileUtil_cwd(void) {
    char buf[PATH_MAX];
    return getcwd(buf, sizeof(buf)) ? new_File(buf) : NULL;
}

// ============================================================================
// [3] 파일 복사 (EINTR 완전 대응)
// ============================================================================
CoreResult FileUtil_copy(const File* src, const File* dest) {
    if (!src || !dest || !src->filePath || !dest->filePath) return CORE_ERR;

    int in = open(src->filePath->path, O_RDONLY);
    if (in < 0) return CORE_ERR;

    int out = open(dest->filePath->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return CORE_ERR;
    }

    char buf[8192];
    ssize_t n;

    while (1) {
        n = read(in, buf, sizeof(buf));
        if (n == 0) break; // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            close(in); close(out);
            return CORE_ERR;
        }

        if (!write_full(out, buf, (size_t)n)) {
            close(in); close(out);
            return CORE_ERR;
        }
    }

    fsync(out);
    close(in);
    close(out);
    return CORE_OK;
}

// ============================================================================
// [4] 파일 이동 (rename + fallback)
// ============================================================================
CoreResult FileUtil_move(File* src, const File* dest) {
    if (!src || !dest || !src->filePath || !dest->filePath) return CORE_ERR;

    // 1차 시도: 동일 파티션 내 rename (Atomic)
    if (rename(src->filePath->path, dest->filePath->path) == 0) {
        return CORE_OK;
    }

    // 2차 시도: Cross-device fallback (Copy & Unlink)
    if (FileUtil_copy(src, dest) != CORE_OK) return CORE_ERR;

    unlink(src->filePath->path);
    return CORE_OK;
}

// ============================================================================
// [5] 임시 파일 생성
// ============================================================================
File* FileUtil_createTemp(const char* dir, const char* prefix) {
    if (!dir || !prefix) return NULL;

    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof(tmpl), "%s/%sXXXXXX", dir, prefix);

    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;

    close(fd);
    return new_File(tmpl);
}

// ============================================================================
// [6] 경로 기반 유틸리티
// ============================================================================
bool FileUtil_exists(const char* path) {
    return path && access(path, F_OK) == 0;
}

bool FileUtil_mkdirs(const char* path) {
    if (!path) return false;

    Directory* dir = new_Directory(path);
    if (!dir) return false;

    bool res = dir->mkdirs(dir);
    // [경고 해결] 명시적 캐스팅 적용
    RELEASE((Object*)dir);
    return res;
}

void FileUtil_delete(const char* path) {
    if (!path) return;

    struct stat st;
    if (lstat(path, &st) != 0) return;

    if (S_ISDIR(st.st_mode)) {
        Directory* dir = new_Directory(path);
        if (dir) {
            dir->deleteRecursive(dir);
            RELEASE((Object*)dir); // 명시적 캐스팅
        }
    } else {
        unlink(path);
    }
}
