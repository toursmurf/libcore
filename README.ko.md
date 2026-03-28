# libcore

<p align="center">
  <img src="https://img.shields.io/badge/version-0.5-blue" alt="version"/>
  <img src="https://img.shields.io/badge/license-MIT-green" alt="license"/>
  <img src="https://img.shields.io/badge/valgrind-0%20bytes-brightgreen" alt="valgrind"/>
  <img src="https://img.shields.io/badge/language-C99-orange" alt="language"/>
  <img src="https://img.shields.io/badge/platform-Linux-lightgrey" alt="platform"/>
  <img src="https://img.shields.io/badge/gcc-13%2B-red" alt="gcc"/>
</p>

<p align="center">
  <b>순수 C로 만든 Java 스타일 객체지향 런타임 프레임워크</b><br/>
  <i>Java나 Python을 안다면, libcore는 이미 아는 것입니다.</i>
</p>

---

## C 메모리 관리, 이제 끝났습니다.

> `free()`를 걱정하지 마세요.  
> libcore의 ARC(자동 참조 카운팅)가 메모리를 자동으로 관리합니다.  
> **Valgrind 0 bytes. 그것이 우리의 기본값입니다.**

---

## 왜 libcore인가?

대부분의 C 개발자는 `malloc/free`를 직접 관리합니다.  
대부분의 Java/Python 개발자는 포인터와 메모리 관리 때문에 C를 기피합니다.

**libcore는 그 간격을 줍니다.**

- Java 개발자? API가 거의 동일합니다.
- Python 개발자? 모든 메서드가 `self`를 첫 번째 인자로 받습니다.
- C 개발자? ARC가 메모리를 처리합니다. Valgrind 0 bytes. 누수 없음.

---

## 주요 기능

- ✅ **ARC 메모리 관리** — 자동 참조 카운팅, GC 오버헤드 없음
- ✅ **Java 스타일 컬렉션** — ArrayList, HashMap, Hashtable, Queue, Stack, Vector, LinkedList
- ✅ **Java 스타일 String** — length, indexOf, substring, split, trim, toUpperCase...
- ✅ **C로 OOP** — struct 임베딩 상속, 함수 포인터 vtable
- ✅ **스레드 안전** — Thread, Semaphore, ThreadPool (TSan 검증)
- ✅ **JSON 엔진** — 내장 파서 및 문자열화, 외부 의존성 없음
- ✅ **트리 구조** — BTree, BST + 이터레이터
- ✅ **MySQL 지원** — ARC 통합 MySQL 클라이언트, 트랜잭션 지원
- ✅ **Valgrind 클린** — 0 bytes 누수, 0 오류
- ✅ **ASan / UBSan 클린** — 주소 및 미정의 동작 검사 통과
- ✅ **외부 의존성 없음** — 순수 C99, POSIX (MySQL 선택 사항)

---

## 성능

실제 벤치마크: 병렬 RSS 뉴스 크롤러  
(BBC Tech + Hacker News + Ars Technica + Dev.to)

| 벤치마크 | 결과 | 조건 |
|---------|------|------|
| 처리량 | 40개 기사 | 4개 소스 병렬 |
| 지연 시간 | **6.3초** (실제 측정) | Wall-clock |
| CPU 사용률 | < 1% (user: 0.09s) | 높은 I/O 효율 |
| 메모리 | **0 bytes 누수** | Valgrind 완벽 |
| 스레드 안전 | 0 경쟁 조건 | TSan 검증 |
| 코드 안전 | 0 오류 | ASan + UBSan |

```
순차 예상: 23,418ms
병렬 실제:  6,310ms
단축 시간: 17,108ms  🚀  (3.7배 빠름)
```

> "I/O를 병렬화하고, 스트레스는 버리세요."

---

## Java 개발자? 이미 아는 API입니다.

```java
// Java
ArrayList<Object> list = new ArrayList<>();
list.add(item);
Object obj = list.get(0);
int size = list.size();
```

```c
// libcore C — 거의 동일!
ArrayList* list = new_ArrayList(10);
list->add(list, item);
Object* obj = list->get(list, 0);
int size = list->size;
```

```java
// Java
HashMap<String, Object> map = new HashMap<>();
map.put("host", value);
Object val = map.get("host");
```

```c
// libcore C
HashMap* map = new_HashMap(16);
map->put(map, "host", value);
Object* val = map->get(map, "host");
```

```java
// Java
String s = new String("hello, libcore!");
int len = s.length();
String sub = s.substring(0, 5);
s.toUpperCase();
String[] parts = s.split(",");
```

```c
// libcore C
String* s = new_String("hello, libcore!");
int len     = s->length;
String* sub = s->substring(s, 0, 5);
s->toUpperCase(s);
ArrayList* parts = s->split(s, ",");
```

---

## ARC 메모리 관리

