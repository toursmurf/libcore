#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <openssl/crypto.h>
#include "json.h"
#include "hashmap.h"
#include "arraylist.h"

#define MAX_JSON_DEPTH 64
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

extern const Class jsonNodeClass; // VTable 전방 선언

static inline int is_json_value(Object *obj) {
    return (obj && GET_CLASS(obj) == &jsonValueClass);
}

static inline int is_hashmap(Object *obj) {
    return (obj && GET_CLASS(obj) == &hashMapClass);
}

static inline int is_arraylist(Object *obj) {
    return (obj && GET_CLASS(obj) == &arrayListClass);
}

static inline int is_json_node(Object *obj) {
    return (obj && GET_CLASS(obj) == &jsonNodeClass);
}

/* =================================================================
 * [0] ParseContext 및 에러 추적 시스템
 * ================================================================= */
typedef struct {
    const char *json_start;
    const char *ptr;
    int depth;
    char *err_buf;
    size_t err_len;
    int has_error;
} ParseContext;

static void report_error(ParseContext *ctx, const char *msg) {
    if (ctx->has_error) {
        return;
    }

    ctx->has_error = 1;

    int line = 1;
    int col = 1;

    for (const char *p = ctx->json_start; p < ctx->ptr; p++) {
        if (*p == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }

    if (ctx->err_buf && ctx->err_len > 0) {
        snprintf(ctx->err_buf, ctx->err_len, "[Line %d, Col %d] %s", line, col, msg);
    }
}

/* =================================================================
 * [Internal] StringBuilder & Helpers
 * ================================================================= */
typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
    int failed;
} StringBuilder;

static void sb_init(StringBuilder *sb) {
    sb->capacity = 256;
    sb->length = 0;
    sb->failed = 0;

    sb->buffer = (char*)malloc(sb->capacity);

    if (!sb->buffer) {
        sb->failed = 1;
    } else {
        sb->buffer[0] = '\0';
    }
}

static void sb_append_char(StringBuilder *sb, char c) {
    if (sb->failed) {
        return;
    }

    if (sb->length + 2 > sb->capacity) {
        size_t new_cap = sb->capacity * 2;
        char *tmp = (char*)realloc(sb->buffer, new_cap);

        if (!tmp) {
            sb->failed = 1;
            return;
        }

        sb->buffer = tmp;
        sb->capacity = new_cap;
    }

    sb->buffer[sb->length++] = c;
    sb->buffer[sb->length] = '\0';
}

static void sb_append(StringBuilder *sb, const char *str) {
    if (sb->failed) {
        return;
    }

    if (!str || !sb->buffer) {
        return;
    }

    size_t str_len = strlen(str);

    if (sb->length + str_len + 1 > sb->capacity) {
        size_t new_cap = sb->capacity;

        while (sb->length + str_len + 1 > new_cap) {
            new_cap *= 2;
        }

        char *tmp = (char*)realloc(sb->buffer, new_cap);

        if (!tmp) {
            sb->failed = 1;
            return;
        }

        sb->buffer = tmp;
        sb->capacity = new_cap;
    }

    memcpy(sb->buffer + sb->length, str, str_len);
    sb->length += str_len;
    sb->buffer[sb->length] = '\0';
}

static void sb_append_escaped(StringBuilder *sb, const char *str) {
    if (sb->failed) {
        return;
    }

    if (!str) {
        return;
    }

    sb_append_char(sb, '\"');

    while (*str) {
        switch (*str) {
            case '\"':
              sb_append(sb, "\\\"");
              break;
            case '\\':
              sb_append(sb, "\\\\");
              break;
            case '\b':
              sb_append(sb, "\\b");
              break;
            case '\f':
              sb_append(sb, "\\f");
              break;
            case '\n':
              sb_append(sb, "\\n");
              break;
            case '\r':
              sb_append(sb, "\\r");
              break;
            case '\t':
              sb_append(sb, "\\t");
              break;
            default:
              sb_append_char(sb, *str);
              break;
        }
        str++;
    }

    sb_append_char(sb, '\"');
}

static char* sb_finish(StringBuilder *sb) {
    if (sb->failed) {
        if (sb->buffer) {
            free(sb->buffer);
        }
        return NULL;
    }

    return sb->buffer;
}

