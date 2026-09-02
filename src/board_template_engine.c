/*
 * template_engine.c
 * libcore SSR (v1.7.2 Final Mustache Subset)
 * 🌿 Eye-Care Mode: 1 Line = 1 Statement
 * 🛡️ json_as_value/json_as_node 공식 API만 사용
 *    ObjectMapper 미사용 / logger 미사용
 */

#include "board_template_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── 1. TplBuilder ── */
typedef struct {
    char*  buf;
    size_t len;
    size_t cap;
    int    failed;
} TplBuilder;

static void tb_init(TplBuilder* tb) {
    tb->cap    = 4096;
    tb->len    = 0;
    tb->failed = 0;
    tb->buf    = (char*)malloc(tb->cap);

    if (!tb->buf) {
        tb->failed = 1;
    } else {
        tb->buf[0] = '\0';
    }
}

static void tb_append(TplBuilder* tb, const char* str, size_t slen) {
    if (tb->failed || !str || slen == 0) {
        return;
    }

    if (tb->len + slen + 1 > tb->cap) {
        size_t new_cap = tb->cap * 2;

        while (new_cap < tb->len + slen + 1) {
            new_cap *= 2;
        }

        char* tmp = (char*)realloc(tb->buf, new_cap);

        if (!tmp) {
            tb->failed = 1;
            return;
        }

        tb->buf = tmp;
        tb->cap = new_cap;
    }

    memcpy(tb->buf + tb->len, str, slen);
    tb->len       += slen;
    tb->buf[tb->len] = '\0';
}

/* 🚨 XSS 방어 + embedded NUL 처리 */
static void tb_append_escaped_len(TplBuilder* tb, const char* str, size_t len) {
    if (!tb || !str) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)str[i];

        switch (ch) {
            case '&':  tb_append(tb, "&amp;",  5); break;
            case '<':  tb_append(tb, "&lt;",   4); break;
            case '>':  tb_append(tb, "&gt;",   4); break;
            case '"':  tb_append(tb, "&quot;", 6); break;
            case '\'': tb_append(tb, "&#39;",  5); break;
            case '\0': tb_append(tb, "&#0;",   4); break;
            default:   tb_append(tb, (const char*)&str[i], 1); break;
        }
    }
}

static char* tb_finish(TplBuilder* tb) {
    if (tb->failed) {
        free(tb->buf);
        return NULL;
    }

    return tb->buf;
}

/* ── 2. 유틸 ── */
static void trim_key(char* key) {
    char* p = key;
    char* l = key + strlen(key);

    while (isspace((unsigned char)*p)) {
        p++;
    }

    while (l > p && isspace((unsigned char)*(l - 1))) {
        l--;
    }

    *l = '\0';

    if (p > key) {
        memmove(key, p, (size_t)(l - p) + 1);
    }
}

/* ── 3. scalar 렌더링 ── */
static void TemplateEngine_renderValue(Object* obj, TplBuilder* tb) {
    JsonValue* value = json_as_value(obj);

    if (!value) {
        return;
    }

    switch (value->type) {
        case J_STRING:
            if (value->string) {
                tb_append_escaped_len(tb, value->string, value->string_exact_size);
            }
            break;

        case J_NUMBER: {
            char num_buf[64];
            int  n = snprintf(num_buf, sizeof(num_buf), "%g", value->number);

            if (n < 0 || (size_t)n >= sizeof(num_buf)) {
                tb->failed = 1;
                return;
            }

            tb_append(tb, num_buf, (size_t)n);
            break;
        }

        case J_BOOL:
            if (value->boolean) {
                tb_append(tb, "true",  4);
            } else {
                tb_append(tb, "false", 5);
            }
            break;

        case J_NULL:
            /* Mustache subset: null → 빈 문자열 */
            break;
    }
}

/* ── 4. section 닫기 탐색 ── */
static const char* TemplateEngine_findSectionEnd(
    const char* start,
    const char* end,
    const char* key)
{
    char open_tag [128];
    char close_tag[128];

    snprintf(open_tag,  sizeof(open_tag),  "{{#%s}}", key);
    snprintf(close_tag, sizeof(close_tag), "{{/%s}}", key);

    size_t open_len  = strlen(open_tag);
    size_t close_len = strlen(close_tag);
    int    depth     = 0;
    const char* p    = start;

    while (p < end) {
        const char* next_open  = strstr(p, open_tag);
        const char* next_close = strstr(p, close_tag);

        if (!next_close || next_close >= end) {
            return NULL;
        }

        if (next_open && next_open < next_close && next_open < end) {
            depth++;
            p = next_open + open_len;
        } else {
            if (depth == 0) {
                return next_close;
            }

            depth--;
            p = next_close + close_len;
        }
    }

    return NULL;
}

