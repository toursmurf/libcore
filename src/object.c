#include "object.h"

/* =========================================
 * 1. 생성자 헬퍼 (초기화)
 * ========================================= */
void Object_Init(Object* obj, const Class* type) {
    if (obj) {
        obj->type = type;
        // [ARC 추가] 태어나는 순간 참조 카운트는 1로 초기화
        atomic_init(&obj->ref_count, 1);
    }
}

/* =========================================
 * 2. 타입 체크 (instanceOf) - 원본 유지
 * ========================================= */
bool instanceOf(Object* obj, const Class* targetType) {
    if (!obj || !obj->type || !targetType) return false;
    return obj->type == targetType;
}

/* =========================================
 * 3. 공통 메서드 래퍼 - 원본 유지 및 보강
 * ========================================= */

void toString(Object* obj, char* buffer, size_t len) {
    if (!obj) {
        snprintf(buffer, len, "null");
        return;
    }
    if (obj->type && obj->type->toString) {
        obj->type->toString(obj, buffer, len);
    } else {
        const char* name = (obj->type && obj->type->name) ? obj->type->name : "Object";
        snprintf(buffer, len, "%s@%p", name, obj);
    }
}

bool equals(Object* obj, Object* other) {
    if (obj == other) return true;
    if (!obj || !other) return false;
    if (obj->type != other->type) return false;
    if (obj->type && obj->type->equals) {
        return obj->type->equals(obj, other);
    }
    return false;
}

int hashCode(Object* obj) {
    if (!obj) return 0;
    if (obj->type && obj->type->hashCode) {
        return obj->type->hashCode(obj);
    }
    size_t addr = (size_t)obj;
    return (int)(addr ^ (addr >> 32));
}

// 추가해야 함:
void destroy(Object* obj) {
    if (!obj) return;
    if (obj->type && obj->type->finalize)
        obj->type->finalize(obj);
    free(obj);  // ← 여기서만 free!
}

/// ✅ void* 에서 Object* 로 인자 타입을 변경하여 에러 해결!
 static void Object_DefaultFinalize(Object* self) {
     // 기본 객체는 추가 해제할 자원이 없음
     (void)self;
 }

 static void Object_DefaultToString(Object* self, char* buffer, size_t len) {
     snprintf(buffer, len, "Object@%p", self);
 }

 // 기본 VTable 정의
 static Class _ObjectClass = {
     .name = "Object",
     .size = sizeof(Object),
     .toString = Object_DefaultToString,
     .equals = NULL,
     .hashCode = NULL,
     .finalize = Object_DefaultFinalize // 이제 타입이 완벽히 일치합니다!
 };

Class* ObjectClass = &_ObjectClass;