/* =================================================================
 * [1] JsonValue Implementation
 * ================================================================= */
static void JsonValue_Finalize(Object *self) {
    JsonValue *v = (JsonValue*)self;

    if (v->type == J_STRING && v->string) {
        if (v->string_exact_size > 0) {
            OPENSSL_cleanse(v->string, v->string_exact_size);
        }
        free(v->string);
    }
}

static void JsonValue_ToString(Object *self, char *buf, size_t len) {
    JsonValue *v = (JsonValue*)self;

    switch(v->type) {
        case J_STRING:
          snprintf(buf, len, "\"%s\"", v->string);
          break;
        case J_NUMBER:
          snprintf(buf, len, "%g", v->number);
          break;
        case J_BOOL:
          snprintf(buf, len, "%s", v->boolean ? "true" : "false");
          break;
        case J_NULL:
          snprintf(buf, len, "null");
          break;
    }
}

const Class jsonValueClass = {
    .name = "JsonValue",
    .size = sizeof(JsonValue),
    .toString = JsonValue_ToString,
    .finalize = JsonValue_Finalize
};

JsonValue* new_json_string(const char *s) {
    JsonValue *v = (JsonValue*)calloc(1, sizeof(JsonValue));

    if (!v) {
        return NULL;
    }

    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_STRING;
    v->string = strdup(s ? s : "");

    if (!v->string) {
        free(v);
        return NULL;
    }

    v->string_exact_size = strlen(v->string);

    return v;
}

JsonValue* new_json_string_exact(char *s, size_t exact_size) {
    JsonValue *v = (JsonValue*)calloc(1, sizeof(JsonValue));
    if (!v) return NULL;

    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_STRING;
    v->string = s; // 소유권 인계
    v->string_exact_size = exact_size;

    return v;
}

JsonValue* new_json_number(double d) {
    JsonValue *v = (JsonValue*)calloc(1, sizeof(JsonValue));

    if (!v) {
        return NULL;
    }

    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_NUMBER;
    v->number = d;

    return v;
}

JsonValue* new_json_bool(int b) {
    JsonValue *v = (JsonValue*)calloc(1, sizeof(JsonValue));

    if (!v) {
        return NULL;
    }

    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_BOOL;
    v->boolean = b;

    return v;
}

JsonValue* new_json_null(void) {
    JsonValue *v = (JsonValue*)calloc(1, sizeof(JsonValue));

    if (!v) {
        return NULL;
    }

    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_NULL;

    return v;
}

/* =================================================================
 * [2] Tree Equality
 * ================================================================= */
static int impl_json_equals(Object *o1, Object *o2) {
    if (o1 == o2) return 1;
    if (!o1 || !o2) return 0;
    if (GET_CLASS(o1) != GET_CLASS(o2)) return 0;

    if (is_json_value(o1)) {
        JsonValue *v1 = (JsonValue*)o1;
        JsonValue *v2 = (JsonValue*)o2;

        if (v1->type != v2->type)
          return 0;
        if (v1->type == J_NULL)
          return 1;
        if (v1->type == J_BOOL)
          return v1->boolean == v2->boolean;
        if (v1->type == J_NUMBER)
          return (v1->number == v2->number);
        if (v1->type == J_STRING) {
            if (!v1->string || !v2->string)
              return v1->string == v2->string;
            return strcmp(v1->string, v2->string) == 0;
        }
    } else if (is_arraylist(o1)) {
        ArrayList *l1 = (ArrayList*)o1;
        ArrayList *l2 = (ArrayList*)o2;

        if (l1->getSize(l1) != l2->getSize(l2)) return 0;

        for (int i = 0; i < l1->getSize(l1); i++) {
            if (!impl_json_equals(l1->get(l1, i), l2->get(l2, i))) return 0;
        }
        return 1;
    } else if (is_hashmap(o1)) {
        HashMap *m1 = (HashMap*)o1;
        HashMap *m2 = (HashMap*)o2;
        int count1 = 0, count2 = 0;

        for (int i = 0; i < m1->capacity; i++) {
            HashNode *n = m1->buckets[i];
            while (n) {
              count1++;
              n = n->next;
            }
        }

        for (int i = 0; i < m2->capacity; i++) {
            HashNode *n = m2->buckets[i];
            while (n) {
              count2++;
              n = n->next;
            }
        }

        if (count1 != count2)
          return 0;

        for (int i = 0; i < m1->capacity; i++) {
            HashNode *n = m1->buckets[i];
            while (n) {
                Object *val2 = m2->get(m2, n->key);
                if (!val2 || !impl_json_equals(n->value, val2)) return 0;
                n = n->next;
            }
        }
        return 1;
    }

    return 0;
}

