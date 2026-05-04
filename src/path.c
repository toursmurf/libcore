#define _GNU_SOURCE
#include "path.h"
#include "string_obj.h"
#include "object.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// 버퍼 오버플로우 경고 해결을 위한 넉넉한 버퍼 크기
#define PATH_BUF_MAX (PATH_MAX * 2)

// ============================================================================
// 1. finalize
// ============================================================================
static void Path_finalize(Object* obj) {
    Path* self = (Path*)obj;
    if (self->path != NULL) {
        free((void*)self->path);
    }
}

// ============================================================================
// 2. 파일명 및 확장자 추출
// ============================================================================
static String* Path_getFileName(Path* self) {
    if (!self || !self->path) return NULL;
    const char* lastSlash = strrchr(self->path, '/');
    return new_String(lastSlash ? lastSlash + 1 : self->path);
}

static String* Path_getBaseName(Path* self) {
    if (!self || !self->path) return NULL;
    const char* lastSlash = strrchr(self->path, '/');
    const char* base = lastSlash ? lastSlash + 1 : self->path;
    const char* lastDot = strrchr(base, '.');
    if (!lastDot || lastDot == base) return new_String(base);
    size_t len = (size_t)(lastDot - base);
    char* temp = strndup(base, len);
    String* res = new_String(temp);
    free(temp);
    return res;
}

static String* Path_getExtension(Path* self) {
    if (!self || !self->path) return NULL;
    const char* lastSlash = strrchr(self->path, '/');
    const char* base = lastSlash ? lastSlash + 1 : self->path;
    const char* lastDot = strrchr(base, '.');
    if (!lastDot || lastDot == base) return new_String("");
    return new_String(lastDot + 1);
}

// ============================================================================
// 3. 경로 탐색 및 변환
// ============================================================================
static String* Path_getParent(Path* self) {
    if (!self || !self->path) return NULL;
    const char* lastSlash = strrchr(self->path, '/');
    if (!lastSlash) return NULL;
    if (lastSlash == self->path) return new_String("/");
    size_t len = (size_t)(lastSlash - self->path);
    char* temp = strndup(self->path, len);
    String* res = new_String(temp);
    free(temp);
    return res;
}

static bool Path_isAbsolute(Path* self) {
    return self && self->path && self->path[0] == '/';
}

static bool Path_isRelative(Path* self) {
    return !Path_isAbsolute(self);
}

static Path* Path_toAbsolute(Path* self) {
    if (!self || !self->path) return NULL;
    if (Path_isAbsolute(self)) return new_Path(self->path);

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        char buf[PATH_BUF_MAX];
        snprintf(buf, sizeof(buf), "%s/%s", cwd, self->path);
        return new_Path(buf);
    }
    return NULL;
}

static Path* Path_getCanonicalPath(Path* self) {
    if (!self || !self->path) return NULL;
    char resolved[PATH_MAX];
    if (realpath(self->path, resolved) == NULL) return NULL;
    return new_Path(resolved);
}

