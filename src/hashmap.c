#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hashmap.h"

/* =========================================
 * [Internal] Helper: DJB2 Hash Algorithm
 * ========================================= */
static unsigned int hash_str(const char *str) {
    unsigned int hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash;
}

/* =========================================
 * [Object] Override: 생명주기 관리 (Finalize)
 * ========================================= */
static void HashMap_ToString(Object *self, char *buffer, size_t len) {
    HashMap *map = (HashMap*)self;

    snprintf(buffer, len, "HashMap(size=%d, cap=%d)", map->size, map->capacity);
}

static void HashMap_Finalize(Object *self) {
    HashMap *map = (HashMap*)self;

    if (map->buckets) {
        for (int i = 0; i < map->capacity; i++) {
            HashNode *node = map->buckets[i];

            while (node) {
                HashNode *next = node->next;

                if (node->key) {
                    free(node->key);
                }

                if (node->value) {
                    RELEASE(node->value);
                }

                free(node);
                node = next;
            }
        }

        free(map->buckets);
    }

    pthread_mutex_destroy(&map->lock);
}

const Class hashMapClass = {
    .name = "HashMap",
    .size = sizeof(HashMap),
    .toString = HashMap_ToString,
    .finalize = HashMap_Finalize
};

// 🚨 [의장님 패치] 타입 체커 inline 화
static inline bool is_hashmap(Object *obj) {
    return (obj && GET_CLASS(obj) == &hashMapClass);
}

/* =========================================
 * [Methods] Implementation
 * ========================================= */

// 전방 선언 (hasKey 등 내부 참조용)
static Object* impl_get(HashMap *self, const char *key);

static ArrayList* HM_keys(HashMap* self) {
    if (!self) {
        return NULL;
    }

    int init_cap = self->size > 0 ? self->size : 10;
    ArrayList* list = new_ArrayList(init_cap);

    if (!list) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    for (int i = 0; i < self->capacity; i++) {
        HashNode* node = self->buckets[i];

        while (node != NULL) {
            String* key_str = new_String(node->key);

            if (key_str) {
                list->add(list, (Object*)key_str);
                RELEASE(key_str);
            }

            node = node->next;
        }
    }

    pthread_mutex_unlock(&self->lock);

    return list;
}

static ArrayList* HM_values(HashMap* self) {
    if (!self) {
        return NULL;
    }

    int init_cap = self->size > 0 ? self->size : 10;
    ArrayList* list = new_ArrayList(init_cap);

    if (!list) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    for (int i = 0; i < self->capacity; i++) {
        HashNode* node = self->buckets[i];

        while (node != NULL) {
            list->add(list, node->value);
            node = node->next;
        }
    }

    pthread_mutex_unlock(&self->lock);

    return list;
}

static bool HashMap_hasKey_impl(HashMap* self, const char* key) {
    if (!self || !key) {
        return false;
    }

    return (impl_get(self, key) != NULL);
}

static void impl_put(HashMap *self, const char *key, Object *value) {
    if (!self) {
        return;
    }

    // 🚨 [S급 방어] 악의적인 NULL 키 접근 원천 차단
    if (!key) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];

    while (node) {
        // 🚨 [의장님 패치] strncmp(MAX_KEY_LEN)으로 일관성 통일
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            RELEASE(node->value);
            node->value = RETAIN(value);

            pthread_mutex_unlock(&self->lock);
            return;
        }

        node = node->next;
    }

    HashNode *newNode = (HashNode*)malloc(sizeof(HashNode));

    // 🚨 [S급 방어] HashNode 껍데기 할당 실패(OOM) 시 메모리 릭 없이 즉시 중단
    if (!newNode) {
        pthread_mutex_unlock(&self->lock);
        return;
    }

    newNode->key = strdup(key);

    // 🚨 [S급 방어] strdup 실패(OOM) 시 껍데기를 파기(Rollback)하고 즉시 중단
    if (!newNode->key) {
        free(newNode);
        pthread_mutex_unlock(&self->lock);
        return;
    }

    newNode->value = RETAIN(value);
    newNode->next = self->buckets[index];

    self->buckets[index] = newNode;
    self->size++;

    pthread_mutex_unlock(&self->lock);
}

static Object* impl_get(HashMap *self, const char *key) {
    if (!self || !key) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];

    while (node) {
        // 🚨 [의장님 패치] strncmp(MAX_KEY_LEN)으로 일관성 통일
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            Object *val = node->value;

            pthread_mutex_unlock(&self->lock);
            return val;
        }

        node = node->next;
    }

    pthread_mutex_unlock(&self->lock);

    return NULL;
}

