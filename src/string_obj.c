#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <regex.h> // glibc POSIX 정규표현식
#include "string_obj.h"

extern const Class stringClass;

/* =========================================
 * [Internal] Helper Functions
 * ========================================= */
static void String_release(String* self) {
    RELEASE((Object*)self);
}

static char* str_replace_all(const char* source, const char* target, const char* replacement) {
    if (!source || !target || !replacement) return strdup(source ? source : "");
    size_t target_len = strlen(target);
    size_t replacement_len = strlen(replacement);
    if (target_len == 0) return strdup(source);

    size_t cnt = 0;
    const char* tmp = source;
    while ((tmp = strstr(tmp, target))) { cnt++; tmp += target_len; }

    size_t src_len = strlen(source);
    size_t new_len;
    if (replacement_len >= target_len) {
        size_t diff = replacement_len - target_len;
        if (cnt > 0 && diff > (SIZE_MAX - src_len - 1) / cnt) return NULL; // overflow guard
        new_len = src_len + cnt * diff + 1;
    } else {
        new_len = src_len - cnt * (target_len - replacement_len) + 1;
    }
    char* result = (char*)malloc(new_len);
    if (!result) return NULL;

    size_t i = 0;
    while (*source) {
        if (strstr(source, target) == source) {
            strcpy(&result[i], replacement);
            i += replacement_len;
            source += target_len;
        } else {
            result[i++] = *source++;
        }
    }
    result[i] = '\0';
    return result;
}

static int get_utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* =========================================
 * [Methods] Implementation
 * ========================================= */
static int impl_length(String* self) { return self->length; }

static char impl_charAt(String* self, int index) {
    if (index < 0 || index >= self->length) return '\0';
    return self->value[index];
}

static bool impl_equals(String* self, const char* another) {
    if (!another) return false;
    return strcmp(self->value, another) == 0;
}

static int impl_indexOf(String* self, const char* str) {
    if (!str) return -1;
    char* pos = strstr(self->value, str);
    return pos ? (int)(pos - self->value) : -1;
}

static bool impl_isEmpty(String* self) { return self->length == 0; }

static void impl_delete(String* self, const char* target) {
    if (!target || strlen(target) == 0) return;
    char* p;
    int t_len = (int)strlen(target);
    while ((p = strstr(self->value, target))) {
        memmove(p, p + t_len, strlen(p + t_len) + 1);
        self->length -= t_len;
    }
}

static String* impl_substring(String* self, int begin, int end) {
    if (begin < 0 || end > self->length || begin > end) return new_String("");
    int sub_len = end - begin;
    char* sub = (char*)malloc(sub_len + 1);
    if (!sub) return NULL;
    strncpy(sub, self->value + begin, sub_len);
    sub[sub_len] = '\0';
    String* new_s = new_String(sub);
    free(sub);
    return new_s;
}

static String* impl_trim(String* self) {
    char *start = self->value, *end = self->value + self->length - 1;
    while(isspace((unsigned char)*start)) start++;
    if(*start == 0) return new_String("");
    while(end > start && isspace((unsigned char)*end)) end--;
    int len = (int)(end - start + 1);
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) return NULL;
    strncpy(trimmed, start, len);
    trimmed[len] = '\0';
    String* new_s = new_String(trimmed);
    free(trimmed);
    return new_s;
}

// 🚀 [보안 패치] impl_append: Silent Fail 방지, 오버플로우 가드, memcpy 최적화 적용
static void impl_append(String* self, const char* str) {
    if (!self || !str) return;
    size_t len = strlen(str);
    if (len == 0) return;

    if ((size_t)self->length + len < (size_t)self->length) {
        fprintf(stderr, "[String_append] Error: Integer overflow detected in length.\n");
        return;
    }

    size_t new_len = (size_t)self->length + len;
    char* new_val = (char*)realloc(self->value, new_len + 1);

    if (!new_val) {
        fprintf(stderr, "[String_append] Error: realloc failed! Capacity full.\n");
        return;
    }

    self->value = new_val;
    memcpy(self->value + self->length, str, len);
    self->length = (int)new_len;
    self->value[self->length] = '\0';
}

static String* impl_replace(String* self, const char* target, const char* replacement) {
    char* replaced = str_replace_all(self->value, target, replacement);
    String* new_obj = new_String(replaced);
    free(replaced);
    return new_obj;
}

// 🚀 [긴급 수정] strtok_r 교체 완료! (Thread-Safety 확보)
static ArrayList* impl_split(String* self, const char* delimiter) {
    ArrayList* list = new_ArrayList(5);
    if (!list) return NULL;
    char* str_copy = strdup(self->value);
    char* saveptr;
    char* token = strtok_r(str_copy, delimiter, &saveptr);
    while (token != NULL) {
        list->add(list, (Object*)new_String(token));
        token = strtok_r(NULL, delimiter, &saveptr);
    }
    free(str_copy);
    return list;
}

static const char* String_c_str(String* self) {
    return self ? self->value : NULL;
}

static String* impl_implode(String* self, ArrayList* array) {
    int array_size = array->getSize(array);
    if (array_size == 0) return new_String("");
    const char* delim = self->value;
    size_t total_bytes = 0;
    for (int i = 0; i < array_size; i++) {
        String* item = (String*)array->get(array, i);
        if (item && item->value) total_bytes += item->length;
    }
    total_bytes += (strlen(delim) * (array_size - 1)) + 1;
    char* dest_raw = (char*)calloc(1, total_bytes);
    if (!dest_raw) return NULL;
    size_t offset = 0;
    for (int i = 0; i < array_size; i++) {
        String* item = (String*)array->get(array, i);
        if (item && item->value) {
            memcpy(dest_raw + offset, item->value, item->length);
            offset += item->length;
            if (i < array_size - 1) {
                memcpy(dest_raw + offset, delim, strlen(delim));
                offset += strlen(delim);
            }
        }
    }
    String* result = new_String(dest_raw);
    free(dest_raw);
    return result;
}

