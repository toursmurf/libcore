# libcore v1.0 코딩 가이드

**Toos IT Holdings | Iron Fortress v1.0 | 한글판**

---

## 1. 시작하기 전에

```
이 문서는 libcore 를 처음 사용하는 개발자를 위한 가이드입니다.
ARC 메모리 관리, Object 시스템, 핵심 모듈 사용법을 다룹니다.

요구사항:
→ Linux 64-bit (Ubuntu / Rocky / Debian)
→ GCC 9+ / Clang 10+
→ GNU Make
→ C99 기본 지식
```

---

## 2. 빌드

```bash
# 기본 빌드
git clone https://github.com/toursmurf/libcore.git
cd libcore
make examples

# 첫 예제 실행
./examples/arc_echo_server

# 다른 터미널에서 테스트
nc localhost 8080
hello
# → hello (에코 응답)

# MySQL 포함 빌드 (선택)
make WITH_MYSQL=1 \
  MYSQL_INC=$(mysql_config --variable=pkgincludedir) \
  MYSQL_LIB=$(mysql_config --variable=pkglibdir)
```

---

## 3. 소스 구조

```
libcore/
├── include/        헤더 파일 (.h) — 먼저 읽을 파일
│   ├── object.h    Object / Class / ARC 매크로
│   ├── arraylist.h ArrayList / Iterator
│   ├── hashmap.h   HashMap
│   ├── tcp_socket.h TcpSocket
│   ├── event_loop.h EventLoop
│   └── ...         (44개 모듈 전체)
│
├── src/            구현 파일 (.c)
├── examples/       예제 소스 (34개) ← 처음엔 여기서 시작!!
├── docs/           문서
└── Makefile
```

> ⭐ **처음엔 `examples/` 폴더부터 시작하세요**

---

## 4. 설계 철학

libcore 는 4가지 핵심 원칙을 따릅니다.

**① Object 최상위 상속**
모든 구조체의 첫 번째 멤버는 반드시 `Object base` 입니다.

```c
struct ArrayList {
    Object base;    // 반드시 첫 번째!! ARC 심장
    int    size;
    int    capacity;
    // ...
};
```

**② new_ClassName() 생성자**
모든 객체는 `new_ClassName()` 으로 생성합니다. 생성 시 `ref_count = 1` 자동 설정.

```c
ArrayList* list = new_ArrayList(10);  // ref_count = 1
```

**③ finalize 소멸자**
`ref_count` 가 0 이 되면 `finalize()` 가 자동 호출되어 내부 자원을 해제합니다.

**④ Valgrind 0 bytes**
모든 모듈은 Valgrind 메모리 누수 0 bytes 를 필수 통과해야 합니다. 타협 없음.

---

## 5. ARC 메모리 관리

### 5-1. 3가지 매크로

```c
/* RETAIN — ref_count++ */
RETAIN((Object*)item);

/* RELEASE — ref_count-- / 0이면 finalize + free */
RELEASE((Object*)item);

/* RELEASE_NULL — RELEASE 후 포인터 NULL 초기화 */
RELEASE_NULL((Object**)&ptr);
```

### 5-2. [OWNED] vs [BORROWED]

| 표기 | 의미 | RELEASE |
|---|---|---|
| `[OWNED]` | 소유권이 호출자에게 넘어옴 | 필수 |
| `[BORROWED]` | 소유권이 오지 않음 | 절대 금지 |

```c
/* [OWNED] — RELEASE 필수 */
ArrayList* list = new_ArrayList(10);   // [OWNED]
String*    str  = new_String("hello"); // [OWNED]
RELEASE((Object*)str);   // 사용 후 반드시!!
RELEASE((Object*)list);

/* [BORROWED] — RELEASE 금지 */
String* item = (String*)list->get(list, 0);  // [BORROWED]
printf("%s\n", item->c_str(item));           // 사용 가능
// RELEASE(item) ← 절대 금지!! 크래시!!
```

### 5-3. 기본 패턴

