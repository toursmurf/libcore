#include "path_validator.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Rule 02: UTF-8 유효성 검사 (Overlong & Surrogate 완벽 차단) */
static bool rule_02_check_utf8(const unsigned char *s) {
    while (*s) {
        if (*s < 0x80) {
            s++;
        } else if ((*s & 0xE0) == 0xC0) {
            if (*s < 0xC2 || (s[1] & 0xC0) != 0x80) return false;
            s += 2;
        } else if ((*s & 0xF0) == 0xE0) {
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return false;
            if (*s == 0xE0 && s[1] < 0xA0) return false;
            if (*s == 0xED && s[1] >= 0xA0) return false;
            s += 3;
        } else if ((*s & 0xF8) == 0xF0) {
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return false;
            if (*s == 0xF0 && s[1] < 0x90) return false;
            if (*s == 0xF4 && s[1] >= 0x90) return false;
            s += 4;
        } else {
            return false;
        }
    }
    return true;
}

/* 🚀 [단일 루프 메인 파이프라인] 디코드 + 화이트리스트 + 세그먼트 동시 검증 */
static bool PathValidator_validate(PathValidator* self, const char* raw_path, char* out_canonical, size_t out_size) {
    (void)self;
    if (!raw_path || !out_canonical || out_size == 0) return false;

    size_t len = strlen(raw_path);
    if (len == 0 || len > MAX_PATH_LEN) return false; /* Rule 01: Path 길이 제한 */
    if (raw_path[0] != '/') return false;             /* 관찰 1: 절대 경로 선행 '/' 강제 */

    size_t out_idx = 0;
    int seg_count = 0;
    int seg_len = 0;

    for (size_t i = 0; i < len; ) {
        unsigned char c = (unsigned char)raw_path[i];
        bool was_encoded = false;

        /* Rule 03, 04: Percent Encoding 검사 및 단일 디코딩 */
        if (c == '%') {
            if (i + 2 >= len) return false;
            /* UB 방지를 위한 (unsigned char) 명시적 캐스팅 */
            if (!isxdigit((unsigned char)raw_path[i+1]) || !isxdigit((unsigned char)raw_path[i+2])) return false;
            char hex[3] = { raw_path[i+1], raw_path[i+2], '\0' };
            c = (unsigned char)strtol(hex, NULL, 16);
            was_encoded = true;
            i += 3;
        } else {
            i++;
        }

        /* Rule 08, 09, 10, 16: NULL, Control, Backslash, Reserved Chars 차단 */
        if (c == '\0' || c == '\\' || c < 32 || c == 127) return false;
        if (c == '?' || c == '#' || c == '&' || c == '=') return false;

        /* 필수 2: %2F 디코드 슬래시 밀수(Path Smuggling) 완벽 차단 */
        if (was_encoded && c == '/') return false;

        /* Rule 12, 15: 명시적 화이트리스트 [A-Za-z0-9_.-] + / + ~ 및 UTF-8 바이트 허용 */
        bool is_alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        bool is_safe_punct = (c == '-' || c == '_' || c == '.' || c == '~' || c == '/');
        if (!is_alnum && !is_safe_punct && c < 0x80) return false;

        /* 세그먼트 정규화 및 길이 평가 */
        if (c == '/') {
            /* 필수 1: 중복 검사 이전에 '.' 및 '..'을 먼저 평가하여 정규화 수행 */
            if (seg_len == 1 && out_idx > 0 && out_canonical[out_idx - 1] == '.') {
                out_idx--; /* Rule 06: "." 정규화 (삭제) */
            } else if (seg_len == 2 && out_idx > 1 && out_canonical[out_idx - 2] == '.' && out_canonical[out_idx - 1] == '.') {
                return false; /* Rule 05: ".." Traversal 차단 */
            } else if (seg_len > 0) {
                seg_count++;
                if (seg_count > MAX_SEGMENTS) return false; /* OOM 방지: 배열 삽입 전 선행 검사 */
            }

            /* 평가로 인해 out_idx가 조정된 상태에서, 직전 문자가 '/'라면 기록 생략 (중복 슬래시 제조 방지) */
            if (out_idx > 0 && out_canonical[out_idx - 1] == '/') {
                seg_len = 0;
                continue;
            }

            /* 버퍼 오버플로 방지 */
            if (out_idx >= out_size - 1) return false;
            out_canonical[out_idx++] = '/';
            seg_len = 0;
        } else {
            seg_len++;
            if (seg_len > MAX_SEGMENT_LEN) return false; /* Rule 11: Segment 길이 제한 */

            /* 버퍼 오버플로 방지 */
            if (out_idx >= out_size - 1) return false;
            out_canonical[out_idx++] = (char)c;
        }
    }

    /* 루프 종료 후 마지막 Trailing Segment 평가 (루프 내부 로직과 동일) */
    if (seg_len == 1 && out_idx > 0 && out_canonical[out_idx - 1] == '.') {
        out_idx--;
    } else if (seg_len == 2 && out_idx > 1 && out_canonical[out_idx - 2] == '.' && out_canonical[out_idx - 1] == '.') {
        return false;
    } else if (seg_len > 0) {
        seg_count++;
        if (seg_count > MAX_SEGMENTS) return false;
    }

    /* 관찰 2: Trailing Slash Policy - 루트("/")를 제외한 끝 슬래시는 모두 제거 */
    if (out_idx > 1 && out_canonical[out_idx - 1] == '/') {
        out_idx--;
    }

    /* 혹시라도 out_idx가 0까지 밀렸다면(예: "/." 입력) 루트 "/" 복구 */
    if (out_idx == 0) {
        if (out_size < 2) return false;
        out_canonical[out_idx++] = '/';
    }
    out_canonical[out_idx] = '\0';

    /* Rule 02: 디코딩 및 정규화가 완료된 버퍼에 대해 UTF-8 최종 유효성 검사 */
    if (!rule_02_check_utf8((const unsigned char*)out_canonical)) return false;

    return true;
}

/* =========================================================
 * 📦 [생성자 및 소멸자]
 * ========================================================= */
static void PathValidator_finalize(Object* obj) {
    (void)obj;
}

static const Class _PathValidator_Class = {
    .name = "PathValidator",
    .size = sizeof(PathValidator),
    .finalize = PathValidator_finalize
};

PathValidator* new_PathValidator(void) {
    PathValidator* self = (PathValidator*)calloc(1, sizeof(PathValidator));
    if (!self) return NULL;

    Object_Init((Object*)self, &_PathValidator_Class);
    self->validate = PathValidator_validate;

    return self;
}