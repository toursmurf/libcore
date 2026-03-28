#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "json.h"
#include "hashmap.h"
#include "arraylist.h"

/* =================================================================
 * [핵심] 멤버 이름을 몰라도 Class를 가져오는 의장님 오리지널 매크로!
 * ================================================================= */
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

/* =================================================================
 * [Internal] StringBuilder (OOM 방어 로직 적용)
 * ================================================================= */
typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} StringBuilder;

static void sb_init(StringBuilder *sb) {
    sb->capacity = 256;
    sb->buffer = (char*)malloc(sb->capacity);
    if (sb->buffer) {
        sb->length = 0;
        sb->buffer[0] = '\0';
    }
}

static void sb_append(StringBuilder *sb, const char *str) {
    if (!str || !sb || !sb->buffer) return;
    size_t str_len = strlen(str);

    // [보안 패치 1] OOM(Out Of Memory) 방어 및 동적 확장
    if (sb->length + str_len + 1 > sb->capacity) {
        size_t new_cap = sb->capacity;
        while (sb->length + str_len + 1 > new_cap) {
            new_cap *= 2;
        }
        char* new_buf = (char*)realloc(sb->buffer, new_cap);
        if (!new_buf) return; 
        sb->buffer = new_buf;
        sb->capacity = new_cap;
    }
    // [보안 패치 2] 고속 & 안전 memcpy 복사
    memcpy(sb->buffer + sb->length, str, str_len);
    sb->length += str_len;
    sb->buffer[sb->length] = '\0';
}

static void sb_append_char(StringBuilder *sb, char c) {
    char tmp[2] = {c, '\0'};
    sb_append(sb, tmp);
}

static char* sb_finish(StringBuilder *sb) {
    return sb->buffer;
}

/* =================================================================
 * [1] JsonValue Implementation (리프 노드)
 * ================================================================= */
static void JsonValue_Finalize(Object *self) {
    JsonValue *v = (JsonValue*)self;
    if (v->type == J_STRING && v->string) free(v->string);
}

static void JsonValue_ToString(Object *self, char *buffer, size_t len) {
    JsonValue *v = (JsonValue*)self;
    switch(v->type) {
        case J_STRING: snprintf(buffer, len, "\"%s\"", v->string); break;
        case J_NUMBER: snprintf(buffer, len, "%g", v->number); break;
        case J_BOOL:   snprintf(buffer, len, "%s", v->boolean ? "true" : "false"); break;
        case J_NULL:   snprintf(buffer, len, "null"); break;
    }
}

const Class jsonValueClass = {
    .name = "JsonValue",
    .size = sizeof(JsonValue),
    .toString = JsonValue_ToString,
    .finalize = JsonValue_Finalize
};

JsonValue* new_json_string(const char *s) {
    JsonValue *v = (JsonValue*)malloc(sizeof(JsonValue));
    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_STRING;
    v->string = strdup(s ? s : "");
    return v;
}

JsonValue* new_json_number(double d) {
    JsonValue *v = (JsonValue*)malloc(sizeof(JsonValue));
    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_NUMBER;
    v->number = d;
    return v;
}

JsonValue* new_json_bool(int b) {
    JsonValue *v = (JsonValue*)malloc(sizeof(JsonValue));
    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_BOOL;
    v->boolean = b;
    return v;
}

JsonValue* new_json_null(void) {
    JsonValue *v = (JsonValue*)malloc(sizeof(JsonValue));
    Object_Init((Object*)v, &jsonValueClass);
    v->type = J_NULL;
    return v;
}

/* =================================================================
 * [2] Core Parsing Engine
 * ================================================================= */
static const char *skip_ws(const char *json) {
    while (*json && isspace((unsigned char)*json)) json++;
    return json;
}

static Object* parse_value(const char **ptr);

static char* parse_string_raw(const char **ptr) {
    const char *start = *ptr + 1;
    const char *end = start;
    while (*end && *end != '\"') {
        if (*end == '\\' && *(end+1)) end++;
        end++;
    }
    int len = end - start;
    char *str = (char*)malloc(len + 1);
    if (str) {
        if (len > 0) memcpy(str, start, len);
        str[len] = '\0';
    }
    *ptr = end + 1;
    return str;
}

static Object* parse_number(const char **ptr) {
    char *end;
    double d = strtod(*ptr, &end);
    *ptr = end;
    return (Object*)new_json_number(d);
}