/* =================================================================
 * [3] Core Parsing Engine
 * ================================================================= */
static void skip_ws(ParseContext *ctx) {
    while (*(ctx->ptr) && isspace((unsigned char)*(ctx->ptr))) {
        ctx->ptr++;
    }
}

static int is_valid_boundary(char c) {
    return (isspace((unsigned char)c) || c == ',' || c == '}' || c == ']' || c == '\0');
}

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Object* parse_value(ParseContext *ctx);

static char* parse_string_raw(ParseContext *ctx, size_t *out_written) {
    const char *start = ctx->ptr + 1;
    const char *end = start;

    while (*end && *end != '\"') {
        if (*end == '\\' && *(end+1)) {
            end += 2;
        } else {
            if ((unsigned char)*end <= 0x1F) {
                ctx->ptr = end;
                report_error(ctx, "Unescaped control character in string!");
                return NULL;
            }
            end++;
        }
    }

    if (*end != '\"') {
        ctx->ptr = end;
        report_error(ctx, "Unterminated string!");
        return NULL;
    }

    int len = (int)(end - start);
    size_t alloc_size = (size_t)len * 3 + 1;
    char *str = (char*)malloc(alloc_size);

    if (!str) {
        ctx->ptr = end + 1;
        report_error(ctx, "OOM in parse_string_raw buffer allocation!");
        return NULL;
    }

    const char *src = start;
    char *dst = str;

    while (src < end) {
        if (*src == '\\') {
            src++;

            if (*src == 'u') {
                src++;
                int cp = 0;
                int valid = 1;

                for (int i = 0; i < 4; i++) {
                    if (src >= end) { valid = 0; break; }
                    int h = hex_to_int(*src);
                    if (h < 0) { valid = 0; break; }
                    cp = (cp << 4) | h;
                    src++;
                }

                if (!valid) {
                    ctx->ptr = src;
                    report_error(ctx, "Invalid unicode escape!");
                    OPENSSL_cleanse(str, alloc_size);
                    free(str);
                    return NULL;
                }

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (src < end - 5 && *src == '\\' && *(src+1) == 'u') {
                        src += 2;
                        int cp2 = 0;
                        int valid2 = 1;

                        for (int i = 0; i < 4; i++) {
                            int h = hex_to_int(*src);
                            if (h < 0) { valid2 = 0; break; }
                            cp2 = (cp2 << 4) | h;
                            src++;
                        }

                        if (valid2 && cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                            cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                        } else {
                            ctx->ptr = src;
                            report_error(ctx, "Invalid low surrogate pair!");
                            OPENSSL_cleanse(str, alloc_size);
                            free(str);
                            return NULL;
                        }
                    } else {
                        ctx->ptr = src;
                        report_error(ctx, "Missing low surrogate pair!");
                        OPENSSL_cleanse(str, alloc_size);
                        free(str);
                        return NULL;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    ctx->ptr = src;
                    report_error(ctx, "Isolated low surrogate!");
                    OPENSSL_cleanse(str, alloc_size);
                    free(str);
                    return NULL;
                }

                if (cp <= 0x7F) {
                    *dst++ = (char)cp;
                } else if (cp <= 0x7FF) {
                    *dst++ = (char)(0xC0 | (cp >> 6));
                    *dst++ = (char)(0x80 | (cp & 0x3F));
                } else if (cp <= 0xFFFF) {
                    *dst++ = (char)(0xE0 | (cp >> 12));
                    *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                    *dst++ = (char)(0x80 | (cp & 0x3F));
                } else {
                    *dst++ = (char)(0xF0 | (cp >> 18));
                    *dst++ = (char)(0x80 | ((cp >> 12) & 0x3F));
                    *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                    *dst++ = (char)(0x80 | (cp & 0x3F));
                }
                continue;
            }

            switch (*src) {
                case '\"':
                  *dst++ = '\"';
                  break;
                case '\\':
                  *dst++ = '\\';
                  break;
                case '/':
                  *dst++ = '/';
                  break;
                case 'b':
                  *dst++ = '\b';
                  break;
                case 'f':
                  *dst++ = '\f';
                  break;
                case 'n':
                  *dst++ = '\n';
                  break;
                case 'r':
                  *dst++ = '\r';
                  break;
                case 't':
                  *dst++ = '\t';
                  break;
                default:
                  *dst++ = *src;
                  break;
            }
            src++;
        } else {
            unsigned char byte1 = (unsigned char)*src;

            if (byte1 >= 0x80) {
                int expected_len = 0;

                if ((byte1 & 0xE0) == 0xC0) {
                    expected_len = 2;

                    if (src + 1 < end) {
                        if (byte1 == 0xC0 || byte1 == 0xC1) {
                            ctx->ptr = src;
                            report_error(ctx, "Overlong UTF-8 sequence detected!");
                            OPENSSL_cleanse(str, alloc_size);
                            free(str);
                            return NULL;
                        }
                    }
                } else if ((byte1 & 0xF0) == 0xE0) {
                    expected_len = 3;

                    if (src + 1 < end) {
                        unsigned char byte2 = (unsigned char)*(src + 1);

                        if (byte1 == 0xE0 && byte2 < 0xA0) {
                            ctx->ptr = src;
                            report_error(ctx, "Overlong UTF-8 sequence detected!");
                            OPENSSL_cleanse(str, alloc_size);
                            free(str);
                            return NULL;
                        }

                        if (byte1 == 0xED && byte2 >= 0xA0) {
                            ctx->ptr = src;
                            report_error(ctx, "UTF-16 surrogate directly encoded in UTF-8!");
                            OPENSSL_cleanse(str, alloc_size);
                            free(str);
                            return NULL;
                        }
                    }
                } else if ((byte1 & 0xF8) == 0xF0) {
                    expected_len = 4;

                    if (byte1 > 0xF4) {
                        ctx->ptr = src;
                        report_error(ctx, "UTF-8 sequence exceeds U+10FFFF!");
                        OPENSSL_cleanse(str, alloc_size);
                        free(str);
                        return NULL;
                    }

                    if (src + 1 < end) {
                        unsigned char byte2 = (unsigned char)*(src + 1);

                        if (byte1 == 0xF0 && byte2 < 0x90) {
                            ctx->ptr = src;
                            report_error(ctx, "Overlong UTF-8 sequence detected!");
                            OPENSSL_cleanse(str, alloc_size);
                            free(str);
                            return NULL;
                        }

                        if (byte1 == 0xF4 && byte2 >= 0x90) {
                            ctx->ptr = src;
                            report_error(ctx, "UTF-8 sequence exceeds U+10FFFF!");
                            OPENSSL_cleanse(str, alloc_size);
                            free(str);
                            return NULL;
                        }
                    }
                } else {
                    ctx->ptr = src;
                    report_error(ctx, "Invalid UTF-8 starting byte!");
                    OPENSSL_cleanse(str, alloc_size);
                    free(str);
                    return NULL;
                }

                if (src + expected_len > end) {
                    ctx->ptr = src;
                    report_error(ctx, "Incomplete UTF-8 sequence!");
                    OPENSSL_cleanse(str, alloc_size);
                    free(str);
                    return NULL;
                }

                for (int i = 1; i < expected_len; i++) {
                    if (((unsigned char)src[i] & 0xC0) != 0x80) {
                        ctx->ptr = src;
                        report_error(ctx, "Invalid UTF-8 continuation byte!");
                        OPENSSL_cleanse(str, alloc_size);
                        free(str);
                        return NULL;
                    }
                }

                for (int i = 0; i < expected_len; i++) {
                    *dst++ = *src++;
                }
                continue;
            } // if (byte1 >= 0x80) 끝

            *dst++ = *src++;
        }
    }

    *dst = '\0';
    ctx->ptr = end + 1;

    if (out_written) {
        *out_written = (size_t)(dst - str);
    }

    return str;
}

