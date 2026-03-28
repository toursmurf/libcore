#include "vector.h"
#include <stdio.h>
#include <stdlib.h>

// --- Iterator Helper ---
void it_next(VectorIterator *it) { it->ptr++; }
Object* it_get(VectorIterator *it) { return *(it->ptr); }
int it_neq(VectorIterator a, VectorIterator b) { return a.ptr != b.ptr; }

// --- 내부 로직 ---
static void resize(Vector *self, int new_capacity) {
    Object **new_items = (Object**)realloc(self->items, sizeof(Object*) * new_capacity);
    if (new_items) {
        self->items = new_items;
        self->capacity = new_capacity;
    }
}

static void impl_push_back(Vector *self, Object *item) {
    pthread_mutex_lock(&self->mutex);
    if (self->size == self->capacity) {
        int new_cap = (self->capacity == 0) ? 4 : self->capacity * 2;
        resize(self, new_cap);
    }
    // 🚀 [ARC] 소유권 획득! Vector가 요소를 꽉 쥡니다.
    self->items[self->size++] = RETAIN(item); 
    pthread_mutex_unlock(&self->mutex);
}

static Object* impl_at(Vector *self, int index) {
    pthread_mutex_lock(&self->mutex);
    Object *item = NULL;
    if (index >= 0 && index < self->size) {
        item = self->items[index];
    }
    pthread_mutex_unlock(&self->mutex);
    return item; // 🚀 Borrowed Reference (소유권 이전 없음)
}

static Object* impl_pop_back(Vector *self) {
    pthread_mutex_lock(&self->mutex);
    Object *item = NULL;
    if (self->size > 0) {
        self->size--;
        item = self->items[self->size];
        self->items[self->size] = NULL; // Dangling Pointer 방지
    }
    pthread_mutex_unlock(&self->mutex);
    return item; // 🚀 소유권을 호출자에게 넘김 (Count 유지)
}

static int impl_get_size(Vector *self) {
    pthread_mutex_lock(&self->mutex);
    int s = self->size;
    pthread_mutex_unlock(&self->mutex);
    return s;
}

static void impl_lock(Vector *self) { pthread_mutex_lock(&self->mutex); }
static void impl_unlock(Vector *self) { pthread_mutex_unlock(&self->mutex); }

static VectorIterator impl_begin(Vector *self) {
    VectorIterator it;
    it.ptr = self->items;
    return it;
}

static VectorIterator impl_end(Vector *self) {
    VectorIterator it;
    it.ptr = self->items + self->size;
    return it;
}

/* ================= Object Overrides ================= */
static void Vector_ToString(Object *self, char *buffer, size_t len) {
    Vector *v = (Vector*)self;
    snprintf(buffer, len, "Vector(size=%d)", v->size);
}

static void Vector_Finalize(Object *self) {
    Vector *v = (Vector*)self;
    
    // 🚀 [ARC 연쇄 소각] 내부 아이템들을 모두 RELEASE!
    for (int i = 0; i < v->size; i++) {
        if (v->items[i]) {
            RELEASE(v->items[i]);
        }
    }
    free(v->items);
    pthread_mutex_destroy(&v->mutex);
}

const Class vectorClass = {
    .name = "Vector",
    .size = sizeof(Vector),
    .toString = Vector_ToString,
    .finalize = Vector_Finalize
};

/* [Constructor] */
Vector *new_Vector(int initial_capacity) {
    Vector *v = (Vector *)malloc(sizeof(Vector));
    if (!v) return NULL;

    Object_Init((Object*)v, &vectorClass);
    
    v->capacity = initial_capacity > 0 ? initial_capacity : 4;
    v->size = 0;
    v->items = (Object **)malloc(sizeof(Object *) * v->capacity);
    
    pthread_mutex_init(&v->mutex, NULL);

    v->push_back = impl_push_back;
    v->at = impl_at;
    v->pop_back = impl_pop_back;
    v->get_size = impl_get_size;
    v->lock = impl_lock;
    v->unlock = impl_unlock;
    v->begin = impl_begin;
    v->end = impl_end;

    return v;
}