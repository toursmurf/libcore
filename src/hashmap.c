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
                if (node->key) free(node->key);
                if (node->value) RELEASE(node->value); // [ARC] 수동 destroy 대신 RELEASE 호출
                free(node);
                node = next;
            }
        }
        free(map->buckets);
    }
    pthread_mutex_destroy(&map->lock);
    // 메모리 해제는 상위 destroy()에서 처리됨
}

const Class hashMapClass = {
    .name = "HashMap",
    .size = sizeof(HashMap),
    .toString = HashMap_ToString,
    .finalize = HashMap_Finalize
};

/* =========================================
 * [Methods] Implementation
 * ========================================= */

// 해시맵의 모든 Key를 ArrayList로 반환
// 해시맵의 모든 Key를 ArrayList로 반환
static ArrayList* HM_keys(HashMap* self) {
    int init_cap = self->size > 0 ? self->size : 10;
    ArrayList* list = new_ArrayList(init_cap);

    pthread_mutex_lock(&self->lock);
    for (int i = 0; i < self->capacity; i++) {
        HashNode* node = self->buckets[i];
        while (node != NULL) {
            // [개선] 딥 카피 대신 String 객체화 후 RETAIN 가능 (여기선 의장님 스타일 유지)
            String* key_str = new_String(node->key);
            list->add(list, (Object*)key_str);
            RELEASE(key_str);
            node = node->next;
        }
    }
    pthread_mutex_unlock(&self->lock);
    return list;
}

// 해시맵의 모든 Value를 ArrayList로 반환
static ArrayList* HM_values(HashMap* self) {
    int init_cap = self->size > 0 ? self->size : 10;
    ArrayList* list = new_ArrayList(init_cap);

    pthread_mutex_lock(&self->lock);
    for (int i = 0; i < self->capacity; i++) {
        HashNode* node = self->buckets[i];
        while (node != NULL) {
            // 🚨 기존: list->add(list, RETAIN(node->value));
            // add 내부에서 이미 RETAIN 하므로, 여기서 또 RETAIN 하면 중복입니다!
            list->add(list, node->value); // ✅ [수정] 주소만 넘기면 add가 알아서 RETAIN 함

            node = node->next;
        }
    }
    pthread_mutex_unlock(&self->lock);
    return list;
}


static void impl_put(HashMap *self, const char *key, Object *value) {
    pthread_mutex_lock(&self->lock);
    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];
    while (node) {
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            // [ARC] 기존 값은 RELEASE 하고 새 값을 RETAIN
            RELEASE(node->value);
            node->value = RETAIN(value);
            pthread_mutex_unlock(&self->lock);
            return;
        }
        node = node->next;
    }

    HashNode *newNode = (HashNode*)malloc(sizeof(HashNode));
    newNode->key = strdup(key);
    newNode->value = RETAIN(value); // [ARC] 소유권 획득
    newNode->next = self->buckets[index];
    self->buckets[index] = newNode;
    self->size++;
    pthread_mutex_unlock(&self->lock);
}

static Object* impl_get(HashMap *self, const char *key) {
    pthread_mutex_lock(&self->lock);
    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];
    while (node) {
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            Object *val = node->value;
            pthread_mutex_unlock(&self->lock);
            return val; // [Borrowed] 호출자는 RETAIN 해서 쓰거나 그냥 사용
        }
        node = node->next;
    }
    pthread_mutex_unlock(&self->lock);
    return NULL;
}

static void impl_remove(HashMap *self, const char *key) {
    pthread_mutex_lock(&self->lock);
    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (strncmp(node->key, key, MAX_KEY_LEN) == 0) {
            if (prev) prev->next = node->next;
            else self->buckets[index] = node->next;

            free(node->key);
            RELEASE(node->value); // [ARC] 안전하게 참조 카운트 감소
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
    pthread_mutex_lock(&self->lock);
    unsigned int h = hash_str(key);
    int index = h % self->capacity;

    HashNode *node = self->buckets[index];
    HashNode *prev = NULL;
    Object *extracted_value = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) prev->next = node->next;
            else self->buckets[index] = node->next;

            extracted_value = node->value; // 알맹이만 챙김 (RELEASE 안 함)

            free(node->key);
            free(node);
            self->size--;
            break;
        }
        prev = node;
        node = node->next;
    }
    pthread_mutex_unlock(&self->lock);

    return extracted_value; // 호출자가 소유권을 그대로 이어받음
}

static void impl_clear(HashMap *self) {
    pthread_mutex_lock(&self->lock);
    for (int i = 0; i < self->capacity; i++) {
        HashNode *node = self->buckets[i];
        while (node) {
            HashNode *next = node->next;
            free(node->key);
            RELEASE(node->value); // [ARC] 연쇄 해제 터트림
            free(node);
            node = next;
        }
        self->buckets[i] = NULL;
    }
    self->size = 0;
    pthread_mutex_unlock(&self->lock);
}

static void impl_forEach(HashMap *self, void (*action)(const char* key, Object* value)) {
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

static int impl_getSize(HashMap *self) { return self->size; }
static bool impl_isEmpty(HashMap *self) { return self->size == 0; }
static void impl_free(HashMap *self) { RELEASE((Object*)self); }

/* =========================================
 * [Constructor] new_HashMap
 * ========================================= */
HashMap* new_HashMap(int initial_capacity) {
    if (initial_capacity <= 0) initial_capacity = 16;

    HashMap *map = (HashMap*)malloc(sizeof(HashMap));
    Object_Init((Object*)map, &hashMapClass);

    map->capacity = initial_capacity;
    map->size = 0;
    map->buckets = (HashNode**)calloc(map->capacity, sizeof(HashNode*));
    map->loadFactor = 0.75f;
    pthread_mutex_init(&map->lock, NULL);

    // 인터페이스 함수 포인터 바인딩
    map->put = impl_put;
    map->get = impl_get;
    map->remove = impl_remove;
    map->detach = impl_detach;
    map->clear = impl_clear;
    map->forEach = impl_forEach;
    map->getSize = impl_getSize;
    map->isEmpty = impl_isEmpty;
    map->free = impl_free;
    map->keys = HM_keys;
    map->values = HM_values;
    map->destroy = impl_free;

    return map;
}