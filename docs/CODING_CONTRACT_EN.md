# libcore Coding Contract

> **This document defines the absolute coding contract that must be followed when writing code based on the libcore framework.**  
> If any single rule is violated, the entire code must be discarded and rewritten from scratch.  
> Partial fixes are not permitted.

---

## Scope

- All libcore modules (public / private)
- libcore-based extension modules
- AI code generation prompt contract
- Code review and inspection standard

---

## Rule 1. Object System

All libcore structs must follow the OOP pattern.

1. The first member of every struct must be `Object base`.
2. Objects must not be created without calling `Object_Init()`.
3. A `Class` (VTable) definition must always be provided.
4. A `finalize` function must always be implemented. (Even an empty body is required.)

```c
// ✅ Correct example
typedef struct MyClass MyClass;
struct MyClass {
    Object base;          // First member must be Object base
    int    value;
    void (*doSomething)(MyClass* self);
};

static void MyClass_finalize(Object* obj) {
    MyClass* self = (MyClass*)obj;
    // Release resources
}

static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),   // .size must be specified
    .finalize = MyClass_finalize,  // .finalize must be connected
};

MyClass* new_MyClass(int value) {
    MyClass* self = calloc(1, sizeof(MyClass));
    if (!self) return NULL;
    Object_Init((Object*)self, &_myClass);  // Must be called
    self->value = value;
    self->doSomething = MyClass_doSomething;
    return self;
}
```

---

## Rule 2. Class Definition

When defining a `Class` struct, all three of the following fields must be specified.

1. `.name` must be specified.
2. `.size = sizeof(ClassName)` must be specified. Omitting it will cause ARC to malfunction.
3. `.finalize` must be connected. `NULL` is not permitted.

```c
// ❌ Incorrect — discard immediately
static const Class _myClass = {
    .name     = "MyClass",
    .finalize = MyClass_finalize,
    // .size missing → ARC malfunction
};

// ✅ Correct
static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),   // Must be specified
    .finalize = MyClass_finalize,
};
```

---

## Rule 3. Memory Management (ARC)

libcore memory management is based on ARC (Automatic Reference Counting).  
All code must satisfy: Valgrind 0 bytes / ASan 0 errors / TSan 0 warnings.

1. `free()` must never be used. `RELEASE()` must be used instead.
2. All object deallocation must use the form `RELEASE((Object*)ptr)`.
3. `RETAIN()` rules must be strictly followed.
4. Ownership must always be explicitly stated. (`[OWNED]` / `[BORROWED]`)
5. After calling `malloc()`, `Object_Init()` must always be called.

```c
// ❌ Absolutely prohibited
free(self);

// ✅ Correct deallocation
RELEASE((Object*)self);

// ✅ NULL-safe deallocation
RELEASE_NULL((Object**)&self);
```

---

## Rule 4. Constructor

1. Constructor functions must always follow the `new_ClassName` naming convention.
2. On failure, `NULL` must be returned.
3. All fields must be initialized. (Use of `calloc` is recommended.)
4. If a failure occurs mid-construction, cleanup must be performed using `RELEASE()` before returning `NULL`.

```c
MyClass* new_MyClass(const char* name) {
    if (!name) return NULL;                      // Input validation

    MyClass* self = calloc(1, sizeof(MyClass));
    if (!self) return NULL;                      // Allocation failure

    Object_Init((Object*)self, &_myClass);

    self->inner = new_InnerObj();
    if (!self->inner) {
        RELEASE((Object*)self);                  // Mid-construction cleanup
        return NULL;
    }

    self->doSomething = MyClass_doSomething;
    return self;
}
```

---

## Rule 5. Function Safety

1. All function inputs must be NULL-checked.
2. Length and size values must be checked for `0`.
3. Pointer validity must be verified before dereferencing.
4. Ownership of externally received objects must be explicitly handled.

```c
static bool MyClass_write(MyClass* self,
                           const void* data,
                           size_t len) {
    if (!self || !data || len == 0) return false;  // NULL + zero check
    if (len > MAX_SIZE) return false;              // Range check
    // ...
    return true;
}
```

---

## Rule 6. Thread Safety

libcore assumes a multi-threaded environment.  
All shared resources must be protected.

1. `strtok()` must not be used. `strtok_r()` must be used instead.
2. Shared resources must always be protected using `pthread_mutex_t`.
3. Code must be designed to eliminate race conditions. (TSan 0 warnings required)

```c
// ❌ Prohibited — Thread-unsafe
char* token = strtok(buf, ",");

// ✅ Correct — Thread-safe
char* saveptr = NULL;
char* token   = strtok_r(buf, ",", &saveptr);
while (token) {
    // Process
    token = strtok_r(NULL, ",", &saveptr);
}

// ✅ Shared resource protection
pthread_mutex_lock(&self->lock);
// Access shared resource
pthread_mutex_unlock(&self->lock);
```