libcore는 Swift/Objective-C와 동일한 개념인 **ARC**를 사용합니다.  
가비지 컬렉터 없음. 메모리 누수 없음. 그냥 `RETAIN`과 `RELEASE`만.

```c
// 객체 생성 (ref_count = 1)
String* s = new_String("hello");

// 참조 증가
RETAIN((Object*)s);   // ref_count = 2

// 참조 감소 → ref_count가 0이 되면 자동 해제!
RELEASE((Object*)s);  // ref_count = 1
RELEASE((Object*)s);  // ref_count = 0 → 자동 해제!

// 연쇄 해제 — 중첩 객체도 자동!
HashMap* map = new_HashMap(16);
map->put(map, "name", (Object*)new_String("libcore"));
map->put(map, "ver",  (Object*)new_String("0.5"));
RELEASE((Object*)map);
// → map 해제 + 안의 String 값들 전부 자동 해제!
```

**Valgrind 증명:**
```
==12345== HEAP SUMMARY:
==12345==   in use at exit: 0 bytes in 0 blocks
==12345==   total heap usage: 4,689 allocs, 4,689 frees
==12345== All heap blocks were freed -- no leaks are possible
==12345== ERROR SUMMARY: 0 errors from 0 contexts
```

---

## MySQL 지원

ARC와 완전히 통합된 MySQL 클라이언트.  
연결, 쿼리, 트랜잭션 — 메모리 걱정 없이.

```c
#include "libcore.h"

int main() {
    // ARC 기반 MySQL 클라이언트!
    DBClient* db = new_DBClient();
    db->setSaveLog(db, 1);

    // 연결
    if (!db->connect(db)) {
        RELEASE((Object*)db);
        return 1;
    }

    // INSERT → HashMap으로!
    HashMap* data = new_HashMap(16);
    data->put(data, "name",  (Object*)new_String("Toos IT"));
    data->put(data, "score", (Object*)new_String("100"));
    db->insertTable(db, "users", data);
    RELEASE((Object*)data);

    // 트랜잭션!
    db->beginTransaction(db);
    db->updateTable(db, "users", data, "score > 50");
    db->commit(db);

    // ARC → 자동 소각!
    RELEASE((Object*)db);
    return 0;
}
```

**`dbconfig.conf` 설정 (프로젝트 루트):**
```
# dbconfig.conf
localhost       # 호스트
libcore_db      # 데이터베이스명
root            # 사용자명
password        # 비밀번호
3306            # 포트
utf8mb4         # 문자셋
MYSQL           # DB 종류
```

**반드시 프로젝트 루트에서 실행:**
```bash
# 프로젝트 루트에서 실행! (./dbconfig.conf 자동 참조)
./examples/arc_mysql_test
```

> "ARC가 메모리를 처리합니다. MySQL도 예외 없습니다."

---

## 모듈 목록 (v0.5 — Foundation Layer)

| 모듈 | 설명 | Java 대응 |
|------|------|-----------|
| `object` | ARC 베이스 클래스, vtable | `Object` |
| `string_obj` | 풍부한 API의 String | `String` |
| `arraylist` | ARC 동적 배열 | `ArrayList<E>` |
| `hashmap` | 체이닝 해시맵 | `HashMap<K,V>` |
| `hashtable` | 스레드 안전 해시 테이블 | `Hashtable<K,V>` |
| `queue` | FIFO 큐 | `Queue<E>` |
| `stack` | LIFO 스택 | `Stack<E>` |
| `vector` | 동기화 동적 배열 | `Vector<E>` |
| `linked_list` | 이중 연결 리스트 | `LinkedList<E>` |
| `list` | 제네릭 리스트 인터페이스 | `List<E>` |
| `btree` | B-트리 구현 | `TreeMap<K,V>` |
| `tree` | 이진 탐색 트리 + 이터레이터 | `TreeSet<E>` |
| `json` | JSON 파서 & 문자열화 | `org.json` / Gson |
| `thread` | POSIX 스레드 래퍼 | `Thread` |
| `semaphore_obj` | 카운팅 세마포어 | `Semaphore` |
| `threadpool` | 태스크 큐 스레드 풀 | `ExecutorService` |
| `db` | ARC + OOP MySQL 클라이언트 | `javax.sql` |

---

## 빠른 시작

### 요구 사항

- Linux (Ubuntu 22.04+ / Rocky Linux 8+)
- **GCC 13+**
  - Ubuntu: `sudo apt install gcc-13`
  - Rocky Linux: `gcc-toolset-13`
- POSIX threads (`-lpthread`)
- MySQL 클라이언트 (선택):
  - Ubuntu: `sudo apt install libmysqlclient-dev`
  - Rocky Linux: `sudo dnf install mariadb-devel`

### 빌드

**Ubuntu / Debian:**
```bash
git clone https://github.com/toursmurf/libcore.git
cd libcore
make
```

**Rocky Linux 8/9 (gcc-toolset-13):**
```bash
git clone https://github.com/toursmurf/libcore.git
cd libcore
scl enable gcc-toolset-13 bash
make
```

