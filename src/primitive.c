#include "primitive.h"
#include <stdlib.h>
#include <stdio.h>

static int Integer_intValue(Integer* self) {
    return self ? self->value : 0;
}

static long Integer_longValue(Integer* self) {
    return self ? (long)self->value : 0L;
}

static double Integer_doubleValue(Integer* self) {
    return self ? (double)self->value : 0.0;
}

static bool Integer_equals(Integer* self, Integer* other) {
    if (!self || !other) {
        return false;
    }
    return self->value == other->value;
}

static int Integer_compareTo(Integer* self, Integer* other) {
    if (!self || !other) {
        return 0;
    }
    if (self->value < other->value) {
        return -1;
    }
    if (self->value > other->value) {
        return 1;
    }
    return 0;
}

static String* Integer_toString(Integer* self) {
    if (!self) {
        return NULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", self->value);
    return new_String(buf);
}

static void Integer_finalize(Object* obj) {
    (void)obj;
}

static const Class _integerClass = {
    .name     = "Integer",
    .size     = sizeof(Integer),
    .finalize = Integer_finalize
};

Integer* new_Integer(int value) {
    Integer* self = (Integer*)calloc(1, sizeof(Integer));
    if (!self) {
        return NULL;
    }
    Object_Init((Object*)self, &_integerClass);

    self->value       = value;
    self->intValue    = Integer_intValue;
    self->longValue   = Integer_longValue;
    self->doubleValue = Integer_doubleValue;
    self->equals      = Integer_equals;
    self->compareTo   = Integer_compareTo;
    self->toString    = Integer_toString;

    return self;
}

static long long Long_longValue(Long* self) {
    return self ? self->value : 0LL;
}

static int Long_intValue(Long* self) {
    return self ? (int)self->value : 0;
}

static double Long_doubleValue(Long* self) {
    return self ? (double)self->value : 0.0;
}

static bool Long_equals(Long* self, Long* other) {
    if (!self || !other) {
        return false;
    }
    return self->value == other->value;
}

static int Long_compareTo(Long* self, Long* other) {
    if (!self || !other) {
        return 0;
    }
    if (self->value < other->value) {
        return -1;
    }
    if (self->value > other->value) {
        return 1;
    }
    return 0;
}

static String* Long_toString(Long* self) {
    if (!self) {
        return NULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", self->value);
    return new_String(buf);
}

static void Long_finalize(Object* obj) {
    (void)obj;
}

static const Class _longClass = {
    .name     = "Long",
    .size     = sizeof(Long),
    .finalize = Long_finalize
};

Long* new_Long(long long value) {
    Long* self = (Long*)calloc(1, sizeof(Long));
    if (!self) {
        return NULL;
    }
    Object_Init((Object*)self, &_longClass);

    self->value       = value;
    self->longValue   = Long_longValue;
    self->intValue    = Long_intValue;
    self->doubleValue = Long_doubleValue;
    self->equals      = Long_equals;
    self->compareTo   = Long_compareTo;
    self->toString    = Long_toString;

    return self;
}

static double Double_doubleValue(Double* self) {
    return self ? self->value : 0.0;
}

static int Double_intValue(Double* self) {
    return self ? (int)self->value : 0;
}

static long Double_longValue(Double* self) {
    return self ? (long)self->value : 0L;
}

static bool Double_equals(Double* self, Double* other) {
    if (!self || !other) {
        return false;
    }

    double diff = self->value - other->value;
    if (diff < 0.0) {
        diff = -diff;
    }

    return diff < 1e-9;
}

static int Double_compareTo(Double* self, Double* other) {
    if (!self || !other) {
        return 0;
    }

    double diff = self->value - other->value;
    if (diff < 0.0) {
        diff = -diff;
    }

    if (diff < 1e-9) {
        return 0;
    }

    if (self->value < other->value) {
        return -1;
    }
    return 1;
}

static String* Double_toString(Double* self) {
    if (!self) {
        return NULL;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", self->value);
    return new_String(buf);
}

static void Double_finalize(Object* obj) {
    (void)obj;
}

static const Class _doubleClass = {
    .name     = "Double",
    .size     = sizeof(Double),
    .finalize = Double_finalize
};

Double* new_Double(double value) {
    Double* self = (Double*)calloc(1, sizeof(Double));
    if (!self) {
        return NULL;
    }
    Object_Init((Object*)self, &_doubleClass);

    self->value       = value;
    self->doubleValue = Double_doubleValue;
    self->intValue    = Double_intValue;
    self->longValue   = Double_longValue;
    self->equals      = Double_equals;
    self->compareTo   = Double_compareTo;
    self->toString    = Double_toString;

    return self;
}

static bool Boolean_boolValue(Boolean* self) {
    return self ? self->value : false;
}

static bool Boolean_equals(Boolean* self, Boolean* other) {
    if (!self || !other) {
        return false;
    }
    return self->value == other->value;
}

static String* Boolean_toString(Boolean* self) {
    if (!self) {
        return NULL;
    }
    return new_String(self->value ? "true" : "false");
}

static void Boolean_finalize(Object* obj) {
    (void)obj;
}

static const Class _booleanClass = {
    .name     = "Boolean",
    .size     = sizeof(Boolean),
    .finalize = Boolean_finalize
};

Boolean* new_Boolean(bool value) {
    Boolean* self = (Boolean*)calloc(1, sizeof(Boolean));
    if (!self) {
        return NULL;
    }
    Object_Init((Object*)self, &_booleanClass);

    self->value     = value;
    self->boolValue = Boolean_boolValue;
    self->equals    = Boolean_equals;
    self->toString  = Boolean_toString;

    return self;
}

static uint8_t Byte_byteValue(Byte* self) {
    return self ? self->value : 0;
}

static int Byte_intValue(Byte* self) {
    return self ? (int)self->value : 0;
}

static bool Byte_equals(Byte* self, Byte* other) {
    if (!self || !other) {
        return false;
    }
    return self->value == other->value;
}

static String* Byte_toString(Byte* self) {
    if (!self) {
        return NULL;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", self->value);
    return new_String(buf);
}

static void Byte_finalize(Object* obj) {
    (void)obj;
}

static const Class _byteClass = {
    .name     = "Byte",
    .size     = sizeof(Byte),
    .finalize = Byte_finalize
};

Byte* new_Byte(uint8_t value) {
    Byte* self = (Byte*)calloc(1, sizeof(Byte));
    if (!self) {
        return NULL;
    }
    Object_Init((Object*)self, &_byteClass);

    self->value     = value;
    self->byteValue = Byte_byteValue;
    self->intValue  = Byte_intValue;
    self->equals    = Byte_equals;
    self->toString  = Byte_toString;

    return self;
}