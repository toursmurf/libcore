#ifndef JSON_H
#define JSON_H

#include "object.h"
#include "arraylist.h"
#include "hashmap.h"

/* =========================================
 * 1. JSON Value Types (Leaf Nodes)
 * ========================================= */
typedef enum {
    J_NULL, J_BOOL, J_NUMBER, J_STRING
} JsonValType;

// 기본 데이터(문자열, 숫자 등)를 감싸는 경량 객체
typedef struct JsonValue {
    Object base;
    JsonValType type;
    union {
        int boolean;      // true/false
        double number;    // 숫자
        char *string;     // 문자열
    };
} JsonValue;

extern const Class jsonValueClass;

JsonValue* new_json_string(const char *s);
JsonValue* new_json_number(double d);
JsonValue* new_json_bool(int b);
JsonValue* new_json_null(void);

/* =========================================
 * 2. 🌟 Unified JSON Node (스마트 단일 객체)
 * ========================================= */
typedef struct JSONNode JSONNode;
struct JSONNode {
    Object base; // ARC 기반

    Object *core_data;   // 내부 데이터 (HashMap 또는 ArrayList)
    int is_object_flag;  // Object인지 여부
    int is_array_flag;   // Array인지 여부

    // [Type Check]
    int (*isObject)(JSONNode *self);
    int (*isArray)(JSONNode *self);

    // [Method: Object]
    void        (*put)(JSONNode *self, const char *key, Object *val);
    Object* (*get)(JSONNode *self, const char *key);
    const char* (*getString)(JSONNode *self, const char *key);
    int         (*getInt)(JSONNode *self, const char *key);

    // [Method: Array]
    void        (*add)(JSONNode *self, Object *val);
    Object* (*getIndex)(JSONNode *self, int index);
    int         (*length)(JSONNode *self);

    // [Common]
    char* (*toString)(JSONNode *self);
};

// 🚀 마법의 단일 생성자
JSONNode* new_JSON(const char *json_str_or_null);

/* =========================================
 * 3. 🌟 ObjectMapper (Jackson Style)
 * ========================================= */
typedef struct {
    char* (*writeValueAsString)(Object *obj);
} ObjectMapper;

const ObjectMapper* GetObjectMapper(void);

/* =========================================
 * 4. Legacy JSON Interface (Core Engine)
 * ========================================= */
typedef struct JSON JSON;
struct JSON {
    Object* (*parse)(const char *json_str);
    char* (*stringify)(Object *obj);
};

const JSON* GetJSON(void);
Object* json_parse(const char *json_str);

#endif // JSON_H
