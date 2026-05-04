#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#include "object.h"
#include "string_obj.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Integer Integer;
struct Integer {
    Object base;
    int    value;

    int     (*intValue)   (Integer* self);
    long    (*longValue)  (Integer* self);
    double  (*doubleValue)(Integer* self);
    bool    (*equals)     (Integer* self, Integer* other);
    int     (*compareTo)  (Integer* self, Integer* other);
    String* (*toString)   (Integer* self);
};

Integer* new_Integer(int value);

typedef struct Long Long;
struct Long {
    Object    base;
    long long value;

    long long (*longValue)  (Long* self);
    int       (*intValue)   (Long* self);
    double    (*doubleValue)(Long* self);
    bool      (*equals)     (Long* self, Long* other);
    int       (*compareTo)  (Long* self, Long* other);
    String* (*toString)   (Long* self);
};

Long* new_Long(long long value);

typedef struct Double Double;
struct Double {
    Object base;
    double value;

    double  (*doubleValue)(Double* self);
    int     (*intValue)   (Double* self);
    long    (*longValue)  (Double* self);
    bool    (*equals)     (Double* self, Double* other);
    int     (*compareTo)  (Double* self, Double* other);
    String* (*toString)   (Double* self);
};

Double* new_Double(double value);

typedef struct Boolean Boolean;
struct Boolean {
    Object base;
    bool   value;

    bool    (*boolValue)(Boolean* self);
    bool    (*equals)   (Boolean* self, Boolean* other);
    String* (*toString) (Boolean* self);
};

Boolean* new_Boolean(bool value);

typedef struct Byte Byte;
struct Byte {
    Object  base;
    uint8_t value;

    uint8_t (*byteValue)(Byte* self);
    int     (*intValue) (Byte* self);
    bool    (*equals)   (Byte* self, Byte* other);
    String* (*toString) (Byte* self);
};

Byte* new_Byte(uint8_t value);

#define INT(v)    new_Integer(v)
#define LONG(v)   new_Long(v)
#define DOUBLE(v) new_Double(v)
#define BOOL(v)   new_Boolean(v)
#define BYTE(v)   new_Byte(v)

#endif // PRIMITIVE_H