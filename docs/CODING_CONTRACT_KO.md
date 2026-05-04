# libcore Coding Contract

> **본 문서는 libcore 프레임워크 기반 코드 작성 시 반드시 준수해야 하는 절대 규약입니다.**  
> 단 하나의 규칙이라도 위반된 경우, 해당 코드는 전면 폐기하고 재작성하여야 합니다.  
> 부분 수정은 허용되지 않습니다.

---

## 적용 범위

- libcore 모든 모듈 (공개 / 비공개)
- libcore 기반 확장 모듈
- AI 코드 생성 시 프롬프트 규약
- 코드 검수 및 리뷰 기준

---

## Rule 1. 객체 시스템

모든 libcore 구조체는 반드시 OOP 패턴을 따라야 합니다.

1. 모든 구조체의 첫 번째 멤버는 반드시 `Object base` 이어야 합니다.
2. `Object_Init()` 없이 객체를 생성하여서는 안 됩니다.
3. `Class` (VTable) 정의를 반드시 작성하여야 합니다.
4. `finalize` 함수를 반드시 구현하여야 합니다. (빈 함수라도 반드시 존재해야 합니다.)

```c
// ✅ 올바른 예시
typedef struct MyClass MyClass;
struct MyClass {
    Object base;          // 첫 번째 멤버는 반드시 Object base
    int    value;
    void (*doSomething)(MyClass* self);
};

static void MyClass_finalize(Object* obj) {
    MyClass* self = (MyClass*)obj;
    // 리소스 해제
}

static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),   // .size 반드시 명시
    .finalize = MyClass_finalize,  // .finalize 반드시 연결
};

MyClass* new_MyClass(int value) {
    MyClass* self = calloc(1, sizeof(MyClass));
    if (!self) return NULL;
    Object_Init((Object*)self, &_myClass);  // 반드시 호출
    self->value = value;
    self->doSomething = MyClass_doSomething;
    return self;
}
```

---

## Rule 2. Class 정의

`Class` 구조체를 정의할 때 반드시 다음 세 가지 필드를 모두 명시하여야 합니다.

1. `.name` 을 반드시 명시하여야 합니다.
2. `.size = sizeof(ClassName)` 을 반드시 명시하여야 합니다. 누락 시 ARC가 오작동합니다.
3. `.finalize` 를 반드시 연결하여야 합니다. `NULL` 은 허용되지 않습니다.

```c
// ❌ 잘못된 예시 — 즉시 폐기
static const Class _myClass = {
    .name     = "MyClass",
    .finalize = MyClass_finalize,
    // .size 누락 → ARC 오작동
};

// ✅ 올바른 예시
static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),   // 반드시 명시
    .finalize = MyClass_finalize,
};
```

---

## Rule 3. 메모리 관리 (ARC)

libcore의 메모리 관리는 ARC(Automatic Reference Counting) 기반입니다.  
모든 코드는 Valgrind 0 bytes / ASan 0 errors / TSan 0 warnings 를 만족하여야 합니다.

1. `free()` 는 절대로 사용하여서는 안 됩니다. 반드시 `RELEASE()` 를 사용하여야 합니다.
2. 모든 객체 해제는 `RELEASE((Object*)ptr)` 형식으로 수행하여야 합니다.
3. `RETAIN()` 규칙을 반드시 준수하여야 합니다.
4. Ownership 을 반드시 명시하여야 합니다. (`[OWNED]` / `[BORROWED]`)
5. `malloc()` 사용 후 반드시 `Object_Init()` 을 호출하여야 합니다.

```c
// ❌ 절대 금지
free(self);

// ✅ 올바른 해제
RELEASE((Object*)self);

// ✅ NULL 안전 해제
RELEASE_NULL((Object**)&self);
```

---

## Rule 4. 생성자

1. 생성 함수는 반드시 `new_ClassName` 형식으로 작성하여야 합니다.
2. 생성 실패 시 반드시 `NULL` 을 반환하여야 합니다.
3. 모든 필드를 반드시 초기화하여야 합니다. (`calloc` 사용을 권장합니다.)
4. 생성 중간에 실패가 발생하면 반드시 `RELEASE()` 로 정리한 후 `NULL` 을 반환하여야 합니다.

