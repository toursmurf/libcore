#ifndef CONTEXT_H
#define CONTEXT_H

#include "object.h"
#include "hashmap.h"
#include "string_obj.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct Context Context;

struct Context {
    Object base;
    HashMap* data;
    pthread_mutex_t lock; // 직접 내장 (메모리 효율 및 Logger 스타일 통일)

    // --- API 리스트 ---
    void    (*set)    (Context* self, const char* key, Object* value);
    Object* (*get)    (Context* self, const char* key);
    Object* (*remove) (Context* self, const char* key);
    bool    (*has)    (Context* self, const char* key);
    void    (*clear)  (Context* self);
    int     (*getSize)(Context* self); // size -> getSize 통일 ✅

    void    (*setString)(Context* self, const char* key, const char* val);
    String* (*getString)(Context* self, const char* key);
    void    (*setInt)   (Context* self, const char* key, int val);
    int     (*getInt)   (Context* self, const char* key);
};

Context* new_Context();

#endif