static Object* parse_number(ParseContext *ctx) {
    const char *start = ctx->ptr;

    if (*start == '-') start++;

    if (*start == '0' && isdigit((unsigned char)*(start + 1))) {
        report_error(ctx, "Leading zero not allowed in JSON number!");
        return NULL;
    }

    errno = 0;
    char *end;
    double d = strtod(ctx->ptr, &end);

    if (end == ctx->ptr) {
        report_error(ctx, "Invalid number!");
        return NULL;
    }

    /* 🚨 [치명 결함 교정] strtod가 식별한 숫자 토큰 범위 [ctx->ptr, end) 내에서만 후행 검사 수행 */
    const char* p = ctx->ptr;
    while (p < end) {
        if (*p == '.') {
            if (p + 1 >= end || !isdigit((unsigned char)*(p + 1))) {
                report_error(ctx, "Decimal point without digit!");
                return NULL;
            }
            break;
        }
        p++;
    }

    if (errno == ERANGE) {
        if (d == 0.0) errno = 0;
        else {
            report_error(ctx, "Number out of range (Overflow)!");
            return NULL;
        }
    }

    if (isinf(d) || isnan(d)) {
        report_error(ctx, "Number out of range (Infinity/NaN)!");
        return NULL;
    }

    if ((*(end - 1) == 'e' || *(end - 1) == 'E') ||
        ( (*(end - 1) == '+' || *(end - 1) == '-') && (*(end - 2) == 'e' || *(end - 2) == 'E') )) {
        report_error(ctx, "Invalid exponent format!");
        return NULL;
    }

    if (!is_valid_boundary(*end)) {
        ctx->ptr = end;
        report_error(ctx, "Invalid boundary after number!");
        return NULL;
    }

    ctx->ptr = end;
    Object *obj = (Object*)new_json_number(d);

    if (!obj) {
        report_error(ctx, "OOM creating JsonValue (number)!");
        return NULL;
    }

    return obj;
}