```c
/* ① 생성 → 사용 → 해제 */
ArrayList* list = new_ArrayList(10);   // ref_count = 1
String*    str  = new_String("apple"); // ref_count = 1

list->add(list, (Object*)str);         // 내부 RETAIN → ref_count = 2
RELEASE((Object*)str);                 // ref_count = 1, list 가 소유

/* ② list RELEASE 시 내부 str 도 자동 소각 */
RELEASE((Object*)list);  // list ref=0 → str RELEASE → 전부 소각

/* ③ [OWNED] 반환 함수 */
String* make_greeting(const char* name) {
    String* s = new_String("Hello, ");
    s->append(s, name);
    return s;  // [OWNED] — 호출자가 RELEASE 책임
}

String* greeting = make_greeting("World");
printf("%s\n", greeting->c_str(greeting));
RELEASE((Object*)greeting);  // 필수!!
```

---

## 6. Object 시스템

```c
/* 새 클래스 만들기 */
typedef struct MyClass MyClass;
struct MyClass {
    Object base;      // 반드시 첫 번째!!
    int    value;
    void (*print)(MyClass* self);
};

/* 소멸자 */
static void myclass_finalize(Object* obj) {
    MyClass* self = (MyClass*)obj;
    // 내부 자원 해제
}

/* 클래스 메타정보 */
static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),      // 반드시 명시!!
    .finalize = myclass_finalize
};

/* 생성자 */
MyClass* new_MyClass(int value) {
    MyClass* self = (MyClass*)calloc(1, sizeof(MyClass));
    if (!self) return NULL;
    Object_Init((Object*)self, &_myClass);  // ARC 초기화
    self->value = value;
    self->print = myclass_print;
    return self;
}
```

---

## 7. 핵심 모듈

### 7-1. String

```c
/* 생성 */
String* s = new_String("hello");

/* 주요 API */
int         len   = s->length_f(s);
const char* raw   = s->c_str(s);          // [BORROWED]
String*     upper = s->copy(s);           // [OWNED]
upper->toUpperCase(upper);                // 인플레이스
String*     cat   = s->concat(s, " world"); // [OWNED]
String*     sub   = s->substring(s, 0, 3);  // [OWNED]
bool        eq    = s->equals(s, (Object*)other);

/* 변환 */
int    n = s->toInt(s);
double d = s->toDouble(s);

/* 정규표현식 */
bool match    = s->matches(s, "^hello.*");
ArrayList* parts = s->split(s, ",");  // [OWNED]

/* 해제 */
RELEASE((Object*)upper);
RELEASE((Object*)cat);
RELEASE((Object*)sub);
RELEASE((Object*)parts);
RELEASE((Object*)s);
```

### 7-2. ArrayList

```c
ArrayList* list = new_ArrayList(10);

/* 추가 — 내부 RETAIN 자동 */
String* item = new_String("apple");
list->add(list, (Object*)item);
RELEASE((Object*)item);  // list 가 소유

/* 조회 — [BORROWED] */
String* got = (String*)list->get(list, 0);
printf("%s\n", got->c_str(got));  // RELEASE 금지!!

/* 크기 */
int sz    = list->getSize(list);
bool empty = list->isEmpty(list);

/* 순회 */
list->forEach(list, my_print_fn);

/* Iterator */
ArrayListIterator* it = list->iterator(list);  // [OWNED]
while (it->hasNext(it)) {
    String* s = (String*)it->next(it);  // [BORROWED]
    printf("%s\n", s->c_str(s));
}
RELEASE((Object*)it);

/* 해제 — 내부 항목 전부 RELEASE 자동 */
RELEASE((Object*)list);
```

### 7-3. HashMap

```c
HashMap* map = new_HashMap(16);

/* 삽입 — 내부 RETAIN 자동 */
String* val = new_String("value1");
map->put(map, "key1", (Object*)val);
RELEASE((Object*)val);

/* 조회 — [BORROWED] */
String* found = (String*)map->get(map, "key1");
if (found) printf("%s\n", found->c_str(found));

/* 키 확인 / 삭제 */
bool exists = map->hasKey(map, "key1");
map->remove(map, "key1");  // 내부 RELEASE 자동

/* 키 목록 — [OWNED] */
ArrayList* keys = map->keys(map);  // RELEASE 필수
RELEASE((Object*)keys);

RELEASE((Object*)map);
```

