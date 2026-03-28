#ifndef HASHMAP_H
#define HASHMAP_H

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "arraylist.h"
#include "string_obj.h"

/* 투스 IT 홀딩스: HashMap 클래스 메타데이터 */
extern const Class hashMapClass;

typedef struct HashMap HashMap;
typedef struct HashNode HashNode;

#define MAX_KEY_LEN 128

/* [HashNode] 내부에 Key(문자열)와 Value(Object)를 보관하는 노드 */
struct HashNode {
    char* key;
    Object* value; // [ARC] 모든 타입을 수용하며 참조 카운트로 관리됨
    struct HashNode* next;
};

/* [HashMap] 의장님 스타일의 가상 함수 인터페이스와 Object 상속 구조 */
struct HashMap {
    // [상속] 라이브러리 연동 및 ARC를 위한 최상위 객체
    Object base;

    HashNode** buckets;
    int capacity;
    int size;
    float loadFactor;
    pthread_mutex_t lock;

    // [인터페이스] 의장님 요청 메서드 바인딩
    void (*put)(HashMap* self, const char* key, Object* value);
    Object* (*get)(HashMap* self, const char* key);
    void (*remove)(HashMap* self, const char* key);
    void (*clear)(HashMap* self);
    Object* (*detach)(HashMap *self, const char *key);

    // 람다식처럼 쓸 수 있는 forEach
    void (*forEach)(HashMap* self, void (*action)(const char* key, Object* value));

    // 추가 유틸리티
    int (*getSize)(HashMap* self);
    bool (*isEmpty)(HashMap* self);

    // Key와 Value를 ArrayList로 반환 (ARC 적용으로 Deep Copy 불필요)
    ArrayList* (*keys)(HashMap* self);
    ArrayList* (*values)(HashMap* self);

    // 소멸 관련
    void (*free)(HashMap* self);
    void (*destroy)(HashMap* self);
};

/* 생성자: PascalCase 명명 규칙 적용 */
HashMap* new_HashMap(int initial_capacity);

#endif // HASHMAP_H