static Object* parse_object(ParseContext *ctx) {
    HashMap *map = new_HashMap(16);

    if (!map) {
        report_error(ctx, "OOM in Object creation!");
        return NULL;
    }

    ctx->ptr++;
    ctx->depth++;

    while (*(ctx->ptr)) {
        skip_ws(ctx);

        if (*(ctx->ptr) == '}') {
            ctx->ptr++;
            ctx->depth--;
            return (Object*)map;
        }

        if (*(ctx->ptr) != '\"') {
            report_error(ctx, "Expected string key!");
            goto fail;
        }

        size_t key_len = 0;
        char *key = parse_string_raw(ctx, &key_len);

        if (!key) {
            goto fail;
        }

        skip_ws(ctx);

        if (*(ctx->ptr) != ':') {
            report_error(ctx, "Expected ':'!");
            OPENSSL_cleanse(key, key_len + 1); /* 🚨 strlen 제거. 정확한 key_len + 1 소각 */
            free(key);
            goto fail;
        }

        ctx->ptr++;

        Object *val = parse_value(ctx);

        if (!val) {
            OPENSSL_cleanse(key, key_len + 1); /* 🚨 strlen 제거. 정확한 key_len + 1 소각 */
            free(key);
            goto fail;
        }

        map->put(map, key, val);

        RELEASE(val);
        free(key);

        skip_ws(ctx);

        if (*(ctx->ptr) == ',') {
            ctx->ptr++;
            skip_ws(ctx);

            if (*(ctx->ptr) == '}') {
                report_error(ctx, "Trailing comma in object!");
                goto fail;
            }
        } else if (*(ctx->ptr) != '}') {
            report_error(ctx, "Expected ',' or '}'!");
            goto fail;
        }
    }

    report_error(ctx, "Unterminated object!");

fail:
    ctx->depth--;
    RELEASE((Object*)map);

    return NULL;
}

