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

// arraylist.c 수정본
static void ArrayList_Finalize(Object* self) {
     ArrayList* list = (ArrayList*)self;
     if (list->items) {
         for (int i = 0; i < list->size; i++)
             RELEASE(list->items[i]);
         free(list->items);
     }
     pthread_mutex_destroy(&list->lock);
     // free(list) 없음! destroy()가 알아서!
 }

const Class arrayListClass = {
    .name = "ArrayList",
    .size = sizeof(ArrayList),
    .toString = ArrayList_ToString,
    .finalize = ArrayList_Finalize
};

/* =========================================
 * [Internal] Iterator Implementation
 * ========================================= */
static bool iter_hasNext(ArrayListIterator *self) {
   if (!self || !self->list) return false;
       return (self->currentIndex < self->list->getSize(self->list));
}

static Object* iter_next(ArrayListIterator *self) {
    if (!iter_hasNext(self)) return NULL;
    // 이터레이터는 리스트의 항목을 잠시 빌려오는(Borrowed) 개념
    return self->list->get(self->list, self->currentIndex++);
}

// 3. 이터레이터 소멸 시 (ARC Finalize)
static void Iterator_Finalize(Object *obj) {
    ArrayListIterator *self = (ArrayListIterator*)obj;

    // [중요] 이터레이터가 생성 시 RETAIN 했던 리스트의 소유권을 반납!
    if (self->list) {
        RELEASE(self->list);
        self->list = NULL;
    }
    // Object 자체 메모리는 destroy()가 해제함
}

const Class arrayListIteratorClass = {
    .name = "ArrayListIterator",
    .size = sizeof(ArrayListIterator),
    .finalize = Iterator_Finalize,
    .toString = NULL,
    .equals = NULL,
    .hashCode = NULL
};

// 4. 이터레이터 생성자 (ArrayList의 메서드로 바인딩됨)
static ArrayListIterator* impl_iterator(ArrayList *self) {
    if (!self) return NULL;

    ArrayListIterator *iter = (ArrayListIterator*)malloc(sizeof(ArrayListIterator));
    if (!iter) return NULL; // ✅ 방어적 프로그래밍

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
static void impl_ensureCapacity(ArrayList* self, int min_capacity) {
    if (min_capacity > self->capacity) {
        int new_cap = self->capacity * 2;
        if (new_cap < min_capacity) new_cap = min_capacity;

        Object **new_items = (Object**)realloc(self->items, sizeof(Object*) * new_cap);
        if (new_items) {
            self->items = new_items;
            self->capacity = new_cap;
        }
    }
}

static void impl_add(ArrayList *self, Object *item) {
    pthread_mutex_lock(&self->lock);
    impl_ensureCapacity(self, self->size + 1);
    // [ARC] 리스트가 객체를 보관하므로 소유권 획득
    self->items[self->size++] = RETAIN(item);
    pthread_mutex_unlock(&self->lock);
}

static bool impl_isEmpty(ArrayList *self) {
    return self->size == 0;
}

static Object* impl_get(ArrayList *self, int index) {
    pthread_mutex_lock(&self->lock);
    Object *item = NULL;
    if (index >= 0 && index < self->size) {
        item = self->items[index];
    }
    pthread_mutex_unlock(&self->lock);
    return item; // [Borrowed] 호출자가 소유하고 싶으면 RETAIN 호출
}

static void impl_remove(ArrayList *self, int index) {
    pthread_mutex_lock(&self->lock);
    if (index >= 0 && index < self->size) {
        // [ARC] 리스트에서 빠지므로 소유권 반납
        RELEASE(self->items[index]);

        for (int i = index; i < self->size - 1; i++) {
            self->items[i] = self->items[i + 1];
        }
        self->size--;
        self->items[self->size] = NULL;
    }
    pthread_mutex_unlock(&self->lock);
}

// 🚀 [의장님 명품 로직] 살려서 꺼내기 (소유권 이전)
static Object* impl_detach(ArrayList *self, int index) {
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

    return item; // 이제 리턴받은 자가 이 객체의 주인이 됨
}

// ✅ 수정: 래퍼 함수 추가
static void ArrayList_release(ArrayList* self) {
    RELEASE((Object*)self);
}

static int impl_getSize(ArrayList *self) {
    pthread_mutex_lock(&self->lock);
    int s = self->size;
    pthread_mutex_unlock(&self->lock);
    return s;
}

static void impl_clear(ArrayList *self) {
    pthread_mutex_lock(&self->lock);
    for(int i=0; i<self->size; i++) {
        RELEASE(self->items[i]); // 전원 소유권 반납
    }
    self->size = 0;
    pthread_mutex_unlock(&self->lock);
}

static void impl_forEach(ArrayList* self, ArrayListActionFunc action) {
    pthread_mutex_lock(&self->lock);
    for(int i=0; i<self->size; i++) {
        action(self->items[i]);
    }
    pthread_mutex_unlock(&self->lock);
}

static void impl_sort(ArrayList* self, ArrayListCompareFunc compare) {
    pthread_mutex_lock(&self->lock);
    qsort(self->items, self->size, sizeof(Object*), (int (*)(const void *, const void *))compare);
    pthread_mutex_unlock(&self->lock);
}

/* =========================================
 * [Constructor] new_ArrayList
 * ========================================= */
ArrayList* new_ArrayList(int initial_capacity) {
    if (initial_capacity <= 0) initial_capacity = 10;

    ArrayList *list = (ArrayList*)malloc(sizeof(ArrayList));
    if (!list) return NULL;
    Object_Init((Object*)list, &arrayListClass);

    list->capacity = initial_capacity;
    list->size = 0;
    list->items = (Object**)calloc(list->capacity, sizeof(Object*));
    pthread_mutex_init(&list->lock, NULL);

    // 인터페이스 바인딩
    list->add = impl_add;
    list->get = impl_get;
    list->remove = impl_remove;
    list->detach = impl_detach;
    list->getSize = impl_getSize;
    list->clear = impl_clear;
    list->ensureCapacity = impl_ensureCapacity;
    list->forEach = impl_forEach;
    list->sort = impl_sort;
    list->iterator = impl_iterator;
    list->isEmpty = impl_isEmpty;

    // 편의용 바인딩
    list->destroy = ArrayList_release;

    return list;
}