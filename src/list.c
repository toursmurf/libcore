#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"

/* =========================================
 * [내부 구현체] 🚀 데드락 방지용 락(Lock) 없는 핵심 로직!
 * ========================================= */
static void _pushBack_nolock(List *self, Object *data) {
    ListNode *node = (ListNode*)malloc(sizeof(ListNode));
    if (!node) return;

    node->data = RETAIN(data); // 🚀 [수정] 컬렉션이 데이터의 소유권을 획득!
    node->next = NULL;
    node->prev = self->tail;

    if (self->tail) self->tail->next = node;
    else self->head = node;
    self->tail = node;
    self->size++;
}

static void _pushFront_nolock(List *self, Object *data) {
    ListNode *node = (ListNode*)malloc(sizeof(ListNode));
    if (!node) return;

    node->data = RETAIN(data); // 🚀 [수정] 소유권 획득!
    node->prev = NULL;
    node->next = self->head;

    if (self->head) self->head->prev = node;
    else self->tail = node;
    self->head = node;
    self->size++;
}

static Object* _popBack_nolock(List *self) {
    if (!self->tail) return NULL;

    ListNode *node = self->tail;
    Object *data = node->data; // 🚀 반환 시 호출자에게 소유권 이전! (RELEASE 안 함)

    self->tail = node->prev;
    if (self->tail) self->tail->next = NULL;
    else self->head = NULL;

    free(node);
    self->size--;
    return data;
}

static Object* _popFront_nolock(List *self) {
    if (!self->head) return NULL;

    ListNode *node = self->head;
    Object *data = node->data;

    self->head = node->next;
    if (self->head) self->head->prev = NULL;
    else self->tail = NULL;

    free(node);
    self->size--;
    return data;
}

/* =========================================
 * [공개 API] Mutex Lock 래퍼 함수들
 * ========================================= */
static void impl_pushBack(List *self, Object *data) {
    pthread_mutex_lock(&self->lock);
    _pushBack_nolock(self, data);
    pthread_mutex_unlock(&self->lock);
}

static void impl_pushFront(List *self, Object *data) {
    pthread_mutex_lock(&self->lock);
    _pushFront_nolock(self, data);
    pthread_mutex_unlock(&self->lock);
}

static Object* impl_popBack(List *self) {
    pthread_mutex_lock(&self->lock);
    Object* data = _popBack_nolock(self);
    pthread_mutex_unlock(&self->lock);
    return data;
}

static Object* impl_popFront(List *self) {
    pthread_mutex_lock(&self->lock);
    Object* data = _popFront_nolock(self);
    pthread_mutex_unlock(&self->lock);
    return data;
}

static Object* impl_get(List *self, int index) {
    pthread_mutex_lock(&self->lock);
    if (index < 0 || index >= self->size) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }

    ListNode *curr;
    if (index < (self->size / 2)) {
        curr = self->head;
        for (int i = 0; i < index; i++) curr = curr->next;
    } else {
        curr = self->tail;
        for (int i = self->size - 1; i > index; i--) curr = curr->prev;
    }

    Object *data = curr->data;
    pthread_mutex_unlock(&self->lock);
    return data; // GET은 BORROWED 개념 (필요시 호출자가 직접 RETAIN)
}

// 🚀 [수정] insertAt 데드락 완전 해결! _nolock 내부 함수 호출!
static void impl_insertAt(List *self, int index, Object *data) {
    pthread_mutex_lock(&self->lock);

    if (index <= 0) {
        _pushFront_nolock(self, data); // 데드락 없음!
        pthread_mutex_unlock(&self->lock);
        return;
    } else if (index >= self->size) {
        _pushBack_nolock(self, data);  // 데드락 없음!
        pthread_mutex_unlock(&self->lock);
        return;
    }

    ListNode *node = (ListNode*)malloc(sizeof(ListNode));
    if (!node) {
        pthread_mutex_unlock(&self->lock);
        return;
    }
    node->data = RETAIN(data); // 소유권 획득!

    ListNode *curr;
    if (index < (self->size / 2)) {
        curr = self->head;
        for (int i = 0; i < index; i++) curr = curr->next;
    } else {
        curr = self->tail;
        for (int i = self->size - 1; i > index; i--) curr = curr->prev;
    }

    node->prev = curr->prev;
    node->next = curr;
    curr->prev->next = node;
    curr->prev = node;

    self->size++;
    pthread_mutex_unlock(&self->lock);
}

static Object* impl_removeAt(List *self, int index) {
    pthread_mutex_lock(&self->lock);

    if (index < 0 || index >= self->size) {
        pthread_mutex_unlock(&self->lock);
        return NULL;
    }
    if (index == 0) {
        Object* data = _popFront_nolock(self); // 데드락 없음!
        pthread_mutex_unlock(&self->lock);
        return data;
    } else if (index == self->size - 1) {
        Object* data = _popBack_nolock(self);  // 데드락 없음!
        pthread_mutex_unlock(&self->lock);
        return data;
    }

    ListNode *curr;
    if (index < (self->size / 2)) {
        curr = self->head;
        for (int i = 0; i < index; i++) curr = curr->next;
    } else {
        curr = self->tail;
        for (int i = self->size - 1; i > index; i--) curr = curr->prev;
    }

    Object *data = curr->data; // 소유권 이전
    curr->prev->next = curr->next;
    curr->next->prev = curr->prev;

    free(curr);
    self->size--;
    pthread_mutex_unlock(&self->lock);
    return data;
}

static void impl_clear(List *self) {
    pthread_mutex_lock(&self->lock);
    ListNode *curr = self->head;
    while (curr) {
        ListNode *next = curr->next;
        RELEASE(curr->data); // 🚀 [수정] 노드 파괴 시 내부 데이터 같이 파괴!
        free(curr);
        curr = next;
    }
    self->head = self->tail = NULL;
    self->size = 0;
    pthread_mutex_unlock(&self->lock);
}

static int impl_getSize(List *self) {
    pthread_mutex_lock(&self->lock);
    int s = self->size;
    pthread_mutex_unlock(&self->lock);
    return s;
}

/* =========================================
 * [ARC v2.0] Object 클래스 연동
 * ========================================= */
static void List_Finalize(Object* obj) {
    List* self = (List*)obj;
    impl_clear(self);
    pthread_mutex_destroy(&self->lock);
}

static void List_ToString(Object* obj, char* buffer, size_t len) {
    List* self = (List*)obj;
    snprintf(buffer, len, "List(Size=%d)", impl_getSize(self));
}

const Class listClass = {
    .name = "List",
    .size = sizeof(List),
    .toString = List_ToString,
    .finalize = List_Finalize
};

/* =========================================
 * [생성자]
 * ========================================= */
List* new_List(void) {
    List *list = (List*)calloc(1, sizeof(List));
    if (!list) return NULL;

    Object_Init((Object*)list, &listClass);
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;

    pthread_mutex_init(&list->lock, NULL);

    list->pushBack  = impl_pushBack;
    list->pushFront = impl_pushFront;
    list->popBack   = impl_popBack;
    list->popFront  = impl_popFront;
    list->insertAt  = impl_insertAt;
    list->removeAt  = impl_removeAt;
    list->get       = impl_get;
    list->clear     = impl_clear;
    list->getSize   = impl_getSize;

    return list;
}