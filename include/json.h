#ifndef JSON_H
#define JSON_H

#include "object.h"
#include "arraylist.h"
#include "hashmap.h"
#include <stddef.h>

/* =========================================
 * 1. JSON Value Types (Leaf Nodes)
 * ========================================= */
typedef enum {
    J_NULL,
    J_BOOL,
    J_NUMBER,
    J_STRING
} JsonValType;

typedef struct JsonValue {
    Object base;
    JsonValType type;
    union {
        int boolean;
        double number;
        char *string;
    };
    /* 🚨 [Diff] 파싱 시점의 정확한 바이트 길이 (Embedded NUL 방어용) */
    size_t string_exact_size;
} JsonValue;

extern const Class jsonValueClass;

JsonValue* new_json_string(const char *s);
/* 🚨 [Diff] 정확한 길이를 각인하며 소유권을 이전받는 신규 생성자 */
JsonValue* new_json_string_exact(char *s, size_t exact_size);
JsonValue* new_json_number(double d);
JsonValue* new_json_bool(int b);
JsonValue* new_json_null(void);

/* =========================================
 * 2. Unified JSON Node (스마트 단일 객체)
 * ========================================= */
typedef struct JSONNode JSONNode;

struct JSONNode {
    Object base;

    Object *core_data;
    int is_object_flag;
    int is_array_flag;

    int (*isObject)(JSONNode *self);
    int (*isArray)(JSONNode *self);

    void (*put)(JSONNode *self, const char *key, Object *val);
    Object* (*get)(JSONNode *self, const char *key);
    const char* (*getString)(JSONNode *self, const char *key);

    /* 🚨 [Diff] 길이를 함께 반환하는 VTable 신설 */
    const char* (*getStringLen)(JSONNode *self, const char *key, size_t *out_len);

    int (*getInt)(JSONNode *self, const char *key);

    void (*add)(JSONNode *self, Object *val);
    Object* (*getIndex)(JSONNode *self, int index);
    int (*length)(JSONNode *self);

    char* (*toString)(JSONNode *self);
    int (*equals)(JSONNode *self, JSONNode *other);
};

JSONNode* new_JSON_Object(void);
JSONNode* new_JSON_Array(void);
JsonValue* json_as_value(Object* obj);
JSONNode*  json_as_node (Object* obj);
/* 🚀 [추가] ToosTalk(TT-1) 호환용 문자열 노드 래퍼
 * 🚨 주의: 내부적으로 JsonValue를 반환하므로 HashMap put/ArrayList add 인자로만 사용!!
 * 절대 반환값에 대해 JSONNode 메서드(getString 등) 호출 금지 (쓰레기 포인터 점프 위험) */
JSONNode* new_JSON_String(const char* s);

/* =========================================
 * 3. ParseResult (실무형 파싱 결과 구조체)
 * ========================================= */
typedef struct {
    JSONNode* root;
    int success;
    char error[256];
} ParseResult;

ParseResult parse_JSON(const char *json_str);

/* =========================================
 * 4. ObjectMapper
 * ========================================= */
typedef struct {
    char* (*writeValueAsString)(Object *obj);
} ObjectMapper;

const ObjectMapper* GetObjectMapper(void);

/* =========================================
 * 5. [하위 호환성] Legacy API 래퍼
 * ========================================= */
JSONNode* new_JSON(const char *json_str_or_null);

typedef struct JSON JSON;

struct JSON {
    Object* (*parse)(const char *json_str);
    char* (*stringify)(Object *obj);
};

const JSON* GetJSON(void);
Object* json_parse(const char *json_str);

#endif // JSON_H