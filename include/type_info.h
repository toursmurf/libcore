#ifndef TYPE_INFO_H
#define TYPE_INFO_H

/* =================================================================
 * [Toos IT Holdings] libcore 객체 주민등록번호 (Type ID)
 * ================================================================= */
typedef enum {
    TYPE_OBJECT = 0,

    // 1. Primitive & String
    TYPE_STRING,         // string_obj.c

    // 2. Linear Collections
    TYPE_ARRAYLIST,      // arraylist.c
    TYPE_VECTOR,         // vector.c
    TYPE_LINKEDLIST,     // linked_list.c (기존 DLIST 대체/확장)
    TYPE_LIST,           // list.c (인터페이스형)
    
    // 3. Adapters (LIFO / FIFO)
    TYPE_STACK,          // stack.c
    TYPE_QUEUE,          // queue.c

    // 4. Map & Hash Collections
    TYPE_HASHMAP,        // hashmap.c
    TYPE_HASHTABLE,      // hashtable.c

    // 5. Tree Collections
    TYPE_TREE,           // tree.c (BST)
    TYPE_BTREE,          // btree.c

    // 6. Concurrency & Threading
    TYPE_THREAD,         // thread.c
    TYPE_THREADPOOL,     // threadpool.c
    TYPE_SEMAPHORE,      // semaphore_obj.c

    // 7. JSON Engine
    TYPE_JSON_NODE,      // json.c (자바 스타일 스마트 래퍼)
    TYPE_JSON_VALUE,     // json.c (리프 노드: String, Number, Bool 등)

    /* ============================================================
     * 사용자 정의(User-Defined) 영역: 100번부터 시작 (코어 충돌 방지)
     * ============================================================ */
    TYPE_SENSOR_DATA = 100,
    TYPE_USER_DEFINED = 101

} TypeId;

#endif // TYPE_INFO_H
