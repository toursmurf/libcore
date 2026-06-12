#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================
 * [Internal] Helper: rehash (ARC 보존)
 * ========================================= */
static void rehash(Hashtable *self) {
    size_t oldCap = self->capacity;
    HashtableEntry **oldTable = self->table;

    size_t newCap = oldCap * 2 + 1;
    HashtableEntry **newTable = (HashtableEntry**)calloc(newCap, sizeof(HashtableEntry*));
    if (!newTable) return;

    self->capacity = newCap;
    self->threshold = (size_t)(newCap * self->loadFactor);
    self->table = newTable;
    self->modCount++;

    for (size_t i = 0; i < oldCap; i++) {
        HashtableEntry *e = oldTable[i];
        while (e) {
            HashtableEntry *next = e->next;
            size_t newIdx = e->hash % newCap;
            e->next = newTable[newIdx];
            newTable[newIdx] = e;
            e = next;
        }
    }
    free(oldTable);
}

/* =========================================
 * [Object] Override: 생명주기 관리
 * ========================================= */
static void Hashtable_Finalize(Object *self) {
    Hashtable *ht = (Hashtable*)self;
    for (size_t i = 0; i < (size_t)ht->capacity; i++) {
        HashtableEntry *e = ht->table[i];
        while (e) {
            HashtableEntry *next = e->next;
            RELEASE(e->key);
            RELEASE(e->value);
            free(e);
            e = next;
        }
    }
    free(ht->table);
    pthread_mutex_destroy(&ht->lock);
}

const Class hashtableClass = {
    .name = "Hashtable",
    .size = sizeof(Hashtable),
    .finalize = Hashtable_Finalize
};

/* =========================================
 * [Methods] Implementation
 * ========================================= */
static size_t ht_size(Hashtable *self) {
    pthread_mutex_lock(&self->lock);
    size_t s = self->count;
    pthread_mutex_unlock(&self->lock);
    return s;
}

static bool ht_isEmpty(Hashtable *self) {
    return ht_size(self) == 0;
}