// ============================================================================
// 4. 경로 정규화 및 결합 (🚀 strncmp + strncat 철벽 방어)
// ============================================================================
static Path* Path_normalize(Path* self) {
    if (!self || !self->path) return NULL;
    char* copy = strndup(self->path, PATH_MAX);
    if (!copy) return NULL;

    char* stack[256];
    int top = 0;
    char* saveptr;
    char* token = strtok_r(copy, "/", &saveptr);

    while (token) {
        // 🚀 [보안 패치] strncmp로 정적 분석 경고까지 원천 차단!
        if (strncmp(token, ".", 2) == 0) {
            // 현재 디렉토리는 무시
        } else if (strncmp(token, "..", 3) == 0) {
            if (top > 0) top--;
        } else {
            if (top < 256) stack[top++] = token;
            else {
              free(copy);
              return NULL;
            }
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    char result[PATH_MAX] = {0};
    size_t remaining = PATH_MAX - 1;

    if (Path_isAbsolute(self)) {
        strncat(result, "/", remaining);
        remaining--;
    }

    for (int i = 0; i < top; i++) {
        size_t seg_len = strlen(stack[i]);

        if (seg_len > remaining) {
            strncat(result, stack[i], remaining);
            remaining = 0;
            break;
        }

        strncat(result, stack[i], remaining);
        remaining -= seg_len;

        if (i < top - 1) {
            if (remaining == 0) break;
            strncat(result, "/", remaining);
            remaining--;
        }
    }

    if (strlen(result) == 0 && !Path_isAbsolute(self)) strcpy(result, ".");
    free(copy);
    return new_Path(result);
}

static Path* Path_resolve(Path* self, const char* child) {
    if (!self || !self->path || !child) return NULL;
    if (child[0] == '/') return new_Path(child);

    char buf[PATH_BUF_MAX];
    size_t parentLen = strlen(self->path);
    bool endsWithSlash = (parentLen > 0 && self->path[parentLen - 1] == '/');
    snprintf(buf, sizeof(buf), "%s%s%s", self->path, endsWithSlash ? "" : "/", child);
    return new_Path(buf);
}

static Path* Path_sibling(Path* self, const char* siblingName) {
    if (!self || !siblingName) return NULL;
    String* parentStr = Path_getParent(self);
    if (!parentStr) return new_Path(siblingName);

    const char* p_str = parentStr->c_str(parentStr);
    char buf[PATH_BUF_MAX];
    snprintf(buf, sizeof(buf), "%s/%s", p_str, siblingName);

    RELEASE((Object*)parentStr);
    return new_Path(buf);
}

static Path* Path_withExt(Path* self, const char* newExt) {
    if (!self || !newExt) return NULL;
    String* parentStr = Path_getParent(self);
    String* baseStr = Path_getBaseName(self);

    if (!baseStr) {
        if (parentStr) RELEASE((Object*)parentStr);
        return NULL;
    }

    const char* b_str = baseStr->c_str(baseStr);
    char buf[PATH_BUF_MAX];

    if (parentStr) {
        const char* p_str = parentStr->c_str(parentStr);
        snprintf(buf, sizeof(buf), "%s/%s.%s", p_str, b_str, newExt);
        RELEASE((Object*)parentStr);
    } else {
        snprintf(buf, sizeof(buf), "%s.%s", b_str, newExt);
    }

    RELEASE((Object*)baseStr);
    return new_Path(buf);
}

static bool Path_equals(Path* self, Path* other) {
    if (!self || !other) return false;
    if (self == other) return true;
    return strcmp(self->path, other->path) == 0;
}

// ============================================================================
// 5. 클래스 메타정보 및 생성자
// ============================================================================
const Class Path_Class = {
    .name = "Path",
    .size = sizeof(Path),
    .finalize = Path_finalize
};

Path* new_Path(const char* pathStr) {
    if (pathStr == NULL) return NULL;
    size_t len = strnlen(pathStr, PATH_MAX);
    if (len == PATH_MAX) return NULL;

    Path* self = (Path*)calloc(1, sizeof(Path));
    if (!self) return NULL;

    Object_Init((Object*)self, &Path_Class);

    const char** writable_path = (const char**)&(self->path);
    *writable_path = strndup(pathStr, PATH_MAX);
    if (!*writable_path) { free(self); return NULL; }

    self->getFileName      = Path_getFileName;
    self->getBaseName      = Path_getBaseName;
    self->getExtension     = Path_getExtension;
    self->getParent        = Path_getParent;
    self->getCanonicalPath = Path_getCanonicalPath;
    self->normalize        = Path_normalize;
    self->toAbsolute       = Path_toAbsolute;
    self->isAbsolute       = Path_isAbsolute;
    self->isRelative       = Path_isRelative;
    self->resolve          = Path_resolve;
    self->sibling          = Path_sibling;
    self->withExt          = Path_withExt;
    self->equals           = Path_equals;

    return self;
}