```c
MyClass* new_MyClass(const char* name) {
    if (!name) return NULL;                      // 입력 검증

    MyClass* self = calloc(1, sizeof(MyClass));
    if (!self) return NULL;                      // 할당 실패 처리

    Object_Init((Object*)self, &_myClass);

    self->inner = new_InnerObj();
    if (!self->inner) {
        RELEASE((Object*)self);                  // 중간 실패 정리
        return NULL;
    }

    self->doSomething = MyClass_doSomething;
    return self;
}
```

---

## Rule 5. 함수 안정성

1. 모든 함수의 입력값에 대해 `NULL` 체크를 수행하여야 합니다.
2. 길이 및 크기 값은 반드시 `0` 체크를 수행하여야 합니다.
3. 포인터를 역참조하기 전에 반드시 유효성을 검사하여야 합니다.
4. 외부에서 전달받은 객체의 소유권을 반드시 명확히 처리하여야 합니다.

```c
static bool MyClass_write(MyClass* self,
                           const void* data,
                           size_t len) {
    if (!self || !data || len == 0) return false;  // NULL + 0 체크
    if (len > MAX_SIZE) return false;              // 범위 체크
    // ...
    return true;
}
```

---

## Rule 6. Thread 안전성

libcore는 멀티스레드 환경을 기본으로 합니다.  
모든 공유 자원은 반드시 보호되어야 합니다.

1. `strtok()` 은 사용하여서는 안 됩니다. 반드시 `strtok_r()` 을 사용하여야 합니다.
2. 공유 자원에는 반드시 `pthread_mutex_t` 로 Mutex 보호를 적용하여야 합니다.
3. Race condition 이 발생하지 않도록 설계하여야 합니다. (TSan 0 warnings 필수)

```c
// ❌ 절대 금지 — Thread-unsafe
char* token = strtok(buf, ",");

// ✅ 올바른 사용 — Thread-safe
char* saveptr = NULL;
char* token   = strtok_r(buf, ",", &saveptr);
while (token) {
    // 처리
    token = strtok_r(NULL, ",", &saveptr);
}

// ✅ 공유 자원 보호
pthread_mutex_lock(&self->lock);
// 공유 자원 접근
pthread_mutex_unlock(&self->lock);
```

---

## Rule 7. 시스템 리소스

1. 파일 디스크립터(`fd`)의 초기값은 반드시 `-1` 로 설정하여야 합니다. (`0` 은 `stdin` 입니다.)
2. `close()` 호출 전에 반드시 `fd >= 0` 유효성 검사를 수행하여야 합니다.
3. `close()` 후에는 반드시 `fd = -1` 로 재설정하여야 합니다. (이중 close 방지)

```c
// ✅ 올바른 초기화
self->fd = -1;

// ✅ 올바른 close 패턴
if (self->fd >= 0) {
    close(self->fd);
    self->fd = -1;  // 이중 close 방지
}
```

---

## Rule 8. 비교 연산

1. `equals` 와 `compareTo` 는 반드시 일관성을 유지하여야 합니다.
2. `equals` 가 `true` 를 반환하면 `compareTo` 는 반드시 `0` 을 반환하여야 합니다.
3. `float` 및 `double` 비교 시 반드시 epsilon 을 사용하여야 합니다. (`1e-9` 권장)

```c
// ✅ Double equals (epsilon 사용)
static bool Double_equals(Double* self, Double* other) {
    if (!self || !other) return false;
    double diff = self->value - other->value;
    if (diff < 0.0) diff = -diff;
    return diff < 1e-9;
}

// ✅ Double compareTo (equals와 동일한 epsilon 적용)
static int Double_compareTo(Double* self, Double* other) {
    if (!self || !other) return 0;
    double diff = self->value - other->value;
    if (diff < 0.0) diff = -diff;
    if (diff < 1e-9) return 0;  // equals=true → compareTo=0 보장
    if (self->value < other->value) return -1;
    return 1;
}
```