static bool ht_containsKey(Hashtable *self, Object *key) {
    if (key == NULL) return false;
    pthread_mutex_lock(&self->lock);
    size_t hash = (size_t)hashCode(key);
    HashtableEntry *e = self->table[hash % self->capacity];
    while (e) {
        if (e->hash == hash && equals(e->key, key)) {
            pthread_mutex_unlock(&self->lock);
            return true;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&self->lock);
    return false;
}

static Object* ht_put(Hashtable *self, Object *key, Object *value) {
    if (key == NULL || value == NULL) return NULL;
    pthread_mutex_lock(&self->lock);

    if (self->count >= self->threshold) rehash(self);

    size_t hash = (size_t)hashCode(key);
    size_t idx = hash % self->capacity;

    HashtableEntry *e = self->table[idx];
    while (e) {
        if (e->hash == hash && equals(e->key, key)) {
            Object *old = e->value;
            e->value = RETAIN(value);
            pthread_mutex_unlock(&self->lock);
            return old;
        }
        e = e->next;
    }

    HashtableEntry *newEntry = (HashtableEntry*)malloc(sizeof(HashtableEntry));
    newEntry->key = RETAIN(key);
    newEntry->value = RETAIN(value);
    newEntry->hash = hash;
    newEntry->next = self->table[idx];
    self->table[idx] = newEntry;

    self->count++;
    self->modCount++;
    pthread_mutex_unlock(&self->lock);
    return NULL;
}

static Object* ht_get(Hashtable *self, Object *key) {
    if (key == NULL) return NULL;
    pthread_mutex_lock(&self->lock);
    size_t hash = (size_t)hashCode(key);
    HashtableEntry *e = self->table[hash % self->capacity];
    while (e) {
        if (e->hash == hash && equals(e->key, key)) {
            Object *val = e->value;
            pthread_mutex_unlock(&self->lock);
            return val;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&self->lock);
    return NULL;
}

static Object* ht_remove(Hashtable *self, Object *key) {
    if (key == NULL) return NULL;
    pthread_mutex_lock(&self->lock);
    size_t hash = (size_t)hashCode(key);
    size_t idx = hash % self->capacity;
    HashtableEntry *prev = NULL;
    HashtableEntry *e = self->table[idx];
    while (e) {
        if (e->hash == hash && equals(e->key, key)) {
            if (prev) prev->next = e->next;
            else self->table[idx] = e->next;
            Object *old = e->value;
            RELEASE(e->key);
            free(e);
            self->count--;
            self->modCount++;
            pthread_mutex_unlock(&self->lock);
            return old;
        }
        prev = e;
        e = e->next;
    }
    pthread_mutex_unlock(&self->lock);
    return NULL;
}

static void ht_clear(Hashtable *self) {
    pthread_mutex_lock(&self->lock);
    for (size_t i = 0; i < (size_t)self->capacity; i++) {
        HashtableEntry *e = self->table[i];
        while (e) {
            HashtableEntry *next = e->next;
            RELEASE(e->key);
            RELEASE(e->value);
            free(e);
            e = next;
        }
        self->table[i] = NULL;
    }
    self->count = 0;
    self->modCount++;
    pthread_mutex_unlock(&self->lock);
}

static ArrayList* ht_keys(Hashtable *self) {
    pthread_mutex_lock(&self->lock);
    ArrayList *list = new_ArrayList(self->count);
    for (size_t i = 0; i < (size_t)self->capacity; i++) {
        HashtableEntry *e = self->table[i];
        while (e) {
            list->add(list, RETAIN(e->key));
            e = e->next;
        }
    }
    pthread_mutex_unlock(&self->lock);
    return list;
}

static ArrayList* ht_values(Hashtable *self) {
    pthread_mutex_lock(&self->lock);
    ArrayList *list = new_ArrayList(self->count);
    for (size_t i = 0; i < (size_t)self->capacity; i++) {
        HashtableEntry *e = self->table[i];
        while (e) {
            list->add(list, RETAIN(e->value));
            e = e->next;
        }
    }
    pthread_mutex_unlock(&self->lock);
    return list;
}

/* =========================================
 * [Internal] Iterator Implementation
 * ========================================= */
static bool it_hasNext(HashtableIterator *self) {
    return self->currentEntry != NULL;
}

static bool it_next(HashtableIterator *self, Object **outKey, Object **outValue) {
    if (self->ht->modCount != self->expectedModCount) return false;
    if (!self->currentEntry) return false;

    if (outKey) *outKey = self->currentEntry->key;
    if (outValue) *outValue = self->currentEntry->value;

    self->lastReturned = self->currentEntry;

    if (self->currentEntry->next) {
        self->currentEntry = self->currentEntry->next;
    } else {
        self->currentEntry = NULL;
        while (self->currentBucketIndex < (size_t) self->ht->capacity) {
            HashtableEntry *e = self->ht->table[self->currentBucketIndex++];
            if (e) {
                self->currentEntry = e;
                break;
            }
        }
    }
    return true;
}

static void Iterator_Finalize(Object *self) {
	(void)self;
}
const Class hashtableIteratorClass = { "HashtableIterator", sizeof(HashtableIterator), .finalize = Iterator_Finalize };

static HashtableIterator* ht_iterator(Hashtable *self) {
    HashtableIterator *it = (HashtableIterator*)malloc(sizeof(HashtableIterator));
    Object_Init((Object*)it, &hashtableIteratorClass);
    pthread_mutex_lock(&self->lock);
    it->ht = self;
    it->currentBucketIndex = 0;
    it->currentEntry = NULL;
    it->lastReturned = NULL;
    it->expectedModCount = self->modCount;
    while (it->currentBucketIndex < (size_t)self->capacity) {
        HashtableEntry *e = self->table[it->currentBucketIndex++];
        if (e) {
            it->currentEntry = e;
            break;
        }
    }
    pthread_mutex_unlock(&self->lock);
    it->hasNext = it_hasNext;
    it->next = it_next;
    return it;
}

static void Hashtable_release(Hashtable* self) {
    RELEASE((Object*)self);
}

/* =========================================
 * [Constructor] new_Hashtable
 * ========================================= */
Hashtable* new_Hashtable(size_t initialCapacity, float loadFactor) {
    Hashtable *ht = (Hashtable*)malloc(sizeof(Hashtable));
    Object_Init((Object*)ht, &hashtableClass);

    ht->capacity = (initialCapacity == 0) ? 11 : initialCapacity;
    ht->loadFactor = (loadFactor <= 0) ? 0.75f : loadFactor;
    ht->threshold = (size_t)(ht->capacity * ht->loadFactor);
    ht->count = 0;
    ht->modCount = 0;
    ht->table = (HashtableEntry**)calloc(ht->capacity, sizeof(HashtableEntry*));
    pthread_mutex_init(&ht->lock, NULL);

    ht->put = ht_put;
    ht->get = ht_get;
    ht->remove = ht_remove;
    ht->containsKey = ht_containsKey;
    ht->size = ht_size;
    ht->isEmpty = ht_isEmpty;
    ht->clear = ht_clear;
    ht->keys = ht_keys;
    ht->values = ht_values;
    ht->iterator = ht_iterator;
    ht->destroy = Hashtable_release;

    return ht;
}