---

## Rule 7. System Resources

1. File descriptors (`fd`) must always be initialized to `-1`. (`0` means `stdin`.)
2. A validity check (`fd >= 0`) must be performed before calling `close()`.
3. After calling `close()`, `fd` must be reset to `-1` to prevent double-close.

```c
// ✅ Correct initialization
self->fd = -1;

// ✅ Correct close pattern
if (self->fd >= 0) {
    close(self->fd);
    self->fd = -1;  // Prevent double-close
}
```

---

## Rule 8. Comparison Operations

1. `equals` and `compareTo` must always be consistent with each other.
2. If `equals` returns `true`, `compareTo` must return `0`.
3. When comparing `float` or `double` values, epsilon must always be used. (`1e-9` recommended)

```c
// ✅ Double equals (using epsilon)
static bool Double_equals(Double* self, Double* other) {
    if (!self || !other) return false;
    double diff = self->value - other->value;
    if (diff < 0.0) diff = -diff;
    return diff < 1e-9;
}

// ✅ Double compareTo (same epsilon as equals)
static int Double_compareTo(Double* self, Double* other) {
    if (!self || !other) return 0;
    double diff = self->value - other->value;
    if (diff < 0.0) diff = -diff;
    if (diff < 1e-9) return 0;  // Guarantees: equals=true → compareTo=0
    if (self->value < other->value) return -1;
    return 1;
}
```

---

## Rule 9. Prohibited Patterns

If any of the following patterns are found, code review must be halted immediately and the code discarded.

| Prohibited Pattern | Reason |
|-------------------|--------|
| Use of `free(obj)` | Bypasses ARC system |
| Struct without `Object base` | Violates OOP pattern |
| Missing `Object_Init` | ARC registration failure |
| Missing `finalize` | Memory leak guaranteed |
| Missing `.size` | ARC allocation error |
| Use of `strtok` | Thread-unsafe |
| Shared resource access without Mutex | Race condition |
| `fd` uninitialized (`= 0`) | stdin malfunction |
| `equals` / `compareTo` inconsistency | Sort malfunction |
| Missing epsilon for `float` / `double` | Comparison error |
| Plain C-style implementation | Violates libcore pattern |
| Circular reference structure | ARC cannot release |

---

## Rule 10. Required Outcome

All libcore code must satisfy the following conditions.

| Condition | Verification |
|-----------|-------------|
| Compiles successfully | `gcc -Wall -Werror` passes |
| No memory leaks | Valgrind `0 bytes in 0 blocks` |
| No memory overflow | ASan `ERROR SUMMARY: 0 errors` |
| No race conditions | TSan `0 warnings` |
| ARC 100% compliant | RELEASE / RETAIN correct |
| Thread-safe | Mutex protection complete |
| Fully libcore-compatible | Follows existing patterns |

```bash
# Triple verification (Iron Fortress standard)

# 1. Valgrind
valgrind --leak-check=full ./binary
# → "0 bytes in 0 blocks" required

# 2. ASan
gcc -fsanitize=address -o binary ./src && ./binary
# → "ERROR SUMMARY: 0 errors" required

# 3. TSan
gcc -fsanitize=thread -o binary ./src && ./binary
# → "WARNING: 0 warnings" required
```

---

## Rule 11. Circular Reference Prevention

The most critical vulnerability of ARC is circular reference.  
If an `A → B → A` cycle forms, the reference count never reaches 0 and memory is never released.

1. When mutual references exist, ownership direction must always be explicitly stated. (Unidirectional ownership only)
2. If a back-reference is needed, a weak reference (pointer only, no RETAIN) must be used.
3. Any structure with potential circular references must be eliminated at the design stage.

```c
// ❌ Circular reference — memory leak guaranteed
struct Parent {
    Object  base;
    Child*  child;   // [OWNED] Parent → Child RETAIN
};
struct Child {
    Object  base;
    Parent* parent;  // [OWNED] Child → Parent RETAIN → circular!
};
// RELEASE(parent) will never reach ref count 0
// because child still holds RETAIN on parent → leak

// ✅ Correct pattern — weak reference
struct Parent {
    Object  base;
    Child*  child;   // [OWNED] → RETAIN
};
struct Child {
    Object  base;
    Parent* parent;  // [BORROWED] → pointer only, no RETAIN (weak reference)
};
// RELEASE(parent) releases correctly
```

---

## Final Declaration

```
All code must be written in strict compliance with these rules.
Plain C-style implementations are not permitted.
The libcore system must always be maintained.
No violations are tolerated.
```

---

*Toos IT Holdings | libcore v1.1 Iron Fortress | 2026*
