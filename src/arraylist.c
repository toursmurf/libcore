#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arraylist.h"

/* =========================================
 * [Internal] Helper & Object Overrides
 * ========================================= */
static void ArrayList_ToString(Object *self, char *buffer, size_t len) {
    ArrayList *list = (ArrayList*)self;

    snprintf(buffer, len, "ArrayList(size=%d, cap=%d)", list->size, list->capacity);
}

static void ArrayList_Finalize(Object* self) {
    ArrayList* list = (ArrayList*)self;

    if (list->items) {
        for (int i = 0; i < list->size; i++) {
            RELEASE(list->items[i]);
        }

        free(list->items);
    }

    pthread_mutex_destroy(&list->lock);
}

const Class arrayListClass = {
    .name = "ArrayList",
    .size = sizeof(ArrayList),
    .toString = ArrayList_ToString,
    .finalize = ArrayList_Finalize
};

// 🚨 [의장님 패치] 타입 체커 inline 화
static inline bool is_arraylist(Object *obj) {
    return (obj && GET_CLASS(obj) == &arrayListClass);
}

/* =========================================
 * [Internal] Iterator Implementation (🚨 복구 완료!)
 * ========================================= */
static bool iter_hasNext(ArrayListIterator *self) {
    if (!self) {
        return false;
    }

    if (!self->list) {
        return false;
    }

    return (self->currentIndex < self->list->getSize(self->list));
}

static Object* iter_next(ArrayListIterator *self) {
    if (!iter_hasNext(self)) {
        return NULL;
    }

    // 이터레이터는 리스트의 항목을 잠시 빌려오는(Borrowed) 개념
    Object* item = self->list->get(self->list, self->currentIndex);

    self->currentIndex++;

    return item;
}

static void Iterator_Finalize(Object *obj) {
    ArrayListIterator *self = (ArrayListIterator*)obj;

    // [중요] 이터레이터가 생성 시 RETAIN 했던 리스트의 소유권을 반납!
    if (self->list) {
        RELEASE(self->list);
        self->list = NULL;
    }
}

const Class arrayListIteratorClass = {
    .name = "ArrayListIterator",
    .size = sizeof(ArrayListIterator),
    .finalize = Iterator_Finalize,
    .toString = NULL,
    .equals = NULL,
    .hashCode = NULL
};

static ArrayListIterator* impl_iterator(ArrayList *self) {
    if (!self) {
        return NULL;
    }

    ArrayListIterator *iter = (ArrayListIterator*)malloc(sizeof(ArrayListIterator));

    if (!iter) {
        return NULL;
    }

    Object_Init((Object*)iter, &arrayListIteratorClass);

    // [핵심] 이터레이터가 살아있는 동안 리스트가 소멸되지 않도록 소유권 공유
    iter->list = (ArrayList*)RETAIN(self);
    iter->currentIndex = 0;
    iter->hasNext = iter_hasNext;
    iter->next = iter_next;

    return iter;
}

/* =========================================
 * [Methods] Implementation
 * ========================================= */

// 🚨 [S급 방어] 내부 확장을 담당하는 헬퍼 함수. 실패 시 false 반환
static bool impl_ensureCapacity_internal(ArrayList* self, int min_capacity) {
    if (min_capacity <= self->capacity) {
        return true;
    }

    int new_cap = self->capacity * 2;

    if (new_cap < min_capacity) {
        new_cap = min_capacity;
    }

    Object **new_items = (Object**)realloc(self->items, sizeof(Object*) * new_cap);

    if (!new_items) {
        return false;
    }

    self->items = new_items;
    self->capacity = new_cap;

    return true;
}

static void impl_ensureCapacity(ArrayList* self, int min_capacity) {
    pthread_mutex_lock(&self->lock);

    impl_ensureCapacity_internal(self, min_capacity);

    pthread_mutex_unlock(&self->lock);
}

static void impl_add(ArrayList *self, Object *item) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    // 🚨 [S급 방어] 용량 확보 실패(OOM) 시 배열 범위 초과 쓰기(OOB) 즉각 차단
    if (!impl_ensureCapacity_internal(self, self->size + 1)) {
        pthread_mutex_unlock(&self->lock);
        return;
    }

    self->items[self->size++] = RETAIN(item);

    pthread_mutex_unlock(&self->lock);
}

static bool impl_isEmpty(ArrayList *self) {
    if (!self) {
        return true;
    }

    pthread_mutex_lock(&self->lock);

    bool empty = (self->size == 0);

    pthread_mutex_unlock(&self->lock);

    return empty;
}

