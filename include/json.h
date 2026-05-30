#ifndef JSON_H
#define JSON_H

#include "object.h"
#include "arraylist.h"
#include "hashmap.h"

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
} JsonValue;

extern const Class jsonValueClass;

JsonValue* new_json_string(const char *s);
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
    int (*getInt)(JSONNode *self, const char *key);

    void (*add)(JSONNode *self, Object *val);
    Object* (*getIndex)(JSONNode *self, int index);
    int (*length)(JSONNode *self);

    char* (*toString)(JSONNode *self);
    int (*equals)(JSONNode *self, JSONNode *other);
};

JSONNode* new_JSON_Object(void);
JSONNode* new_JSON_Array(void);

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