static Object* parse_object(const char **ptr) {
    HashMap *map = new_HashMap(16);
    (*ptr)++;
    while (**ptr) {
        *ptr = skip_ws(*ptr);
        if (**ptr == '}') { (*ptr)++; return (Object*)map; }
        if (**ptr != '\"') break;
        char *key = parse_string_raw(ptr);
        *ptr = skip_ws(*ptr);
        if (**ptr != ':') { free(key); break; }
        (*ptr)++;
        Object *val = parse_value(ptr);
        if (val) {
            map->put(map, key, val);
            RELEASE(val); // HashMap이 RETAIN 했으므로 파서는 소유권 포기
        }
        free(key);
        *ptr = skip_ws(*ptr);
        if (**ptr == ',') (*ptr)++;
        else if (**ptr != '}') break;
    }
    return (Object*)map;
}

static Object* parse_array(const char **ptr) {
    ArrayList *list = new_ArrayList(10);
    (*ptr)++;
    while (**ptr) {
        *ptr = skip_ws(*ptr);
        if (**ptr == ']') { (*ptr)++; return (Object*)list; }
        Object *val = parse_value(ptr);
        if (val) {
            list->add(list, val);
            RELEASE(val);
        }
        *ptr = skip_ws(*ptr);
        if (**ptr == ',') (*ptr)++;
        else if (**ptr != ']') break;
    }
    return (Object*)list;
}

