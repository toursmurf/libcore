#ifndef REGEX_OBJ_H
#define REGEX_OBJ_H

#include "object.h"
#include "string_obj.h"
#include "arraylist.h"
#include <regex.h>
#include <stdbool.h>
#include <string.h>

typedef struct Regex Regex;

struct Regex {
    Object base;

    regex_t compiled;
    char* original_pattern;
    int cflags;
    bool is_compiled;

    bool (*matches)(Regex* self, const char* str);
    int  (*search)(Regex* self, const char* str);

    // [OWNED] 반환된 ArrayList는 호출자가 다 쓴 후 RELEASE 해야 합니다.
    ArrayList* (*findAll)(Regex* self, const char* text);

    int (*matchCount)(Regex* self, const char* text);

    // [BORROWED] 반환된 문자열 포인터는 해제(free/RELEASE) 불가합니다.
    const char* (*getPattern)(Regex* self);

    int (*getFlags)(Regex* self);
};

// [OWNED] 생성된 Regex 객체는 호출자가 책임지고 RELEASE 해야 합니다.
Regex* new_Regex(const char* pattern, int flags, int* err);

#endif // REGEX_OBJ_H