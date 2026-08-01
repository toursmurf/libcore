#define _GNU_SOURCE   /* memmem, strcasestr */
#include "multipart_parser.h"
#include "string_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================
 * 내부 유틸
 * ========================================================= */

/* Content-Type 헤더에서 boundary= 값 추출 */
int Multipart_extract_boundary(const char* ct_header,
                                char* out_buf, size_t out_size) {
    if (!ct_header || !out_buf || out_size == 0) return -1;

    const char* p = strcasestr(ct_header, "boundary=");
    if (!p) return -1;
    p += 9; /* "boundary=" 길이 */

    /* 선택적 따옴표 제거 */
    if (*p == '"') p++;

    const char* end = p;
    while (*end && *end != '"' && *end != ';' &&
           *end != ' '  && *end != '\r' && *end != '\n')
        end++;

    size_t len = (size_t)(end - p);
    if (len == 0 || len >= out_size) return -1;

    memcpy(out_buf, p, len);
    out_buf[len] = '\0';
    return (int)len;
}

/* 헤더 블록에서 Content-Disposition 파라미터 추출
 *   param = "name" → 필드 이름
 *   param = "filename" → 파일 이름
 *   반환값: malloc 문자열 (없으면 NULL) */
static char* extract_param(const char* header_block, const char* param) {
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", param);

    const char* search = header_block;
    const char* p = NULL;

    /* 선행 문자 검사: ';' 또는 공백 또는 블록 시작이어야 오매칭 방지
     * "filename=" 검색 시 "name=" 이 중간에 매칭되지 않도록 */
    while ((search = strcasestr(search, needle)) != NULL) {
        if (search == header_block ||
            search[-1] == ';' || search[-1] == ' ' || search[-1] == '	') {
            p = search + strlen(needle);
            break;
        }
        search++; /* 이 위치는 오매칭 — 다음 위치부터 재시도 */
    }
    if (!p) return NULL;

    bool quoted = (*p == '"');
    if (quoted) p++;

    const char* end = p;
    if (quoted) {
        while (*end && *end != '"') end++;
    } else {
        while (*end && *end != ';' && *end != '\r' &&
               *end != '\n' && *end != ' ') end++;
    }

    size_t len = (size_t)(end - p);
    if (len == 0) return NULL;

    char* val = (char*)malloc(len + 1);
    if (!val) return NULL;
    memcpy(val, p, len);
    val[len] = '\0';
    return val;
}

/* 헤더 블록에서 Content-Type 값 추출
 *   e.g. "Content-Type: image/jpeg" → "image/jpeg" */
static char* extract_content_type(const char* header_block) {
    const char* p = strcasestr(header_block, "Content-Type:");
    if (!p) return NULL;
    p += 13;
    while (*p == ' ' || *p == '\t') p++;

    const char* end = p;
    while (*end && *end != '\r' && *end != '\n') end++;

    size_t len = (size_t)(end - p);
    if (len == 0) return NULL;

    char* ct = (char*)malloc(len + 1);
    if (!ct) return NULL;
    memcpy(ct, p, len);
    ct[len] = '\0';
    return ct;
}

/* =========================================================
 * ARC 클래스 정의
 * ========================================================= */

static void MultipartResult_finalize(Object* obj) {
    MultipartResult* self = (MultipartResult*)obj;
    if (self->fields) RELEASE((Object*)self->fields);
    if (self->files)  RELEASE((Object*)self->files);
}

static const Class _MultipartResult_Class = {
    .name     = "MultipartResult",
    .size     = sizeof(MultipartResult),
    .finalize = MultipartResult_finalize
};

static MultipartResult* new_MultipartResult(void) {
    MultipartResult* self =
        (MultipartResult*)calloc(1, sizeof(MultipartResult));
    if (!self) return NULL;
    Object_Init((Object*)self, &_MultipartResult_Class);

    self->fields = new_HashMap(8);
    self->files  = new_HashMap(4);
    if (!self->fields || !self->files) {
        RELEASE((Object*)self);
        return NULL;
    }
    return self;
}

/* =========================================================
 * 파트 처리 — 헤더/바디 분리 후 field / file 분류
 * ========================================================= */
