#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include "object.h"
#include "arraylist.h"

extern const Class hashtableClass;
extern const Class hashtableIteratorClass;

typedef struct Hashtable Hashtable;
typedef struct HashtableIterator HashtableIterator;
// 🚀 충돌 방지: Entry -> HashtableEntry
typedef struct HashtableEntry HashtableEntry;

typedef void (*BiConsumer)(Object *key, Object *value);

struct HashtableEntry {
    Object *key;
    Object *value;
    size_t hash;
    struct HashtableEntry *next; // 🚀 타입 변경 적용
};

struct HashtableIterator {
    Object base;
    Hashtable *ht;
    size_t currentBucketIndex;
    HashtableEntry *currentEntry; // 🚀 타입 변경 적용
    HashtableEntry *lastReturned; // 🚀 타입 변경 적용
    size_t expectedModCount;

    bool (*hasNext)(HashtableIterator *self);
    bool (*next)(HashtableIterator *self, Object **outKey, Object **outValue);
    void (*remove)(HashtableIterator *self);
};

struct Hashtable {
    Object base;

    HashtableEntry **table; // 🚀 타입 변경 적용
    int capacity;
    int count;
    int threshold;
    float loadFactor;
    size_t modCount;

    pthread_mutex_t lock;

    Object* (*put)(Hashtable *self, Object *key, Object *value);
    Object* (*get)(Hashtable *self, Object *key);
    Object* (*remove)(Hashtable *self, Object *key);

    bool (*containsKey)(Hashtable *self, Object *key);
    bool (*containsValue)(Hashtable *self, Object *value);

    size_t (*size)(Hashtable *self);
    bool (*isEmpty)(Hashtable *self);
    void (*clear)(Hashtable *self);

    void (*forEach)(Hashtable *self, BiConsumer action);

    HashtableIterator* (*iterator)(Hashtable *self);
    ArrayList* (*keys)(Hashtable *self);
    ArrayList* (*values)(Hashtable *self);

    void (*destroy)(Hashtable *self);
};

Hashtable* new_Hashtable(size_t initialCapacity, float loadFactor);

#endif // HASHTABLE_H