static Object* parse_array(ParseContext *ctx) {
    ArrayList *list = new_ArrayList(10);

    if (!list) {
        report_error(ctx, "OOM in Array creation!");
        return NULL;
    }

    ctx->ptr++;
    ctx->depth++;

    while (*(ctx->ptr)) {
        skip_ws(ctx);

        if (*(ctx->ptr) == ']') {
            ctx->ptr++;
            ctx->depth--;
            return (Object*)list;
        }

        Object *val = parse_value(ctx);

        if (!val) {
            goto fail;
        }

        list->add(list, val);
        RELEASE(val);

        skip_ws(ctx);

        if (*(ctx->ptr) == ',') {
            ctx->ptr++;
            skip_ws(ctx);

            if (*(ctx->ptr) == ']') {
                report_error(ctx, "Trailing comma in array!");
                goto fail;
            }
        } else if (*(ctx->ptr) != ']') {
            report_error(ctx, "Expected ',' or ']'!");
            goto fail;
        }
    }

    report_error(ctx, "Unterminated array!");

fail:
    ctx->depth--;
    RELEASE((Object*)list);

    return NULL;
}

static Object* parse_value(ParseContext *ctx) {
    if (ctx->depth > MAX_JSON_DEPTH) {
        report_error(ctx, "Depth limit exceeded!");
        return NULL;
    }

    skip_ws(ctx);

    char c = *(ctx->ptr);

    if (c == '\0') return NULL;

    if (c == '\"') {
        size_t exact_len = 0;
        char *s = parse_string_raw(ctx, &exact_len);

        if (!s) return NULL;

        Object *o = (Object*)new_json_string_exact(s, exact_len);

        if (!o) {
            report_error(ctx, "OOM creating JsonValue (string)!");
            OPENSSL_cleanse(s, exact_len + 1);
            free(s);
        }

        return o;
    }

    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(ctx);
    if (c == '{') return parse_object(ctx);
    if (c == '[') return parse_array(ctx);

    if (strncmp(ctx->ptr, "true", 4) == 0 && is_valid_boundary((ctx->ptr)[4])) {
        ctx->ptr += 4;
        Object *o = (Object*)new_json_bool(1);
        if (!o) report_error(ctx, "OOM creating JsonValue (bool)!");
        return o;
    }

    if (strncmp(ctx->ptr, "false", 5) == 0 && is_valid_boundary((ctx->ptr)[5])) {
        ctx->ptr += 5;
        Object *o = (Object*)new_json_bool(0);
        if (!o) report_error(ctx, "OOM creating JsonValue (bool)!");
        return o;
    }

    if (strncmp(ctx->ptr, "null", 4) == 0 && is_valid_boundary((ctx->ptr)[4])) {
        ctx->ptr += 4;
        Object *o = (Object*)new_json_null();
        if (!o) report_error(ctx, "OOM creating JsonValue (null)!");
        return o;
    }

    report_error(ctx, "Unexpected token!");
    return NULL;
}

/* =================================================================
 * [4] Stringify Engine
 * ================================================================= */
static void stringify_recursive(Object *obj, StringBuilder *sb, int depth) {
    if (sb->failed) return;

    if (depth > MAX_JSON_DEPTH) {
        sb_append(sb, "\"#DEPTH_LIMIT_EXCEEDED#\"");
        return;
    }

    if (!obj) {
        sb_append(sb, "null");
        return;
    }

    if (is_json_value(obj)) {
        JsonValue *jv = (JsonValue*)obj;

        if (jv->type == J_STRING) sb_append_escaped(sb, jv->string);
        else {
            char buf[64];
            GET_CLASS(obj)->toString(obj, buf, sizeof(buf));
            sb_append(sb, buf);
        }
    } else if (is_json_node(obj)) {
        stringify_recursive(((JSONNode*)obj)->core_data, sb, depth + 1);
    } else if (is_arraylist(obj)) {
        ArrayList *list = (ArrayList*)obj;
        sb_append_char(sb, '[');

        for (int i = 0; i < list->getSize(list); i++) {
            if (i > 0) sb_append_char(sb, ',');
            stringify_recursive(list->get(list, i), sb, depth + 1);
        }
        sb_append_char(sb, ']');
    } else if (is_hashmap(obj)) {
        HashMap *map = (HashMap*)obj;
        sb_append_char(sb, '{');
        int first = 1;

        for (int i = 0; i < map->capacity; i++) {
            HashNode *node = map->buckets[i];
            while (node) {
                if (!first) sb_append_char(sb, ',');
                sb_append_escaped(sb, node->key);
                sb_append_char(sb, ':');
                stringify_recursive(node->value, sb, depth + 1);
                first = 0;
                node = node->next;
            }
        }
        sb_append_char(sb, '}');
    }
}