---

## Rule 9. 금지 사항

다음 패턴이 발견되면 코드 검수를 즉시 중단하고 전면 폐기하여야 합니다.

| 금지 패턴 | 이유 |
|-----------|------|
| `free(obj)` 사용 | ARC 시스템 우회 |
| `Object base` 없는 구조체 | OOP 패턴 위반 |
| `Object_Init` 누락 | ARC 등록 불가 |
| `finalize` 미구현 | 메모리 누수 확정 |
| `.size` 누락 | ARC 할당 오류 |
| `strtok` 사용 | Thread-unsafe |
| Mutex 없는 공유 자원 접근 | Race condition |
| `fd` 초기화 없음 (`= 0`) | stdin 오작동 |
| `equals` / `compareTo` 불일치 | 정렬 오작동 |
| `float` / `double` epsilon 없음 | 비교 오류 |
| 일반 C 스타일 구현 | libcore 패턴 위반 |
| 순환 참조 구조 | ARC 해제 불가 |

---

## Rule 10. 결과 요구 조건

모든 libcore 코드는 다음 조건을 반드시 만족하여야 합니다.

| 조건 | 검증 방법 |
|------|-----------|
| 컴파일 가능 | `gcc -Wall -Werror` 통과 |
| 메모리 누수 없음 | Valgrind `0 bytes in 0 blocks` |
| 메모리 오버플로우 없음 | ASan `ERROR SUMMARY: 0 errors` |
| Race condition 없음 | TSan `0 warnings` |
| ARC 100% 준수 | RELEASE / RETAIN 정확 |
| Thread-safe | Mutex 보호 완비 |
| libcore 완전 호환 | 기존 패턴 준수 |

```bash
# 3중 검증 (Iron Fortress 기준)

# 1. Valgrind
valgrind --leak-check=full ./binary
# → "0 bytes in 0 blocks" 필수

# 2. ASan
gcc -fsanitize=address -o binary ./src && ./binary
# → "ERROR SUMMARY: 0 errors" 필수

# 3. TSan
gcc -fsanitize=thread -o binary ./src && ./binary
# → "WARNING: 0 warnings" 필수
```

---

## Rule 11. 순환 참조 방지

ARC의 가장 치명적인 약점은 순환 참조(Circular Reference)입니다.  
`A → B → A` 구조가 형성되면 참조 카운트가 절대 0이 되지 않아 메모리가 영원히 해제되지 않습니다.

1. 상호 참조 시 ownership 방향을 반드시 명시하여야 합니다. (단방향 소유만 허용)
2. 역방향 참조가 필요한 경우 weak reference (RETAIN 없이 포인터만) 를 사용하여야 합니다.
3. 순환 참조 가능성이 있는 구조는 설계 단계에서 반드시 차단하여야 합니다.

```c
// ❌ 순환 참조 — 메모리 누수 확정
struct Parent {
    Object  base;
    Child*  child;   // [OWNED] Parent → Child RETAIN
};
struct Child {
    Object  base;
    Parent* parent;  // [OWNED] Child → Parent RETAIN → 순환!!
};
// RELEASE(parent) 해도 child가 parent를 RETAIN하고 있어
// 참조 카운트가 절대 0이 되지 않음 → 누수

// ✅ 올바른 패턴 — weak reference
struct Parent {
    Object  base;
    Child*  child;   // [OWNED] → RETAIN
};
struct Child {
    Object  base;
    Parent* parent;  // [BORROWED] → RETAIN 없이 포인터만 (weak reference)
};
// RELEASE(parent) 시 정상 해제
```

---

## 최종 선언

```
이 규칙을 반드시 준수하여 코드를 작성하여야 합니다.
일반적인 C 방식으로 구현하여서는 안 됩니다.
libcore 시스템을 반드시 유지하여야 합니다.
단 하나의 위반도 용납되지 않습니다.
```

---

*Toos IT Holdings | libcore v1.1 Iron Fortress | 2026*
