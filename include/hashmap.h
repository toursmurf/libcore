#ifndef HASHMAP_H
#define HASHMAP_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "arraylist.h"
#include "string_obj.h"

extern const Class hashMapClass;

typedef struct HashMap HashMap;
typedef struct HashNode HashNode;

#define MAX_KEY_LEN 128

/* 확장형 이터레이터 콜백 시그니처 (Read-Only) */
typedef bool (*HashMapIterator)(const char* key, Object* value, void* ctx);

struct HashNode {
    char* key;
    Object* value;
    struct HashNode* next;
};

struct HashMap {
    Object base;
    HashNode** buckets;
    int capacity;
    int size;
    float loadFactor;
    pthread_mutex_t lock;
    void    (*put)(HashMap* self, const char* key, Object* value);
    Object* (*get)(HashMap* self, const char* key);
    bool    (*hasKey)(HashMap* self, const char* key);
    void    (*remove)(HashMap* self, const char* key);
    void    (*clear)(HashMap* self);
    Object* (*detach)(HashMap *self, const char *key);
    void    (*forEach)(HashMap* self, void (*action)(const char* key, Object* value));
    void    (*iterate)(HashMap* self, HashMapIterator fn, void* ctx); //초고속 순회 전용
    int     (*getSize)(HashMap* self);
    bool    (*isEmpty)(HashMap* self);
    ArrayList* (*keys)(HashMap* self);
    ArrayList* (*values)(HashMap* self);
    void    (*free)(HashMap* self);
    void    (*destroy)(HashMap* self);
};

void        hashmap_put_str (HashMap* self, const char* key, const char* value);
void        hashmap_put_int (HashMap* self, const char* key, int value);
void        hashmap_put_long(HashMap* self, const char* key, long value);
const char* hashmap_get_str (HashMap* self, const char* key);
int         hashmap_get_int (HashMap* self, const char* key);
long        hashmap_get_long(HashMap* self, const char* key);
HashMap* new_HashMap(int initial_capacity); //생성자

#endif // HASHMAP_H