static Object* parse_value(const char **ptr) {
    *ptr = skip_ws(*ptr);
    char c = **ptr;
    if (c == '\0') return NULL;
    if (c == '\"') {
        char *s = parse_string_raw(ptr);
        Object *o = (Object*)new_json_string(s);
        free(s);
        return o;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(ptr);
    if (c == '{') return parse_object(ptr);
    if (c == '[') return parse_array(ptr);
    // 예약어는 길이 지정 strncmp로 원천 차단
    if (strncmp(*ptr, "true", 4) == 0) { *ptr += 4; return (Object*)new_json_bool(1); }
    if (strncmp(*ptr, "false", 5) == 0) { *ptr += 5; return (Object*)new_json_bool(0); }
    if (strncmp(*ptr, "null", 4) == 0) { *ptr += 4; return (Object*)new_json_null(); }
    return NULL;
}

static Object* impl_parse(const char *json_str) {
    if (!json_str) return NULL;
    const char *ptr = json_str;
    return parse_value(&ptr);
}

/* =================================================================
 * [3] Core Stringify Engine (보안 패치 완료!)
 * ================================================================= */
static void stringify_recursive(Object *obj, StringBuilder *sb);

static void stringify_arraylist(ArrayList *list, StringBuilder *sb) {
    sb_append_char(sb, '[');
    for (int i = 0; i < list->getSize(list); i++) {
        if (i > 0) sb_append_char(sb, ',');
        Object *val = list->get(list, i);
        stringify_recursive(val, sb);
    }
    sb_append_char(sb, ']');
}

static void stringify_recursive(Object *obj, StringBuilder *sb) {
    if (!obj) { sb_append(sb, "null"); return; }
    
    const Class *cls = GET_CLASS(obj);
    
    // [보안 패치] strcmp -> 안전한 strncmp + sizeof 적용
    if (strncmp(cls->name, "JsonValue", sizeof("JsonValue")) == 0) {
        char buf[64];
        cls->toString(obj, buf, sizeof(buf));
        sb_append(sb, buf);
    }
    // 🚀 [긴급 추가] JSONNode 껍데기를 만나면 알맹이(core_data)를 꺼내서 다시 직렬화!
    else if (strncmp(cls->name, "JSONNode", sizeof("JSONNode")) == 0) {
        stringify_recursive( ((JSONNode*)obj)->core_data, sb );
    }
    else if (strncmp(cls->name, "ArrayList", sizeof("ArrayList")) == 0) {
        stringify_arraylist((ArrayList*)obj, sb);
    }
    else if (strncmp(cls->name, "HashMap", sizeof("HashMap")) == 0) {
        HashMap *map = (HashMap*)obj;
        sb_append_char(sb, '{');
        int first = 1;
        for (int i = 0; i < map->capacity; i++) {
            HashNode *node = map->buckets[i];
            while (node) {
                if (!first) sb_append_char(sb, ',');
                sb_append_char(sb, '\"'); sb_append(sb, node->key); sb_append(sb, "\":");
                stringify_recursive(node->value, sb);
                first = 0;
                node = node->next;
            }
        }
        sb_append_char(sb, '}');
    }
    else {
        char buf[256];
        if (cls->toString) {
            cls->toString(obj, buf, sizeof(buf));
            sb_append_char(sb, '\"'); sb_append(sb, buf); sb_append_char(sb, '\"');
        } else {
            sb_append(sb, "\"unknown\"");
        }
    }
}

static char* impl_stringify(Object *obj) {
    StringBuilder sb;
    sb_init(&sb);
    stringify_recursive(obj, &sb);
    return sb_finish(&sb);
}

/* =================================================================
 * [4] 🚀 Java-Style JSONNode Wrapper (보안 패치 완료!)
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
static int node_isArray(JSONNode *self)  { return self->is_array_flag; }

static void node_put(JSONNode *self, const char *key, Object *val) {
    if (self->is_object_flag && self->core_data) {
        HashMap *map = (HashMap*)self->core_data;
        map->put(map, key, val);
    }
}

static Object* node_get(JSONNode *self, const char *key) {
    if (self->is_object_flag && self->core_data) {
        HashMap *map = (HashMap*)self->core_data;
        return map->get(map, key);
    }
    return NULL;
}

static const char* node_getString(JSONNode *self, const char *key) {
    Object *val = node_get(self, key);
    // [보안 패치]
    if (val && strncmp(GET_CLASS(val)->name, "JsonValue", sizeof("JsonValue")) == 0) {
        JsonValue *jv = (JsonValue*)val;
        if (jv->type == J_STRING) return jv->string;
    }
    return NULL;
}

static int node_getInt(JSONNode *self, const char *key) {
    Object *val = node_get(self, key);
    // [보안 패치]
    if (val && strncmp(GET_CLASS(val)->name, "JsonValue", sizeof("JsonValue")) == 0) {
        JsonValue *jv = (JsonValue*)val;
        if (jv->type == J_NUMBER) return (int)jv->number;
    }
    return 0;
}

static void node_add(JSONNode *self, Object *val) {
    if (self->is_array_flag && self->core_data) {
        ArrayList *list = (ArrayList*)self->core_data;
        list->add(list, val);
    }
}

static Object* node_getIndex(JSONNode *self, int index) {
    if (self->is_array_flag && self->core_data) {
        ArrayList *list = (ArrayList*)self->core_data;
        return list->get(list, index);
    }
    return NULL;
}

static int node_length(JSONNode *self) {
    if (self->is_array_flag && self->core_data) {
        ArrayList *list = (ArrayList*)self->core_data;
        return list->getSize(list);
    }
    return 0;
}

static char* node_toString(JSONNode *self) {
    if (self->core_data) return impl_stringify(self->core_data);
    return strdup("null");
}

/* =================================================================
 * [5] 🚀 Magic Constructor (new_JSON - 보안 패치 완료!)
 * ================================================================= */
JSONNode* new_JSON(const char *json_str_or_null) {
    JSONNode *node = (JSONNode*)malloc(sizeof(JSONNode));
    Object_Init((Object*)node, &jsonNodeClass);

    node->isObject = node_isObject;
    node->isArray  = node_isArray;
    node->put      = node_put;
    node->get      = node_get;
    node->getString = node_getString;
    node->getInt   = node_getInt;
    node->add      = node_add;
    node->getIndex = node_getIndex;
    node->length   = node_length;
    node->toString = node_toString;

    if (!json_str_or_null || strlen(json_str_or_null) == 0) {
        node->core_data = (Object*)new_HashMap(16);
        node->is_object_flag = 1;
        node->is_array_flag = 0;
        return node;
    }

    Object *parsed = impl_parse(json_str_or_null);
    node->core_data = parsed;

    if (parsed) {
        const Class *cls = GET_CLASS(parsed);
        // [보안 패치]
        if (strncmp(cls->name, "HashMap", sizeof("HashMap")) == 0) {
            node->is_object_flag = 1;
            node->is_array_flag = 0;
        } else if (strncmp(cls->name, "ArrayList", sizeof("ArrayList")) == 0) {
            node->is_object_flag = 0;
            node->is_array_flag = 1;
        } else {
            node->is_object_flag = 0;
            node->is_array_flag = 0;
        }
    } else {
        node->core_data = (Object*)new_HashMap(16);
        node->is_object_flag = 1;
        node->is_array_flag = 0;
    }
    return node;
}

/* =================================================================
 * [6] 🌟 ObjectMapper & Legacy Interfaces (보안 패치 완료!)
 * ================================================================= */
static char* om_writeValueAsString(Object *obj) {
    if (!obj) return strdup("null");
    
    // [보안 패치] 꼬리 잘림 방지 + 철벽 strncmp
    if (strncmp(GET_CLASS(obj)->name, "JSONNode", sizeof("JSONNode")) == 0) {
        return impl_stringify( ((JSONNode*)obj)->core_data );
    }
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