static char* impl_stringify(Object *obj) {
    StringBuilder sb;
    sb_init(&sb);
    stringify_recursive(obj, &sb, 0);
    return sb_finish(&sb);
}

/* =================================================================
 * [5] JSONNode Wrapper & Constructor
 * ================================================================= */
static void JSONNode_Finalize(Object *self) {
    JSONNode *node = (JSONNode*)self;
    if (node->core_data) RELEASE(node->core_data);
}

const Class jsonNodeClass = {
    .name = "JSONNode",
    .size = sizeof(JSONNode),
    .toString = NULL,
    .finalize = JSONNode_Finalize
};

static int node_isObject(JSONNode *self) { return self->is_object_flag; }
static int node_isArray(JSONNode *self) { return self->is_array_flag; }

static void node_put(JSONNode *self, const char *key, Object *val) {
    if (self->is_object_flag && self->core_data) {
        ((HashMap*)self->core_data)->put((HashMap*)self->core_data, key, val);
    }
}

static Object* node_get(JSONNode *self, const char *key) {
    if (self->is_object_flag && self->core_data) {
        return ((HashMap*)self->core_data)->get((HashMap*)self->core_data, key);
    }
    return NULL;
}

static const char* node_getString(JSONNode *self, const char *key) {
    Object *val = node_get(self, key);
    if (is_json_value(val) && ((JsonValue*)val)->type == J_STRING) {
        return ((JsonValue*)val)->string;
    }
    return NULL;
}

static const char* node_getStringLen(JSONNode *self, const char *key, size_t *out_len) {
    Object *val = node_get(self, key);

    if (is_json_value(val) && ((JsonValue*)val)->type == J_STRING) {
        if (out_len) *out_len = ((JsonValue*)val)->string_exact_size;
        return ((JsonValue*)val)->string;
    }

    if (out_len) *out_len = 0;
    return NULL;
}

static int node_getInt(JSONNode *self, const char *key) {
    Object *val = node_get(self, key);
    if (is_json_value(val) && ((JsonValue*)val)->type == J_NUMBER) {
        return (int)((JsonValue*)val)->number;
    }
    return 0;
}

static void node_add(JSONNode *self, Object *val) {
    if (self->is_array_flag && self->core_data) {
        ((ArrayList*)self->core_data)->add((ArrayList*)self->core_data, val);
    }
}

static Object* node_getIndex(JSONNode *self, int index) {
    if (self->is_array_flag && self->core_data) {
        return ((ArrayList*)self->core_data)->get((ArrayList*)self->core_data, index);
    }
    return NULL;
}

static int node_length(JSONNode *self) {
    if (self->is_array_flag && self->core_data) {
        return ((ArrayList*)self->core_data)->getSize((ArrayList*)self->core_data);
    }
    return 0;
}

static char* node_toString(JSONNode *self) {
    if (self->core_data) return impl_stringify(self->core_data);
    char *null_str = strdup("null");
    if (!null_str) return NULL;
    return null_str;
}

static int node_equals(JSONNode *self, JSONNode *other) {
    if (!self || !other) return 0;
    return impl_json_equals(self->core_data, other->core_data);
}

static JSONNode* alloc_JSONNode(int is_obj) {
    JSONNode *node = (JSONNode*)calloc(1, sizeof(JSONNode));
    if (!node) return NULL;

    Object_Init((Object*)node, &jsonNodeClass);
    node->isObject = node_isObject;
    node->isArray = node_isArray;
    node->put = node_put;
    node->get = node_get;
    node->getString = node_getString;
    node->getStringLen = node_getStringLen;
    node->getInt = node_getInt;
    node->add = node_add;
    node->getIndex = node_getIndex;
    node->length = node_length;
    node->toString = node_toString;
    node->equals = node_equals;

    node->is_object_flag = is_obj;
    node->is_array_flag = !is_obj;

    if (is_obj) {
        node->core_data = (Object*)new_HashMap(16);
    } else {
        node->core_data = (Object*)new_ArrayList(10);
    }

    if (!node->core_data) {
        RELEASE((Object*)node);
        return NULL;
    }

    return node;
}