static void impl_remove(HashMap *self, const char *key) {
    if (!self || !key) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        // 🚨 [의장님 패치] strncmp(MAX_KEY_LEN)으로 일관성 통일
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                self->buckets[index] = node->next;
            }

            free(node->key);
            RELEASE(node->value);
            free(node);

            self->size--;
            break;
        }

        prev = node;
        node = node->next;
    }

    pthread_mutex_unlock(&self->lock);
}

static Object* impl_detach(HashMap *self, const char *key) {
    if (!self || !key) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];
    HashNode *prev = NULL;
    Object *extracted_value = NULL;

    while (node) {
        // 🚨 [의장님 패치 복구 완료] strcmp -> strncmp(MAX_KEY_LEN)으로 일관성 통일!!!
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                self->buckets[index] = node->next;
            }

            extracted_value = node->value;

            free(node->key);
            free(node);

            self->size--;
            break;
        }

        prev = node;
        node = node->next;
    }

    pthread_mutex_unlock(&self->lock);

    return extracted_value;
}

static void impl_clear(HashMap *self) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    for (int i = 0; i < self->capacity; i++) {
        HashNode *node = self->buckets[i];

        while (node) {
            HashNode *next = node->next;

            free(node->key);
            RELEASE(node->value);
            free(node);

            node = next;
        }

        self->buckets[i] = NULL;
    }

    self->size = 0;

    pthread_mutex_unlock(&self->lock);
}

static void impl_forEach(HashMap *self, void (*action)(const char* key, Object* value)) {
    if (!self || !action) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    for (int i = 0; i < self->capacity; i++) {
        HashNode *node = self->buckets[i];

        while (node) {
            action(node->key, node->value);
            node = node->next;
        }
    }

    pthread_mutex_unlock(&self->lock);
}

static int impl_getSize(HashMap *self) {
    if (!self) {
        return 0;
    }

    pthread_mutex_lock(&self->lock);

    int s = self->size;

    pthread_mutex_unlock(&self->lock);

    return s;
}

static bool impl_isEmpty(HashMap *self) {
    if (!self) {
        return true;
    }

    pthread_mutex_lock(&self->lock);

    bool empty = (self->size == 0);

    pthread_mutex_unlock(&self->lock);

    return empty;
}

static void impl_free(HashMap *self) {
    if (!self) {
        return;
    }

    RELEASE((Object*)self);
}

/* =========================================
 * [Constructor] new_HashMap
 * ========================================= */
HashMap* new_HashMap(int initial_capacity) {
    if (initial_capacity <= 0) {
        initial_capacity = 16;
    }

    HashMap *map = (HashMap*)malloc(sizeof(HashMap));

    if (!map) {
        return NULL;
    }

    Object_Init((Object*)map, &hashMapClass);

    map->capacity = initial_capacity;
    map->size = 0;
    map->buckets = (HashNode**)calloc(map->capacity, sizeof(HashNode*));

    if (!map->buckets) {
        free(map);
        return NULL;
    }

    map->loadFactor = 0.75f;
    pthread_mutex_init(&map->lock, NULL);

    map->put = impl_put;
    map->get = impl_get;
    map->remove = impl_remove;
    map->detach = impl_detach;
    map->clear = impl_clear;
    map->forEach = impl_forEach;
    map->getSize = impl_getSize;
    map->isEmpty = impl_isEmpty;

    map->keys = HM_keys;
    map->values = HM_values;
    map->hasKey = HashMap_hasKey_impl;

    map->free = impl_free;
    map->destroy = impl_free;

    return map;
}

/* ==============================================================================
 * 💡 [추가] C 기본 자료형 변환 헬퍼 (Helper API) 구현체
 * ============================================================================== */

void hashmap_put_str(HashMap* self, const char* key, const char* value) {
    if (!self || !key || !value) {
        return;
    }

    Object* v = (Object*)new_String(value);

    if (v) {
        self->put(self, key, v);
        RELEASE(v);
    }
}

void hashmap_put_int(HashMap* self, const char* key, int value) {
    if (!self || !key) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);

    Object* v = (Object*)new_String(buf);

    if (v) {
        self->put(self, key, v);
        RELEASE(v);
    }
}

void hashmap_put_long(HashMap* self, const char* key, long value) {
    if (!self || !key) {
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", value);

    Object* v = (Object*)new_String(buf);

    if (v) {
        self->put(self, key, v);
        RELEASE(v);
    }
}

const char* hashmap_get_str(HashMap* self, const char* key) {
    if (!self || !key) {
        return NULL;
    }

    String* v = (String*)self->get(self, key);

    return v ? v->value : NULL;
}

int hashmap_get_int(HashMap* self, const char* key) {
    const char* str = hashmap_get_str(self, key);

    return str ? atoi(str) : 0;
}

long hashmap_get_long(HashMap* self, const char* key) {
    const char* str = hashmap_get_str(self, key);

    return str ? atol(str) : 0L;
}