### 7-4. Thread / ThreadPool

```c
/* Thread */
void* my_task(void* arg) {
    printf("작업 실행\n");
    return NULL;
}

CoreThread* t = new_CoreThread(my_task, NULL);
t->start(t);
t->join(t);
RELEASE((Object*)t);

/* ThreadPool */
ThreadPool* pool = new_ThreadPool(4);  // 워커 4개
pool->submit(pool, my_task, NULL);
pool->submit(pool, my_task, NULL);
pool->shutdown(pool);
RELEASE((Object*)pool);

/* Semaphore */
Semaphore* sem = new_Semaphore(1);
sem->wait(sem);   // P 연산
// ... 임계 구역 ...
sem->post(sem);   // V 연산
RELEASE((Object*)sem);
```

### 7-5. Socket / EventLoop

```c
/* 서버 생성 */
TcpSocket*  tcp  = new_TcpServer("0.0.0.0", 8080);
UdpSocket*  udp  = new_UdpServer("0.0.0.0", 9000);
UnixSocket* unix = new_UnixServer("/tmp/arc.sock");

/* EventLoop */
EventLoop* loop = new_EventLoop(1024);

/* 콜백 등록 */
tcp->base.on_readable = on_accept;
udp->base.on_readable = on_udp;

/* 소켓 등록 */
loop->addSocket(loop, (Socket*)tcp, EV_READ);
loop->addSocket(loop, (Socket*)udp, EV_READ);

/* 실행 — 블로킹 */
loop->run(loop);

/* 소켓 제거 */
loop->delSocket(loop, (Socket*)tcp);

/* 정지 */
loop->stop(loop);

RELEASE((Object*)tcp);
RELEASE((Object*)udp);
RELEASE((Object*)loop);
```

### 7-6. Logger

```c
Logger* logger = new_Logger(LOG_LEVEL_INFO);

/* 파일 출력 설정 (선택) */
logger->setLogFile(logger, "/var/log/myapp.log");

/* 로그 출력 */
LOG_DEBUG(logger, "디버그: %s", "메시지");
LOG_INFO (logger, "정보: %d", 42);
LOG_WARN (logger, "경고: %s", "주의");
LOG_ERROR(logger, "오류: %s", "실패");

RELEASE((Object*)logger);
```

---

## 8. TCP 에코 서버 — 전체 예제

```c
#include "libcore.h"

/* ③ 클라이언트 데이터 수신 → 에코 */
static void on_client(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    char buf[1024];
    ssize_t n = self->recv(self, buf, sizeof(buf), NULL, NULL);
    if (n > 0) {
        self->send(self, buf, n, NULL, 0);
    } else {
        loop->delSocket(loop, self);
        RELEASE((Object*)self);
    }
}

/* ② 연결 수락 */
static void on_accept(Socket* self, void* loop_ptr) {
    EventLoop* loop   = (EventLoop*)loop_ptr;
    TcpSocket* client = ((TcpSocket*)self)->accept(
                            (TcpSocket*)self, NULL, NULL);
    if (client) {
        client->base.on_readable = on_client;
        loop->addSocket(loop, (Socket*)client, EV_READ);
        RELEASE((Object*)client);  // loop 에 넘김
    }
}

/* ① main */
int main(void) {
    EventLoop* loop   = new_EventLoop(1024);
    TcpSocket* server = new_TcpServer("0.0.0.0", 8080);
    server->base.on_readable = on_accept;
    loop->addSocket(loop, (Socket*)server, EV_READ);
    loop->run(loop);
    RELEASE((Object*)server);
    RELEASE((Object*)loop);
    return 0;
}
```

---

## 9. 멀티 프로토콜 서버