static void process_part(MultipartResult* mp,
                         const char* part_start, size_t part_len) {
    /* 파트 내 헤더/바디 경계: \r\n\r\n */
    const char* sep = (const char*)memmem(part_start, part_len,
                                           "\r\n\r\n", 4);
    if (!sep) return;

    size_t      hdr_len    = (size_t)(sep - part_start);
    const char* body_start = sep + 4;
    size_t      body_len   = part_len - hdr_len - 4;

    /* 헤더 블록 NUL 종단 복사 */
    char* hdr_buf = (char*)malloc(hdr_len + 1);
    if (!hdr_buf) return;
    memcpy(hdr_buf, part_start, hdr_len);
    hdr_buf[hdr_len] = '\0';

    char* name     = extract_param(hdr_buf, "name");
    char* filename = extract_param(hdr_buf, "filename");
    char* ct       = extract_content_type(hdr_buf);
    free(hdr_buf);

    if (!name) {
        free(filename);
        free(ct);
        return; /* name 없는 파트는 무시 */
    }

    if (filename) {
        /* ── 파일 파트 ── */
        /* content_type 없으면 application/octet-stream */
        const char* mime = ct ? ct : "application/octet-stream";

        HttpMultipartFile* f =
            new_HttpMultipartFile(filename, mime, body_start, body_len);
        if (f) {
            mp->files->put(mp->files, name, (Object*)f);
            RELEASE((Object*)f); /* HashMap이 RETAIN 했으므로 여기서 해제 */
        }
    } else {
        /* ── 텍스트 필드 파트 ── */
        char* val = (char*)malloc(body_len + 1);
        if (val) {
            memcpy(val, body_start, body_len);
            val[body_len] = '\0';

            String* s = new_String(val);
            free(val);
            if (s) {
                mp->fields->put(mp->fields, name, (Object*)s);
                RELEASE((Object*)s);
            }
        }
    }

    free(name);
    free(filename);
    free(ct);
}

/* =========================================================
 * 공개 API
 * ========================================================= */

MultipartResult* Multipart_parse(const void* body, size_t body_len,
                                 const char* boundary) {
    if (!body || body_len == 0 || !boundary || boundary[0] == '\0')
        return NULL;

    /* ── 구분자 2종 준비 ──────────────────────────────────────
     * bare_delim : "--boundary"       (첫 경계 탐색용 — RFC상 앞 CRLF 없음)
     * crlf_delim : "\r\n--boundary"  (2번째~ 경계 탐색용 — 바이너리 오분할 방지)
     * RFC 2046: 경계 구분자 앞에는 반드시 CRLF — 바이너리 내부의
     *           우연한 "--boundary" 문자열을 경계로 오인하지 않음 */
    size_t bnd_len        = strlen(boundary);
    size_t bare_len       = bnd_len + 2;          /* "--" + boundary */
    size_t crlf_len       = bnd_len + 4;          /* "\r\n--" + boundary */

    char* bare_delim = (char*)malloc(bare_len + 1);
    char* crlf_delim = (char*)malloc(crlf_len + 1);
    if (!bare_delim || !crlf_delim) {
        free(bare_delim); free(crlf_delim);
        return NULL;
    }
    snprintf(bare_delim, bare_len + 1, "--%s", boundary);
    snprintf(crlf_delim, crlf_len + 1, "\r\n--%s", boundary);

    MultipartResult* mp = new_MultipartResult();
    if (!mp) { free(bare_delim); free(crlf_delim); return NULL; }

    const char* buf = (const char*)body;
    const char* end = buf + body_len;
    const char* cur = buf;
    bool first = true;  /* 첫 경계는 bare_delim 으로 탐색 */

    while (cur < end) {
        const char* hit;
        size_t      hit_len;

        if (first) {
            /* 첫 경계: bare "--boundary" 탐색 */
            hit     = (const char*)memmem(cur, (size_t)(end - cur),
                                           bare_delim, bare_len);
            hit_len = bare_len;
            first   = false;
        } else {
            /* 이후 경계: "\r\n--boundary" 앵커 탐색 — 바이너리 오분할 차단 */
            hit     = (const char*)memmem(cur, (size_t)(end - cur),
                                           crlf_delim, crlf_len);
            hit_len = crlf_len;
        }
        if (!hit) break;

        const char* after = hit + hit_len;

        /* 종료 구분자: "--boundary--" */
        if (after + 2 <= end && after[0] == '-' && after[1] == '-')
            break;

        /* \r\n 건너뜀 (파트 헤더 시작) */
        if (after + 2 <= end && after[0] == '\r' && after[1] == '\n')
            after += 2;
        else if (after < end && after[0] == '\n')
            after += 1;

        /* 다음 경계: "\r\n--boundary" 앵커로 탐색 */
        const char* next = (const char*)memmem(
            after, (size_t)(end - after), crlf_delim, crlf_len);
        if (!next) break;

        /* 파트 끝 = next (\r\n 은 crlf_delim에 이미 포함되므로 제거 불필요) */
        if (next > after)
            process_part(mp, after, (size_t)(next - after));

        cur = next;
    }

    free(bare_delim);
    free(crlf_delim);
    return mp;
}

const char* MultipartResult_get_field(const MultipartResult* self,
                                      const char* name) {
    if (!self || !name || !self->fields) return NULL;
    String* s = (String*)self->fields->get(self->fields, name);
    return s ? s->c_str(s) : NULL;
}

HttpMultipartFile* MultipartResult_get_file(const MultipartResult* self,
                                            const char* name) {
    if (!self || !name || !self->files) return NULL;
    return (HttpMultipartFile*)self->files->get(self->files, name);
}
