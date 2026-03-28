#include <stdio.h>
#include <stdlib.h>
#include "libcore.h"

/* =========================================
 * [Object] Overrides
 * ========================================= */
static void LinkedList_ToString(Object *self, char *buffer, size_t len) {
    LinkedList *list = (LinkedList*)self;
    snprintf(buffer, len, "LinkedList(size=%d)", list->size);
}

// 🚀 [ARC 핵심] 기존 free_list를 대체하는 완벽한 연쇄 소각!
static void LinkedList_Finalize(Object *self) {
    LinkedList *list = (LinkedList*)self;
    LinkedListNode* curr = list->head; // 🚀 Node -> LinkedListNode 변경
    LinkedListNode* next;              // 🚀 Node -> LinkedListNode 변경

    while (curr != NULL) {
        next = curr->next;
        // 1. 노드가 쥐고 있던 데이터의 소유권 해제! (메모리 누수 방지)
        if (curr->data) {
            RELEASE_NULL(curr->data);
        }
        // 2. 노드 자체 메모리 반납
        free(curr);
        curr = next;
    }
    pthread_mutex_destroy(&list->lock);
}

const Class linkedListClass = {
    .name = "LinkedList",
    .size = sizeof(LinkedList),
    .toString = LinkedList_ToString,
    .finalize = LinkedList_Finalize
};

/* =========================================
 * [Methods] Implementation
 * ========================================= */

// 노드 추가 (의장님 로직 + ARC + Mutex)
static void impl_add_node(LinkedList* self, void* data) {
    pthread_mutex_lock(&self->lock);

    LinkedListNode* new_node = (LinkedListNode*)malloc(sizeof(LinkedListNode)); // 🚀 변경
    new_node->data = (Object*)data;
    RETAIN(new_node->data); // 🚀 [ARC] 리스트가 소유권 확보!
    new_node->next = NULL;

    if (self->head == NULL) {
        self->head = new_node;
    } else {
        LinkedListNode* temp = self->head; // 🚀 변경
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = new_node;
    }
    self->size++;

    pthread_mutex_unlock(&self->lock);
}

// 노드 삭제 (의장님 로직 + ARC 소유권 반납)
static void impl_delete_node(LinkedList* self, void* data, int (*compare)(Object*, Object*)) {
    pthread_mutex_lock(&self->lock);

    LinkedListNode* curr = self->head; // 🚀 변경
    LinkedListNode* prev = NULL;       // 🚀 변경

    while (curr != NULL) {
        if (compare(curr->data, (Object*)data) == 0) {
            if (prev == NULL) {
                self->head = curr->next;
            } else {
                prev->next = curr->next;
            }
            // 🚀 [ARC] 리스트에서 빠져나가므로 소유권 즉시 해제!
            RELEASE_NULL(curr->data);
            free(curr);
            self->size--;

            pthread_mutex_unlock(&self->lock);
            return;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&self->lock);
}

// 리스트 출력
static void impl_print_list(LinkedList* self, void (*display)(Object*)) {
    pthread_mutex_lock(&self->lock);
    LinkedListNode* curr = self->head; // 🚀 변경
    while (curr != NULL) {
        display(curr->data);
        curr = curr->next;
    }
    printf("NULL\n");
    pthread_mutex_unlock(&self->lock);
}

static int impl_get_size(LinkedList* self) {
    pthread_mutex_lock(&self->lock);
    int s = self->size;
    pthread_mutex_unlock(&self->lock);
    return s;
}

/* =========================================
 * [Constructor] create_list를 대체
 * ========================================= */
LinkedList* new_LinkedList(void) {
    LinkedList* list = (LinkedList*)malloc(sizeof(LinkedList));
    if (!list) return NULL;

    Object_Init((Object*)list, &linkedListClass);
    list->head = NULL;
    list->size = 0;
    pthread_mutex_init(&list->lock, NULL);

    // 메서드 바인딩 (의장님 인터페이스 보존)
    list->add_node = impl_add_node;
    list->delete_node = impl_delete_node;
    list->print_list = impl_print_list;
    list->get_size = impl_get_size;
    list->getSize = impl_get_size;

    return list;
}