static Object* impl_get(ArrayList *self, int index) {
    if (!self) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    Object *item = NULL;

    if (index >= 0 && index < self->size) {
        item = self->items[index];
    }

    pthread_mutex_unlock(&self->lock);

    return item;
}

static void impl_remove(ArrayList *self, int index) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    if (index >= 0 && index < self->size) {
        RELEASE(self->items[index]);

        for (int i = index; i < self->size - 1; i++) {
            self->items[i] = self->items[i + 1];
        }

        self->size--;
        self->items[self->size] = NULL;
    }

    pthread_mutex_unlock(&self->lock);
}

static void impl_removeResult(ArrayList* self, int index) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    if (index >= 0 && index < self->size) {
        for (int i = index; i < self->size - 1; i++) {
            self->items[i] = self->items[i + 1];
        }

        self->size--;
        self->items[self->size] = NULL;
    }

    pthread_mutex_unlock(&self->lock);
}

static Object* impl_detach(ArrayList *self, int index) {
    if (!self) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    Object *item = NULL;

    if (index >= 0 && index < self->size) {
        item = self->items[index]; // 알맹이를 챙긴다 (RELEASE 안 함!)

        for (int i = index; i < self->size - 1; i++) {
            self->items[i] = self->items[i + 1];
        }

        self->size--;
        self->items[self->size] = NULL;
    }

    pthread_mutex_unlock(&self->lock);

    return item;
}

static int impl_getSize(ArrayList *self) {
    if (!self) {
        return 0;
    }

    pthread_mutex_lock(&self->lock);

    int s = self->size;

    pthread_mutex_unlock(&self->lock);

    return s;
}

static void impl_clear(ArrayList *self) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    for(int i = 0; i < self->size; i++) {
        RELEASE(self->items[i]);
        self->items[i] = NULL;
    }

    self->size = 0;

    pthread_mutex_unlock(&self->lock);
}

static void impl_trimToSize(ArrayList* self) {
    if (!self) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    if (self->size < self->capacity) {
        int new_cap = (self->size == 0) ? 1 : self->size;

        Object** new_items = (Object**)realloc(self->items, new_cap * sizeof(Object*));

        if (new_items) {
            self->items = new_items;
            self->capacity = new_cap;
        }
    }

    pthread_mutex_unlock(&self->lock);
}

static void impl_forEach(ArrayList* self, ArrayListActionFunc action) {
    if (!self || !action) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    for(int i = 0; i < self->size; i++) {
        action(self->items[i]);
    }

    pthread_mutex_unlock(&self->lock);
}

static void* impl_find(ArrayList* self, void* target, ArrayListCompareFunc compare) {
    if (!self || !compare) {
        return NULL;
    }

    pthread_mutex_lock(&self->lock);

    void* result = NULL;

    for(int i = 0; i < self->size; i++) {
        if (compare(self->items[i], target) == 0) {
            result = self->items[i];
            break;
        }
    }

    pthread_mutex_unlock(&self->lock);

    return result;
}

static void impl_sort(ArrayList* self, ArrayListCompareFunc compare) {
    if (!self || !compare || self->size <= 1) {
        return;
    }

    pthread_mutex_lock(&self->lock);

    qsort(self->items, self->size, sizeof(Object*), (int (*)(const void *, const void *))compare);

    pthread_mutex_unlock(&self->lock);
}

static void impl_destroy(ArrayList* self) {
    if (!self) {
        return;
    }

    RELEASE((Object*)self);
}

/* =========================================
 * [Constructor] new_ArrayList
 * ========================================= */
ArrayList* new_ArrayList(int initial_capacity) {
    if (initial_capacity <= 0) {
        initial_capacity = 10;
    }

    ArrayList *list = (ArrayList*)malloc(sizeof(ArrayList));

    if (!list) {
        return NULL;
    }

    Object_Init((Object*)list, &arrayListClass);

    list->capacity = initial_capacity;
    list->size = 0;

    list->items = (Object**)calloc(list->capacity, sizeof(Object*));

    if (!list->items) {
        free(list);
        return NULL;
    }

    pthread_mutex_init(&list->lock, NULL);

    list->add = impl_add;
    list->get = impl_get;
    list->remove = impl_remove;
    list->removeResult = impl_removeResult;
    list->detach = impl_detach;
    list->getSize = impl_getSize;
    list->clear = impl_clear;
    list->ensureCapacity = impl_ensureCapacity;
    list->forEach = impl_forEach;
    list->find = impl_find;
    list->sort = impl_sort;
    list->iterator = impl_iterator;
    list->isEmpty = impl_isEmpty;
    list->trimToSize = impl_trimToSize;
    list->destroy = impl_destroy;

    return list;
}