JSONNode* new_JSON_Object(void) {
    return alloc_JSONNode(1);
}

JSONNode* new_JSON_Array(void) {
    return alloc_JSONNode(0);
}

JSONNode* new_JSON_String(const char* s) {
    return (JSONNode*)new_json_string(s);
}

ParseResult parse_JSON(const char *json_str) {
    ParseResult res;
    res.root = NULL;
    res.success = 0;
    memset(res.error, 0, sizeof(res.error));

    if (!json_str || strlen(json_str) == 0) {
        snprintf(res.error, sizeof(res.error), "Empty input string.");
        return res;
    }

    ParseContext ctx = { json_str, json_str, 0, res.error, sizeof(res.error), 0 };
    Object *parsed = parse_value(&ctx);

    if (!parsed) {
        if (!ctx.has_error) {
            snprintf(res.error, sizeof(res.error), "Empty or whitespace-only JSON.");
        }
        return res;
    }

    if (ctx.has_error) {
        RELEASE(parsed);
        return res;
    }

    skip_ws(&ctx);

    if (*(ctx.ptr) != '\0') {
        snprintf(res.error, sizeof(res.error), "Garbage data after root element.");
        RELEASE(parsed);
        return res;
    }

    JSONNode *node = (JSONNode*)calloc(1, sizeof(JSONNode));
    if (!node) {
        snprintf(res.error, sizeof(res.error), "OOM while allocating root JSONNode.");
        RELEASE(parsed);
        return res;
    }

    Object_Init((Object*)node, &jsonNodeClass);
    node->isObject = node_isObject;
    node->isArray = node_isArray;
    node->put = node_put;
    node->get = node_get;
    node->getString = node_getString;
    node->getStringLen = node_getStringLen;
    node->getInt = node_getInt;
    node->add = node_add;
    node->getIndex = node_getIndex;
    node->length = node_length;
    node->toString = node_toString;
    node->equals = node_equals;

    node->core_data = parsed;

    if (is_hashmap(parsed)) {
        node->is_object_flag = 1;
        node->is_array_flag = 0;
    } else if (is_arraylist(parsed)) {
        node->is_object_flag = 0;
        node->is_array_flag = 1;
    }

    res.success = 1;
    res.root = node;

    return res;
}

static Object* impl_parse(const char *json_str) {
    if (!json_str) return NULL;
    ParseContext ctx = { json_str, json_str, 0, NULL, 0, 0 };
    return parse_value(&ctx);
}

/* =================================================================
 * [6] Legacy & 하위 호환성 래퍼 구현
 * ================================================================= */
JSONNode* new_JSON(const char *json_str_or_null) {
    if (!json_str_or_null || strlen(json_str_or_null) == 0) return new_JSON_Object();
    if (strcmp(json_str_or_null, "[]") == 0) return new_JSON_Array();

    ParseResult res = parse_JSON(json_str_or_null);
    if (res.success) return res.root;
    else {
        fprintf(stderr, "[Legacy Wrapper] 파싱 실패: %s\n", res.error);
        return NULL;
    }
}

static char* om_writeValueAsString(Object *obj) {
    if (!obj) return strdup("null");
    if (is_json_node(obj)) return impl_stringify(((JSONNode*)obj)->core_data);
    return impl_stringify(obj);
}

static const ObjectMapper mapperInstance = {
    .writeValueAsString = om_writeValueAsString
};

const ObjectMapper* GetObjectMapper(void) {
    return &mapperInstance;
}

static const JSON jsonInstance = {
    .parse = impl_parse,
    .stringify = impl_stringify
};

const JSON* GetJSON(void) {
    return &jsonInstance;
}

Object* json_parse(const char *json_str) {
    return impl_parse(json_str);
}