/* ── 5. 핵심 렌더러 ── */
static void TemplateEngine_renderRange(TemplateEngine* self,const char* start,const char* end,JSONNode* ctx,TplBuilder* tb){
    const char* p = start;

    while (p < end && !tb->failed) {
        const char* tag_start = strstr(p, "{{");

        if (!tag_start || tag_start >= end) {
            tb_append(tb, p, (size_t)(end - p));
            break;
        }

        tb_append(tb, p, (size_t)(tag_start - p));
        p = tag_start + 2;

        const char* tag_end = strstr(p, "}}");

        if (!tag_end || tag_end >= end) {
            tb_append(tb, "{{", 2);
            continue;
        }

        int is_section = (*p == '#');

        if (is_section) {
            p++;
        }

        /* 닫는 태그 처리 — 그냥 스킵 */
        if (*p == '/') {
            p = tag_end + 2;
            continue;
        }

        char   key[128] = {0};
        size_t key_len  = (size_t)(tag_end - p);

        if (key_len >= sizeof(key)) {
            key_len = sizeof(key) - 1;
        }

        strncpy(key, p, key_len);
        trim_key(key);

        p = tag_end + 2;

        if (is_section) {
            /* ── Array loop ── */
            const char* section_end =
                TemplateEngine_findSectionEnd(p, end, key);

            if (!section_end) {
                tb->failed = 1;
                return;
            }

            if (ctx && ctx->get) {
                Object*  section_obj = ctx->get(ctx, key);
                JSONNode* array      = json_as_node(section_obj);

                if (array && array->isArray && array->isArray(array)) {
                    int count = array->length(array);

                    for (int i = 0; i < count; i++) {
                        Object*   item_obj = array->getIndex(array, i);
                        JSONNode* item     = json_as_node(item_obj);

                        if (item && item->isObject && item->isObject(item)) {
                            TemplateEngine_renderRange(self, p, section_end, item, tb);

                            if (tb->failed) {
                                return;
                            }
                        }
                    }
                }
            }

            char   close_tag[128];
            int    n = snprintf(close_tag, sizeof(close_tag), "{{/%s}}", key);

            if (n < 0 || (size_t)n >= sizeof(close_tag)) {
                tb->failed = 1;
                return;
            }

            p = section_end + (size_t)n;

        } else {
            /* ── Scalar 치환 ── */
            if (ctx && ctx->get) {
                Object* val = ctx->get(ctx, key);
                TemplateEngine_renderValue(val, tb);
            }
        }
    }
}

/* ── 6. render() ── */
static char* TemplateEngine_render(TemplateEngine* self,const char*     template_text,JSONNode* ctx){
    if (!self || !template_text) {
        return NULL;
    }

    TplBuilder tb;
    tb_init(&tb);

    size_t len = strlen(template_text);
    TemplateEngine_renderRange(self, template_text, template_text + len, ctx, &tb);

    return tb_finish(&tb);
}

/* ── 7. renderFile() ── */
static char* TemplateEngine_renderFile(TemplateEngine* self,const char* file_path,JSONNode* ctx){
    if (!self || !file_path) {
        return NULL;
    }

    FILE* fp = fopen(file_path, "rb");

    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);

    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    char* buf = (char*)malloc((size_t)size + 1);

    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    if (read_bytes != (size_t)size) {
        free(buf);
        return NULL;
    }

    buf[size] = '\0';

    char* result = self->render(self, buf, ctx);
    free(buf);

    return result;
}

/* ── ARC ── */
static void TemplateEngine_finalize(Object* obj) {
    (void)obj;
}

static const Class _TemplateEngine_Class = {
    .name     = "TemplateEngine",
    .size     = sizeof(TemplateEngine),
    .finalize = TemplateEngine_finalize
};

TemplateEngine* new_TemplateEngine(void) {
    TemplateEngine* self = (TemplateEngine*)calloc(1, sizeof(TemplateEngine));

    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &_TemplateEngine_Class);

    self->render     = TemplateEngine_render;
    self->renderFile = TemplateEngine_renderFile;

    return self;
}