static String* impl_reverse(String* self) {
    int len = self->length;
    char* dest_raw = (char*)malloc(len + 1);
    if (!dest_raw) return NULL;
    dest_raw[len] = '\0';
    int src_idx = 0, dest_idx = len;
    while (src_idx < len) {
        int char_len = get_utf8_char_len((unsigned char)self->value[src_idx]);
        dest_idx -= char_len;
        memcpy(dest_raw + dest_idx, self->value + src_idx, char_len);
        src_idx += char_len;
    }
    String* new_str = new_String(dest_raw);
    free(dest_raw);
    return new_str;
}

static void impl_toLowerCase(String* self) {
    for(int i = 0; self->value[i]; i++) self->value[i] = (char)tolower((unsigned char)self->value[i]);
}

static void impl_toUpperCase(String* self) {
    for(int i = 0; self->value[i]; i++) self->value[i] = (char)toupper((unsigned char)self->value[i]);
}

// 🚀 [긴급 수정] 포인터 주소 비교로 타입 안정성 및 성능 극대화!
static int impl_toInt(Object *obj) {
    if (!obj || obj->type != &stringClass) return 0;
    String *s = (String*)obj;
    return s->value ? atoi(s->value) : 0;
}

static long long impl_toLong(Object *obj) {
    if (!obj || obj->type != &stringClass) return 0LL;
    String *s = (String*)obj;
    return s->value ? atoll(s->value) : 0LL;
}

static double impl_toDouble(Object *obj) {
    if (!obj || obj->type != &stringClass) return 0.0;
    String *s = (String*)obj;
    return s->value ? atof(s->value) : 0.0;
}

// =========================================
// 🔥 [신규] 정규표현식 매칭 로직
// =========================================
static bool impl_matches(String* self, const char* pattern) {
    if (!self || !self->value || !pattern) return false;
    regex_t preg;
    if (regcomp(&preg, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        return false;
    }
    int match = regexec(&preg, self->value, 0, NULL, 0);
    regfree(&preg);
    return (match == 0);
}

static bool impl_eregi(String* self, const char* pattern) {
    if (!self || !self->value || !pattern) return false;
    regex_t preg;
    if (regcomp(&preg, pattern, REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0) {
        return false;
    }
    int match = regexec(&preg, self->value, 0, NULL, 0);
    regfree(&preg);
    return (match == 0);
}

/* =========================================
 * [Object] Overrides
 * ========================================= */
static void String_ToString(Object *self, char *buffer, size_t len) {
    String *s = (String*)self;
    snprintf(buffer, len, "%s", s->value);
}

static void String_Finalize(Object *self) {
    String *s = (String*)self;
    if (s->value) free(s->value);
}

const Class stringClass = {
    .name = "String",
    .size = sizeof(String),
    .toString = String_ToString,
    .finalize = String_Finalize
};

/* =========================================
 * [Constructor] new_String
 * ========================================= */
String* new_String(const char* init_str) {
    String* s = (String*)malloc(sizeof(String));
    if (!s) return NULL;

    Object_Init((Object*)s, &stringClass);

    s->value = strdup(init_str ? init_str : "");
    if (!s->value) { free(s); return NULL; }

    s->length = (int)strlen(s->value);

    // 기존 인터페이스 바인딩
    s->length_f = impl_length;
    s->charAt = impl_charAt;
    s->equals = impl_equals;
    s->indexOf = impl_indexOf;
    s->substring = impl_substring;
    s->trim = impl_trim;
    s->append = impl_append;
    s->replace = impl_replace;
    s->isEmpty = impl_isEmpty;
    s->split = impl_split;
    s->explode = impl_split;
    s->implode = impl_implode;
    s->reverse = impl_reverse;
    s->toLowerCase = impl_toLowerCase;
    s->toUpperCase = impl_toUpperCase;
    s->toInt = impl_toInt;
    s->toLong = impl_toLong;
    s->toDouble = impl_toDouble;
    s->delete = impl_delete;
    s->c_str = String_c_str;

    // 🔥 정규표현식 매칭 바인딩
    s->matches = impl_matches;
    s->eregi = impl_eregi;

    // ARC 관리 래퍼 바인딩
    s->destroy = String_release;
    s->free = String_release;

    return s;
}

// 🚀 [보안 패치] string_join: 정수 오버플로우 방어 및 안전한 memcpy 적용
char* string_join(const char* delimiter, const char** str_array, int count) {
    if (!delimiter || !str_array || count <= 0) return NULL;

    size_t delim_len = strlen(delimiter);
    size_t total_len = 0;

    for (int i = 0; i < count; i++) {
        if (!str_array[i]) continue;

        size_t len = strlen(str_array[i]);
        if (total_len + len < total_len) return NULL;

        total_len += len;

        if (i < count - 1) {
            if (total_len + delim_len < total_len) return NULL;
            total_len += delim_len;
        }
    }

    char* result = (char*)malloc(total_len + 1);
    if (!result) return NULL;

    size_t offset = 0;
    for (int i = 0; i < count; i++) {
        if (!str_array[i]) continue;

        size_t len = strlen(str_array[i]);
        memcpy(result + offset, str_array[i], len);
        offset += len;

        if (i < count - 1) {
            memcpy(result + offset, delimiter, delim_len);
            offset += delim_len;
        }
    }

    result[total_len] = '\0';
    return result;
}