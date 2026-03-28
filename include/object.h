#ifndef OBJECT_H
#define OBJECT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h> // [ARC 추가] 원자적 연산을 위해

/* =========================================
 * 1. 핵심 타입 정의 (순서 중요)
 * ========================================= */

typedef struct Object Object;
typedef struct Class Class;
// get ref count
#define REF_COUNT(obj) \
    (((Object*)(obj))->ref_count)

struct Class {
    const char* name;
    size_t size;

    // VTable
    void (*toString)(Object* self, char* buffer, size_t len);
    bool (*equals)(Object* self, Object* other);
    int  (*hashCode)(Object* self);
    void (*finalize)(Object* self); // [ARC] 참조 카운트가 0일 때 호출됨
};

struct Object {
    const Class* type;
    atomic_int ref_count; // [ARC 추가] 원자적 참조 카운트
};

/* =========================================
 * 2. ARC 절대 매크로 (소유권 관리의 핵심)
 * ========================================= */

// RETAIN: 소유권 획득 (카운트 증가)
#define RETAIN(obj) \
    ((obj) ? (atomic_fetch_add(&((Object*)(obj))->ref_count, 1), (obj)) : NULL)

// RELEASE: 소유권 반납 (카운트 감소 및 0일 때 소멸)
#define RELEASE(obj) do { \
    if (obj) { \
        if (atomic_fetch_sub(&((Object*)(obj))->ref_count, 1) == 1) { \
            destroy((Object*)(obj)); \
        } \
    } \
} while(0)

// RELEASE_NULL: 해제 후 댕글링 포인터 방지
#define RELEASE_NULL(obj) do { \
    RELEASE(obj); \
    (obj) = NULL; \
} while(0)


/* =========================================
 * 3. 공통 유틸리티 함수 선언
 * ========================================= */

void Object_Init(Object* obj, const Class* type);
bool instanceOf(Object* obj, const Class* targetType);
void toString(Object* obj, char* buffer, size_t len);
bool equals(Object* obj, Object* other);
int hashCode(Object* obj);
void destroy(Object* obj); // [내부용] 실제 메모리 해제 로직

#endif // OBJECT_H
