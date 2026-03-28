#ifndef STRING_OBJ_H
#define STRING_OBJ_H

#include <stdbool.h>
#include "object.h"
#include "arraylist.h"

/* 투스 IT 홀딩스: String 클래스 메타데이터 */
extern const Class stringClass;

typedef struct String String;

struct String {
    // 1. 상속 (Object base는 반드시 첫 번째 멤버)
    Object base;

    // 2. 멤버 변수
    char* value;
    int length;

    // 3. 메서드 인터페이스 (함수 포인터)
    int (*length_f)(String* self);
    char (*charAt)(String* self, int index);
    bool (*equals)(String* self, const char* another);
    int (*indexOf)(String* self, const char* str);
    String* (*substring)(String* self, int begin, int end);
    String* (*concat)(String* self, const char* str);
    String* (*trim)(String* self);

    void (*append)(String* self, const char* str);
    void (*delete)(String* self, const char* target);
    void (*clear)(String* self);
    String* (*copy)(String* self);
    bool (*isEmpty)(String* self);
    void (*toUpperCase)(String* self);
    void (*toLowerCase)(String* self);

    // 타입 변환 메서드
    int (*toInt)(Object *obj);
    long long (*toLong)(Object *obj);
    double (*toDouble)(Object *obj);

    // 확장 기능
    String* (*reverse)(String* self);
    String* (*implode)(String* self, ArrayList *array);
    String* (*replace)(String* self, const char* target, const char* replacement);
    ArrayList* (*split)(String* self, const char* delimiter);
    ArrayList* (*explode)(String* self, const char* delimiter);
    void (*free)(String* self);
    void (*destroy)(String* self);
};

/* [Constructor] 신규 문자열 객체 생성 */
String* new_String(const char* init_str);

/* [Static Helper] 문자열 결합 유틸리티 */
char* string_join(const char* delimiter, const char** str_array, int count);

#endif