```c
/* TCP + UDP + Unix 동시 처리 — 단일 EventLoop */

int main(void) {
    EventLoop*  loop = new_EventLoop(1024);
    TcpSocket*  tcp  = new_TcpServer("0.0.0.0", 8001);
    UdpSocket*  udp  = new_UdpServer("0.0.0.0", 8002);
    UnixSocket* unix = new_UnixServer("/tmp/arc.sock");

    tcp->base.on_readable  = on_accept;
    udp->base.on_readable  = on_udp;      // UDP 는 바로 처리
    unix->base.on_readable = on_accept;

    loop->addSocket(loop, (Socket*)tcp,  EV_READ);
    loop->addSocket(loop, (Socket*)udp,  EV_READ);
    loop->addSocket(loop, (Socket*)unix, EV_READ);

    loop->run(loop);  // 단일 스레드로 3개 프로토콜 동시 처리!!
    /* ... cleanup ... */
    return 0;
}
```

---

## 10. 자주 하는 실수

### ❌ 실수 1: [BORROWED] 반환값 RELEASE

```c
/* 잘못된 코드 */
String* s = list->get(list, 0);
RELEASE((Object*)s);  // ❌ 크래시!!

/* 올바른 코드 */
String* s = list->get(list, 0);
printf("%s\n", s->c_str(s));  // 사용만. RELEASE 금지!!
```

### ❌ 실수 2: new 후 RELEASE 안 함

```c
/* 잘못된 코드 */
String* s = new_String("hi");
// RELEASE 없음 → 메모리 누수!!

/* 올바른 코드 */
String* s = new_String("hi");
// 사용 후
RELEASE((Object*)s);  // ✅ 필수!!
```

### ❌ 실수 3: accept() 후 RELEASE 안 함

```c
/* 잘못된 코드 */
TcpSocket* c = server->accept(...);
loop->addSocket(loop, c, EV_READ);
// RELEASE 없음 → 누수!!

/* 올바른 코드 */
TcpSocket* c = server->accept(...);
loop->addSocket(loop, c, EV_READ);
RELEASE((Object*)c);  // ✅ loop 가 소유
```

### ❌ 실수 4: Object base 위치 잘못됨

```c
/* 잘못된 코드 */
struct Bad {
    int    field;
    Object base;  // ❌ 오프셋 오류!!
};

/* 올바른 코드 */
struct Good {
    Object base;  // ✅ 반드시 첫 번째!!
    int    field;
};
```

### ❌ 실수 5: finalize 누락

```c
/* 잘못된 코드 */
static const Class _myClass = {
    .name = "MyClass",
    .size = sizeof(MyClass),
    // finalize 없음 → 내부 자원 누수!!
};

/* 올바른 코드 */
static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),
    .finalize = myclass_finalize  // ✅ 반드시!!
};
```

---

## 11. Coding Contract (필수 규칙)

```
1. Object base 는 구조체 첫 번째 멤버
2. .size = sizeof(구조체) 반드시 명시
3. finalize 반드시 구현
4. free() 직접 호출 금지 — RELEASE 만 사용
5. new_ClassName() 형식 생성자
6. NULL 체크 필수
7. strtok 금지 — strtok_r 사용
8. 공유 자원 Mutex 보호 필수
9. fd 초기값 = -1
10. float/double 비교 시 epsilon 사용
11. Thread-Safety 필수
```

---

## 12. 핵심 3가지

```
① Object base 는 반드시 구조체 첫 번째 멤버

② [OWNED] = RELEASE 필수
   [BORROWED] = RELEASE 절대 금지

③ EventLoop 가 I/O 처리
   ThreadPool 이 계산 처리
```

---

## 13. 문서 링크

| 문서                                                         | 내용 |
|------------------------------------------------------------|---|
| [libcore_api_ko.md](libcore_api_ko.md)                     | API 레퍼런스 전체 |
| [libcore_v1_class_diagram.md](libcore_v1_class_diagram.md) | 클래스 다이어그램 |
| [examples.ko.md](examples.ko.md)                        | 예제 가이드 |
| [mysql_setup.md](mysql_setup.md)                           | MySQL 연동 |

---

**libcore v1.0 Iron Fortress | Toos IT Holdings | MIT License**