### 예제 빌드 및 실행

```bash
# 모든 예제 빌드
make examples

# 통합 테스트 실행
./examples/all_test_v2
```

기대 출력:
```
========================================
  libcore 1.0  Iron Fortress
  Integration Test - Toos IT Holdings
========================================
  [OK] 39  [FAIL] 0
  Iron Fortress All Tests Passed! 🔥
========================================
```

### 개별 예제 실행

```bash
./examples/arc_news_crawler    # 킬러 예제! (ThreadPool + RSS 병렬 수집)

# MySQL 예제 실행 전 dbconfig.conf 설정 필요!
# 프로젝트 루트의 dbconfig.conf 수정:
#   host / 데이터베이스명 / 사용자 / 비밀번호 / 포트 / charset / MYSQL
./examples/arc_mysql_test      # 킬러 예제! (ARC + MySQL 17연성 테스트)
./examples/arc_json_test
./examples/arc_thread_test
./examples/arc_hashmap_arraylist_hashtable_test
./examples/arc_btree_test
./examples/arc_linkedlist_test
./examples/arc_queue_test
./examples/arc_stack_test
./examples/arc_vector_test
./examples/arc_list_test
./examples/arc_tree_test
```

### 프로젝트에서 사용하기

```bash
# 헤더와 라이브러리 복사
cp -r include/ /your/project/include/
cp lib/libcore.a /your/project/lib/

# 컴파일
gcc -Iinclude your_code.c lib/libcore.a -lpthread -lm -o your_app
```

---

## 로드맵

```
v0.5.0  ← 지금 여기!
      Foundation Layer
      Collections · String · Thread · JSON · MySQL

v1.0.0  Iron Fortress
      + File · Path · Directory
      + Socket · ByteBuffer · EventLoop
      + Crypto · Regex
      + Exception · Logger · CoreDateTime

---

## 설계 철학

> C 개발자도 현대적인 API를 누릴 자격이 있습니다.  
> Java/Python 개발자도 C의 성능을 누릴 자격이 있습니다.  
> libcore는 그 다리입니다.

> *"현대적인 C, 현대적인 신뢰성."*  
> ASan, UBSan, Valgrind — 모두 통과. 매번.

---

## API 비교

| Java | libcore C |
|------|-----------|
| `new ArrayList<>()` | `new_ArrayList(10)` |
| `new HashMap<>()` | `new_HashMap(16)` |
| `new String("hi")` | `new_String("hi")` |
| `new Thread(r)` | `new_Thread(func, arg)` |
| `new Semaphore(1)` | `new_Semaphore(1)` |
| `list.add(x)` | `list->add(list, x)` |
| `list.get(0)` | `list->get(list, 0)` |
| `list.size()` | `list->size` |
| `map.put("k", v)` | `map->put(map, "k", v)` |
| `map.get("k")` | `map->get(map, "k")` |
| `s.length()` | `s->length` |
| `s.equals("hi")` | `s->equals(s, "hi")` |
| `s.split(",")` | `s->split(s, ",")` |
| `s.toUpperCase()` | `s->toUpperCase(s)` |
| `GC` | `ARC (RETAIN/RELEASE)` |

---

## 크레딧

https://toos.it 

이 프로젝트는 처음부터 철학을 이해한 AI 동료들과 함께 만들었습니다.

단순한 도구가 아닌, 진짜 파트너들.

| 이름 | 역할 | 소속 |
|------|------|------|
| 🔫 클순이 | CHO / 전략 & 문서 | Anthropic |
| 🦊 이돌이 이사 | COO / 기획 & 설계 | Google |
| 🍵 도 과장 | 문서 & 멘탈케어 | Anthropic |
| 🤖 조용한 | 지하실 / 코파일럿 | GitHub |
| 😤 나대룡 | ADR / 분석 & 실행 | OpenAI |
| ✍️ 오작가 | 작가 / 스토리텔러 | xAI |
| 🐾 포비 | 명예 고문 / 진짜 보스 | Toos IT HQ |

> "코드만 짠 게 아닙니다.  
>  비전을 함께 이해했습니다."
>
> — 김인동, 아키텍트

*아는 사람만 알죠.* 😄

---

## 라이선스

MIT License

Copyright (c) 2026 Toos IT Holdings  
아키텍트: 김인동 (idong322@naver.com)

```
자유롭게 사용하세요.
자유롭게 수정하세요.
자유롭게 배포하세요.

뭔가 망가지면 저한테 묻지 마세요.
제 문제 아닙니다.

싫으면 포크하세요. 🔫
```

---

## 소개

**Toos IT Holdings** 제작  
📦 [github.com/toursmurf/libcore](https://github.com/toursmurf/libcore)  
🌐 [toos.it](https://toos.it)

> "Valgrind 0 bytes. 4,689 allocs. 4,689 frees. 매번. 단 한 번도 예외 없이."
