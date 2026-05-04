# libcore v1.0 "Iron Fortress" — API 레퍼런스

**Toos IT Holdings** | 작성: 클순이 부장 | 기준: 2026-04-21 | 총 43개 모듈 | 350+ API

---

> **`[OWNED]`** = 호출자가 RELEASE 책임
> **`[BORROWED]`** = RELEASE 금지
> `free()` 명시 항목은 일반 `char*` 반환

---

## 전체 상속 계층도

```
Object (최상위 부모)
├── String
├── ArrayList
│   └── ArrayListIterator
├── HashMap
├── Hashtable
│   └── HashtableIterator 
├── Queue
├── List
├── LinkedList           
├── BTree
├── Stack     (ArrayList 위임)
├── Vector    (수동 lock/unlock)
├── Tree                 
│── TreeIterator TreeNode (내부)
├── JSONNode
│── JsonValue
├── Thread
├── ThreadPool
├── Semaphore
├── Logger
│   └── AsyncLogger
├── Exception
├── Path
├── File
├── FileWatcher
├── MappedFile
├── Directory
├── ByteBuffer
├── RingBuffer
├── Socket               (추상 기반)
│   ├── TcpSocket
│   ├── UdpSocket
│   └── UnixSocket
├── EventLoop
├── Timer
├── Scheduler            (ThreadPool + EventLoop 조합)
├── Context
├── Config
├── ServiceRegistry
├── AppContext            (Config + Context + ServiceRegistry 조합)
└── DBClient

```

---

## 📦 object — `object.h / object.c`

```
Object  ← 최상위 부모. 상속 없음.
```

ARC 기반 최상위 부모 클래스. 모든 libcore 객체의 기반.

> 매크로: `RETAIN(obj)` · `RELEASE(obj)` · `RELEASE_NULL(&ptr)`

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `void` | `Object_Init(Object* obj, const Class* type)` | 객체 초기화, ref_count=1 세팅 |
| `bool` | `instanceOf(Object* obj, const Class* type)` | 런타임 타입 체크 (Java instanceof) |
| `void` | `toString(Object* obj, char* buf, size_t len)` | VTable 기반 문자열 변환 |
| `bool` | `equals(Object* obj, Object* other)` | VTable 기반 동등 비교 |
| `int` | `hashCode(Object* obj)` | VTable 기반 해시코드 반환 |
| `void` | `destroy(Object* obj)` | [내부용] finalize 호출 후 free |
| `char*` | `safe_strdup(const char* src, size_t max_len)` | 안전한 문자열 복사 |

---

## 📦 string_obj — `string_obj.h / string_obj.c`

```
Object
└── String
```

Java 스타일 String. Object 상속, ARC 완전 적용.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] String*` | `new_String(const char* init_str)` | String 생성자 |
| `char*` | `string_join(const char* delim, const char** arr, int count)` | 문자열 배열 결합 — 사용 후 `free()` |
| `int` | `self->length_f(String* self)` | 문자열 길이 반환 |
| `char` | `self->charAt(String* self, int index)` | index 위치 문자 반환 |
| `bool` | `self->equals(String* self, const char* another)` | 문자열 동등 비교 |
| `int` | `self->indexOf(String* self, const char* str)` | 부분 문자열 위치 반환 |
| `[OWNED] String*` | `self->substring(String* self, int begin, int end)` | 부분 문자열 추출 |
| `[OWNED] String*` | `self->concat(String* self, const char* str)` | 문자열 결합 |
| `[OWNED] String*` | `self->trim(String* self)` | 앞뒤 공백 제거 |
| `void` | `self->append(String* self, const char* str)` | 문자열 추가 (인플레이스) |
| `void` | `self->clear(String* self)` | 문자열 초기화 |
| `[OWNED] String*` | `self->copy(String* self)` | 복사본 생성 |
| `bool` | `self->isEmpty(String* self)` | 빈 문자열 여부 |
| `void` | `self->toUpperCase(String* self)` | 대문자 변환 (인플레이스) |
| `void` | `self->toLowerCase(String* self)` | 소문자 변환 (인플레이스) |
| `int` | `self->toInt(Object* obj)` | 정수 변환 |
| `long long` | `self->toLong(Object* obj)` | long 변환 |
| `double` | `self->toDouble(Object* obj)` | double 변환 |
| `[BORROWED] const char*` | `self->c_str(String* self)` | C 문자열 포인터 반환 |
| `[OWNED] String*` | `self->reverse(String* self)` | 문자열 역순 반환 |
| `[OWNED] String*` | `self->replace(String* self, const char* target, const char* rep)` | 문자열 치환 |
| `[OWNED] ArrayList*` | `self->split(String* self, const char* delimiter)` | 구분자로 분할 |
| `bool` | `self->matches(String* self, const char* pattern)` | 정규표현식 매칭 |
| `bool` | `self->eregi(String* self, const char* pattern)` | 대소문자 무시 정규표현식 매칭 |

---

## 📦 arraylist — `arraylist.h / arraylist.c`

```
Object
└── ArrayList
    └── ArrayListIterator
```

동적 배열. Object 상속, Thread-Safe, Iterator 지원.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] ArrayList*` | `new_ArrayList(int initial_capacity)` | 동적 배열 생성 |
| `void` | `self->add(ArrayList* self, Object* item)` | 항목 추가 (RETAIN 포함) |
| `[BORROWED] Object*` | `self->get(ArrayList* self, int index)` | index 위치 항목 반환 |
| `void` | `self->remove(ArrayList* self, int index)` | index 항목 삭제 (RELEASE 포함) |
| `[OWNED] Object*` | `self->detach(ArrayList* self, int index)` | 항목 분리 (RELEASE 안 함) |
| `int` | `self->getSize(ArrayList* self)` | 현재 항목 수 |
| `void` | `self->clear(ArrayList* self)` | 전체 항목 제거 |
| `bool` | `self->isEmpty(ArrayList* self)` | 비어있는지 여부 |
| `void` | `self->forEach(ArrayList* self, ArrayListActionFunc fn)` | 전체 항목에 콜백 적용 |
| `void*` | `self->find(ArrayList* self, void* target, ArrayListCompareFunc cmp)` | 조건에 맞는 첫 항목 반환 |
| `void` | `self->sort(ArrayList* self, ArrayListCompareFunc cmp)` | 비교함수 기준 정렬 |
| `[OWNED] ArrayListIterator*` | `self->iterator(ArrayList* self)` | 이터레이터 생성 |
| `bool` | `it->hasNext(ArrayListIterator* self)` | 다음 항목 존재 여부 |
| `[BORROWED] Object*` | `it->next(ArrayListIterator* self)` | 다음 항목 반환 |

---

## 📦 hashmap — `hashmap.h / hashmap.c`

```
Object
└── HashMap
```

문자열 키 기반 해시맵. Object 상속, Thread-Safe.

> `keys()` / `values()` 반환값은 사용 후 RELEASE 필수.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] HashMap*` | `new_HashMap(int initial_capacity)` | 해시맵 생성 |
| `void` | `self->put(HashMap* self, const char* key, Object* value)` | 키-값 저장 (RETAIN 포함) |
| `[BORROWED] Object*` | `self->get(HashMap* self, const char* key)` | 키로 값 조회 |
| `bool` | `self->hasKey(HashMap* self, const char* key)` | 키 존재 여부 |
| `void` | `self->remove(HashMap* self, const char* key)` | 키-값 삭제 (RELEASE 포함) |
| `[OWNED] Object*` | `self->detach(HashMap* self, const char* key)` | 값 분리 (RELEASE 안 함) |
| `void` | `self->clear(HashMap* self)` | 전체 항목 제거 |
| `void` | `self->forEach(HashMap* self, void (*action)(const char*, Object*))` | 전체 항목 순회 |
| `int` | `self->getSize(HashMap* self)` | 저장된 항목 수 |
| `bool` | `self->isEmpty(HashMap* self)` | 비어있는지 여부 |
| `[OWNED] ArrayList*` | `self->keys(HashMap* self)` | 전체 키 목록 — 사용 후 RELEASE 필수 |
| `[OWNED] ArrayList*` | `self->values(HashMap* self)` | 전체 값 목록 — 사용 후 RELEASE 필수 |

---

## 📦 hashtable — `hashtable.h / hashtable.c`

```
Object
└── Hashtable
    └── HashtableIterator
```

제네릭 키-값 해시테이블. Fail-Fast Iterator, containsValue 지원.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Hashtable*` | `new_Hashtable(size_t initialCapacity, float loadFactor)` | 해시테이블 생성 |
| `Object*` | `self->put(Hashtable* self, Object* key, Object* value)` | 키-값 저장, 이전 값 반환 |
| `[BORROWED] Object*` | `self->get(Hashtable* self, Object* key)` | 키로 값 조회 |
| `Object*` | `self->remove(Hashtable* self, Object* key)` | 키-값 삭제 |
| `bool` | `self->containsKey(Hashtable* self, Object* key)` | 키 존재 여부 |
| `bool` | `self->containsValue(Hashtable* self, Object* value)` | 값 존재 여부 O(N) |
| `size_t` | `self->size(Hashtable* self)` | 저장된 항목 수 |
| `bool` | `self->isEmpty(Hashtable* self)` | 비어있는지 여부 |
| `void` | `self->clear(Hashtable* self)` | 전체 항목 제거 |
| `void` | `self->forEach(Hashtable* self, BiConsumer action)` | 전체 항목 순회 |
| `[OWNED] HashtableIterator*` | `self->iterator(Hashtable* self)` | Fail-Fast 이터레이터 생성 |
| `[OWNED] ArrayList*` | `self->keys(Hashtable* self)` | 전체 키 목록 |
| `[OWNED] ArrayList*` | `self->values(Hashtable* self)` | 전체 값 목록 |
| `bool` | `it->hasNext(HashtableIterator* self)` | 다음 항목 존재 여부 |
| `bool` | `it->next(HashtableIterator* self, Object** key, Object** value)` | 다음 키-값 반환 |
| `void` | `it->remove(HashtableIterator* self)` | 현재 항목 삭제 |

---

## 📦 queue — `queue.h / queue.c`

```
Object
└── Queue
```

FIFO 큐. Object 상속, Thread-Safe.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Queue*` | `new_Queue(int initial_capacity)` | 큐 생성 |
| `void` | `self->enqueue(Queue* self, void* data)` | 데이터 삽입 (뒤에 추가) |
| `void*` | `self->dequeue(Queue* self)` | 데이터 꺼내기 (앞에서 제거) |
| `void*` | `self->peek(Queue* self)` | 앞 항목 확인 (제거 없음) |
| `bool` | `self->isEmpty(Queue* self)` | 비어있는지 여부 |
| `int` | `self->size(Queue* self)` | 현재 항목 수 |
| `void` | `self->forEach(Queue* self, void (*action)(Object*))` | 전체 항목 순회 |
| `[OWNED] ArrayListIterator*` | `self->iterator(Queue* self)` | 이터레이터 생성 |

---

## 📦 stack — `stack.h / stack.c`

```
Object
└── Stack
    └── [내부] ArrayList  (위임)
```

LIFO 스택. Object 상속, ArrayList 위임, Thread-Safe.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Stack*` | `new_Stack(int initial_capacity)` | 스택 생성 |
| `void` | `self->push(Stack* self, void* data)` | 데이터 삽입 (LIFO) |
| `void*` | `self->pop(Stack* self)` | 맨 위 항목 꺼내기 |
| `void*` | `self->peek(Stack* self)` | 맨 위 항목 확인 (제거 없음) |
| `bool` | `self->isEmpty(Stack* self)` | 비어있는지 여부 |
| `int` | `self->size(Stack* self)` | 현재 항목 수 |

---

## 📦 vector — `vector.h / vector.c`

```
Object
└── Vector
    └── VectorIterator
```

C++ STL 스타일 동적 배열. Object 상속, Thread-Safe, 수동 lock/unlock.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Vector*` | `new_Vector(int initial_capacity)` | 벡터 생성 |
| `void` | `self->push_back(Vector* self, Object* item)` | 뒤에 항목 추가 |
| `[BORROWED] Object*` | `self->at(Vector* self, int index)` | index 위치 항목 반환 |
| `[OWNED] Object*` | `self->pop_back(Vector* self)` | 마지막 항목 꺼내기 |
| `int` | `self->get_size(Vector* self)` | 현재 항목 수 |
| `void` | `self->lock(Vector* self)` | 수동 뮤텍스 획득 |
| `void` | `self->unlock(Vector* self)` | 수동 뮤텍스 해제 |
| `VectorIterator` | `self->begin(Vector* self)` | 시작 이터레이터 반환 |
| `VectorIterator` | `self->end(Vector* self)` | 끝 이터레이터 반환 |

---

## 📦 list — `list.h / list.c`

```
Object
└── List
```

이중 연결 리스트. Object 상속, Thread-Safe.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] List*` | `new_List(void)` | 이중 연결 리스트 생성 |
| `void` | `self->pushBack(List* self, Object* data)` | 뒤에 항목 추가 O(1) |
| `void` | `self->pushFront(List* self, Object* data)` | 앞에 항목 추가 O(1) |
| `[OWNED] Object*` | `self->popBack(List* self)` | 뒤 항목 꺼내기 O(1) |
| `[OWNED] Object*` | `self->popFront(List* self)` | 앞 항목 꺼내기 O(1) |
| `void` | `self->insertAt(List* self, int index, Object* data)` | index 위치에 삽입 O(N) |
| `[OWNED] Object*` | `self->removeAt(List* self, int index)` | index 항목 제거 O(N) |
| `[BORROWED] Object*` | `self->get(List* self, int index)` | index 항목 조회 O(N) |
| `void` | `self->clear(List* self)` | 전체 항목 제거 |
| `int` | `self->getSize(List* self)` | 현재 항목 수 |

---

## 📦 linked_list — `linked_list.h / linked_list.c`

```
Object
└── LinkedList
    └── LinkedListNode  (내부)
```

단순 연결 리스트. Object 상속, Thread-Safe.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] LinkedList*` | `new_LinkedList(void)` | 단순 연결 리스트 생성 |
| `void` | `self->add_node(LinkedList* self, void* data)` | 노드 추가 |
| `void` | `self->delete_node(LinkedList* self, void* data, int (*cmp)(...))` | 비교함수 기준 노드 삭제 |
| `void` | `self->print_list(LinkedList* self, void (*display)(Object*))` | 전체 노드 출력 |
| `int` | `self->getSize(LinkedList* self)` | 현재 노드 수 |

---

## 📦 btree — `btree.h / btree.c`

```
Object
└── BTree
    └── BTreeNode  (내부)
```

B-트리 (차수 t). Object 상속, Thread-Safe.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] BTree*` | `new_BTree(int t)` | B-트리 생성 (차수 t) |
| `void` | `self->insert(BTree* self, Object* key, Object* value)` | 키-값 삽입 |
| `[BORROWED] Object*` | `self->search(BTree* self, Object* key)` | 키로 값 검색 |
| `void` | `self->clear(BTree* self)` | 전체 노드 제거 |
| `int` | `self->getSize(BTree* self)` | 저장된 키 수 |

---

## 📦 tree — `tree.h / tree.c`

```
Object
├── Tree
│   └── TreeNode  (내부)
└── TreeIterator
```

이진 탐색 트리 (BST). Object 상속, 외부 이터레이터, BFS 순회 지원.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Tree*` | `new_Tree(CompareFunc cmp)` | BST 생성 |
| `[OWNED] TreeIterator*` | `new_TreeIterator(Tree* tree)` | 중위순회 이터레이터 생성 |
| `void` | `self->insert(Tree* self, Object* data)` | 데이터 삽입 |
| `[BORROWED] Object*` | `self->search(Tree* self, Object* key)` | 키로 데이터 검색 |
| `void` | `self->remove(Tree* self, Object* key)` | 키 데이터 삭제 |
| `void` | `self->foreach(Tree* self, void (*func)(Object*))` | 전체 노드 순회 |
| `void` | `self->traverseBFS(Tree* self)` | 너비우선 순회 출력 |
| `int` | `self->getHeight(TreeNode* node)` | 트리 높이 반환 |
| `void` | `self->clear(Tree* self)` | 전체 노드 제거 |
| `bool` | `it->hasNext(TreeIterator* self)` | 다음 노드 존재 여부 |
| `[BORROWED] Object*` | `it->next(TreeIterator* self)` | 다음 노드 반환 |

---

## 📦 json — `json.h / json.c`

```
Object
└── JSONNode
    └── JsonValue  (J_NULL / J_BOOL / J_NUMBER / J_STRING)
```

JSON 파서/직렬화. HashMap/ArrayList 연동, Jackson 스타일 ObjectMapper.

> `toString()` / `writeValueAsString()` 반환값은 사용 후 `free()` 필수.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] JSONNode*` | `new_JSON(const char* json_str_or_null)` | JSON 노드 생성 (NULL=빈 객체) |
| `Object*` | `json_parse(const char* json_str)` | JSON 문자열 파싱 → HashMap*/ArrayList* |
| `const JSON*` | `GetJSON(void)` | 싱글톤 JSON 엔진 반환 |
| `const ObjectMapper*` | `GetObjectMapper(void)` | Jackson 스타일 ObjectMapper 반환 |
| `int` | `node->isObject(JSONNode* self)` | JSON 객체 여부 |
| `int` | `node->isArray(JSONNode* self)` | JSON 배열 여부 |
| `void` | `node->put(JSONNode* self, const char* key, Object* val)` | 객체에 키-값 추가 |
| `[BORROWED] Object*` | `node->get(JSONNode* self, const char* key)` | 객체에서 값 조회 |
| `const char*` | `node->getString(JSONNode* self, const char* key)` | 문자열 값 직접 조회 |
| `int` | `node->getInt(JSONNode* self, const char* key)` | 정수 값 직접 조회 |
| `void` | `node->add(JSONNode* self, Object* val)` | 배열에 항목 추가 |
| `[BORROWED] Object*` | `node->getIndex(JSONNode* self, int index)` | 배열 index 항목 조회 |
| `int` | `node->length(JSONNode* self)` | 배열 길이 반환 |
| `char*` | `node->toString(JSONNode* self)` | JSON 직렬화 — 사용 후 `free()` 필수 |
| `char*` | `mapper->writeValueAsString(Object* obj)` | 객체 JSON 직렬화 — 사용 후 `free()` 필수 |

---

## 📦 thread — `thread.h / thread.c`

```
Object
└── Thread
```

POSIX 스레드 래퍼. Object 상속, 5단계 상태머신.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Thread*` | `new_Thread(Runnable run_func, void* arg)` | 스레드 생성 |
| `void` | `self->start(Thread* self)` | 스레드 시작 (pthread_create) |
| `void*` | `self->join(Thread* self)` | 완료 대기 후 결과 반환 |
| `void` | `self->detach(Thread* self)` | 독립 실행 (결과 수신 불가) |

---

## 📦 threadpool — `threadpool.h / threadpool.c`

```
Object
└── ThreadPool
```

스레드 풀. 작업 큐 기반, parallel_for 지원.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] ThreadPool*` | `new_ThreadPool(int num_threads, int max_resources)` | 스레드 풀 생성 |
| `void` | `self->submit(ThreadPool* self, TaskRoutine func, void* arg)` | 작업 제출 |
| `void*` | `self->parallel_for(ThreadPool* self, ArrayList* list, Consumer fn)` | 병렬 처리 |
| `void` | `self->shutdown(ThreadPool* self)` | 풀 종료 (모든 워커 join) |
| `int` | `self->getPendingCount(ThreadPool* self)` | 대기 중 작업 수 반환 |

---

## 📦 semaphore_obj — `semaphore_obj.h / semaphore_obj.c`

```
Object
└── Semaphore
```

POSIX 세마포어 래퍼. Object 상속.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Semaphore*` | `new_Semaphore(int initial_value)` | 세마포어 생성 |
| `void` | `self->wait(Semaphore* self)` | P 연산 (자원 획득, 블로킹) |
| `void` | `self->post(Semaphore* self)` | V 연산 (자원 반납) |
| `bool` | `self->tryWait(Semaphore* self)` | 비차단 P 연산 |
| `int` | `self->getValue(Semaphore* self)` | 현재 잔여 자원 수 |

---

## 📦 logger — `logger.h / logger.c`

```
Object
└── Logger
    └── AsyncLogger
```

동기 로거. Object 상속, 파일/콘솔 출력, 외부 Appender 지원.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Logger*` | `new_Logger(int level)` | 로거 생성 |
| `void` | `self->setLogFile(Logger* self, const char* path)` | 로그 파일 경로 설정 |
| `void` | `self->setLevel(Logger* self, int level)` | 로그 레벨 설정 |
| `void` | `self->addAppender(Logger* self, LogAppenderCallback cb, void* data)` | 외부 Appender 등록 |
| `void` | `self->debug(Logger* self, const char* fmt, ...)` | DEBUG 레벨 로그 |
| `void` | `self->info(Logger* self, const char* fmt, ...)` | INFO 레벨 로그 |
| `void` | `self->warn(Logger* self, const char* fmt, ...)` | WARN 레벨 로그 |
| `void` | `self->error(Logger* self, const char* fmt, ...)` | ERROR 레벨 로그 |

---

## 📦 async_logger — `async_logger.h / async_logger.c`

```
Object
└── Logger
    └── AsyncLogger
```

비동기 로거. Logger 상속, 큐 기반 배치 처리.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] AsyncLogger*` | `new_AsyncLogger(int level)` | 비동기 로거 생성 |
| `void` | `AsyncLogger_log(AsyncLogger* self, int level, const char* file, int line, const char* fmt, ...)` | 로그 큐에 삽입 |
| `void` | `self->start(AsyncLogger* self)` | 워커 스레드 시작 |
| `void` | `self->stop(AsyncLogger* self)` | 워커 스레드 종료 (flush 후) |

---

## 📦 exception — `exception.h / exception.c`

```
Object
└── Exception
```

예외 처리. ErrorCode 체계, Cause 체이닝 지원.

> 매크로: `throw_Exception(code, msg)` · `throw_ExceptionCause(code, msg, cause)`

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Exception*` | `new_Exception(ErrorCode code, const char* msg, Exception* cause, const char* file, int line)` | 예외 생성 |
| `const char*` | `self->getMessage(Exception* self)` | 에러 메시지 반환 |
| `[BORROWED] Exception*` | `self->getCause(Exception* self)` | 원인 예외 반환 |
| `ErrorCode` | `self->getCode(Exception* self)` | 에러 코드 반환 |
| `bool` | `self->hasCause(Exception* self)` | 원인 예외 존재 여부 |
| `void` | `self->printStackTrace(Exception* self)` | 스택 트레이스 출력 |
| `const char*` | `ErrorCode_toString(ErrorCode code)` | ErrorCode → 문자열 변환 |

---

## 📦 path — `path.h / path.c`

```
Object
└── Path
```

파일 경로 처리. Java Path 스타일.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Path*` | `new_Path(const char* pathStr)` | 경로 객체 생성 |
| `[OWNED] String*` | `self->getFileName(Path* self)` | 파일명 반환 |
| `[OWNED] String*` | `self->getBaseName(Path* self)` | 확장자 제외 파일명 |
| `[OWNED] String*` | `self->getExtension(Path* self)` | 확장자 반환 |
| `[OWNED] String*` | `self->getParent(Path* self)` | 부모 디렉토리 경로 |
| `[OWNED] Path*` | `self->getCanonicalPath(Path* self)` | 절대 정규화 경로 |
| `[OWNED] Path*` | `self->normalize(Path* self)` | 경로 정규화 |
| `[OWNED] Path*` | `self->toAbsolute(Path* self)` | 절대 경로 변환 |
| `bool` | `self->isAbsolute(Path* self)` | 절대 경로 여부 |
| `[OWNED] Path*` | `self->resolve(Path* self, const char* child)` | 자식 경로 결합 |
| `[OWNED] Path*` | `self->sibling(Path* self, const char* name)` | 형제 경로 생성 |
| `[OWNED] Path*` | `self->withExt(Path* self, const char* newExt)` | 확장자 변경 |
| `bool` | `self->equals(Path* self, Path* other)` | 경로 동등 비교 |

---

## 📦 file — `file.h / file.c`

```
Object
└── File
```

파일 I/O. Java File 스타일, ARC 완전 적용.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] File*` | `new_File(const char* pathStr)` | 파일 객체 생성 |
| `bool` | `self->exists(File* self)` | 파일 존재 여부 |
| `int64_t` | `self->length(File* self)` | 파일 크기 (bytes) |
| `bool` | `self->isFile(File* self)` | 일반 파일 여부 |
| `bool` | `self->isSymlink(File* self)` | 심볼릭 링크 여부 |
| `bool` | `self->isReadable(File* self)` | 읽기 권한 여부 |
| `bool` | `self->isWritable(File* self)` | 쓰기 권한 여부 |
| `bool` | `self->canExecute(File* self)` | 실행 권한 여부 |
| `int64_t` | `self->lastModifiedMs(File* self)` | 마지막 수정 시각 (ms) |
| `[OWNED] String*` | `self->readAllText(File* self)` | 전체 텍스트 읽기 |
| `[OWNED] ByteBuffer*` | `self->readAllBytes(File* self)` | 전체 바이트 읽기 |
| `[OWNED] ArrayList*` | `self->readLines(File* self)` | 라인 단위 읽기 |
| `bool` | `self->writeString(File* self, String* content)` | 텍스트 쓰기 (덮어쓰기) |
| `bool` | `self->appendString(File* self, String* content)` | 텍스트 추가 쓰기 |
| `bool` | `self->copyTo(File* self, Path* destPath)` | 다른 경로로 복사 |
| `bool` | `self->deleteFile(File* self)` | 파일 삭제 |
| `bool` | `self->renameAtomic(File* self, Path* newPath)` | 원자적 이름 변경 |
| `bool` | `self->fsync(File* self)` | 디스크 동기화 |
| `bool` | `self->lockExclusive(File* self)` | 배타적 파일 잠금 |
| `void` | `self->unlock(File* self)` | 파일 잠금 해제 |
| `[OWNED] String*` | `self->md5(File* self)` | MD5 해시 계산 |
| `[OWNED] String*` | `self->sha256(File* self)` | SHA-256 해시 계산 |

---

## 📦 file_util — `file_util.h / file_util.c`

```
(정적 헬퍼 — 인스턴스 없음)
FileUtil
```

파일 시스템 유틸리티. 정적 헬퍼 함수 모음.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] File*` | `FileUtil_tmp(void)` | 임시 디렉토리 반환 |
| `[OWNED] File*` | `FileUtil_home(void)` | 홈 디렉토리 반환 |
| `[OWNED] File*` | `FileUtil_cwd(void)` | 현재 작업 디렉토리 반환 |
| `CoreResult` | `FileUtil_copy(const File* src, const File* dest)` | 파일 복사 |
| `CoreResult` | `FileUtil_move(File* src, const File* dest)` | 파일 이동 |
| `[OWNED] File*` | `FileUtil_createTemp(const char* dir, const char* prefix)` | 임시 파일 생성 |
| `bool` | `FileUtil_exists(const char* path)` | 경로 존재 여부 |
| `bool` | `FileUtil_mkdirs(const char* path)` | 디렉토리 재귀 생성 |
| `void` | `FileUtil_delete(const char* path)` | 파일/디렉토리 삭제 |

---

## 📦 file_watcher — `file_watcher.h / file_watcher.c`

```
Object
└── FileWatcher
```

inotify 기반 파일 감시. IN_NONBLOCK, EventLoop 연동.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] FileWatcher*` | `new_FileWatcher(void)` | 파일 감시자 생성 |
| `bool` | `self->watch(FileWatcher* self, Path* target)` | 경로 감시 시작 |
| `void` | `self->onEvent(FileWatcher* self, EventCallback cb)` | 이벤트 콜백 등록 |
| `void` | `self->poll(FileWatcher* self)` | 이벤트 폴링 (수동 호출) |
| `void` | `self->stop(FileWatcher* self)` | 감시 중지 |

---

## 📦 mapped_file — `mapped_file.h / mapped_file.c`

```
Object
└── MappedFile
```

mmap 기반 메모리 맵 파일. ByteBuffer 연동.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] MappedFile*` | `new_MappedFile(const char* pathStr)` | 메모리 맵 파일 생성 |
| `bool` | `self->map(MappedFile* self, bool readOnly)` | 파일 메모리 매핑 |
| `void` | `self->unmap(MappedFile* self)` | 매핑 해제 |
| `bool` | `self->sync(MappedFile* self)` | 변경사항 디스크 동기화 (msync) |
| `[OWNED] ByteBuffer*` | `self->asByteBuffer(MappedFile* self)` | ByteBuffer로 변환 (copy 기반) |

---

## 📦 directory — `directory.h / directory.c`

```
Object
└── Directory
```

디렉토리 조작. Java Directory 스타일.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Directory*` | `new_Directory(const char* pathStr)` | 디렉토리 객체 생성 |
| `bool` | `self->exists(Directory* self)` | 디렉토리 존재 여부 |
| `bool` | `self->mkdirs(Directory* self)` | 디렉토리 재귀 생성 |
| `[OWNED] ArrayList*` | `self->listFiles(Directory* self)` | 직접 하위 파일 목록 |
| `[OWNED] ArrayList*` | `self->walkTree(Directory* self)` | 전체 트리 순회 목록 |
| `bool` | `self->deleteRecursive(Directory* self)` | 재귀 삭제 |

---

## 📦 bytebuffer — `bytebuffer.h / bytebuffer.c`

```
Object
└── ByteBuffer
```

Java NIO ByteBuffer 스타일. 동적 확장, 엔디안 변환, compact 자동화.

> 매크로: `BB_REMAINING(bb)` · `BB_WRITABLE(bb)` · `BB_CLEAR(bb)`. 최대 16MB.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] ByteBuffer*` | `new_ByteBuffer(size_t capacity)` | 버퍼 생성 |
| `int` | `self->writeByte(ByteBuffer* self, uint8_t b)` | 1바이트 쓰기 |
| `int` | `self->writeInt32(ByteBuffer* self, int32_t v)` | 4바이트 정수 쓰기 (Big-Endian) |
| `int` | `self->write(ByteBuffer* self, const void* buf, size_t len)` | 바이트 배열 쓰기 |
| `bool` | `self->readByte(ByteBuffer* self, uint8_t* out)` | 1바이트 읽기 |
| `bool` | `self->readInt32(ByteBuffer* self, int32_t* out)` | 4바이트 정수 읽기 (Big-Endian) |
| `bool` | `self->peekInt32(ByteBuffer* self, int32_t* out)` | 4바이트 확인 (포인터 이동 없음) |
| `size_t` | `self->read(ByteBuffer* self, void* buf, size_t len)` | 바이트 배열 읽기 |
| `void` | `self->compact(ByteBuffer* self)` | 읽은 데이터 제거, 공간 확보 |
| `void` | `self->rewind(ByteBuffer* self)` | 읽기 포인터 초기화 |
| `void` | `self->skip(ByteBuffer* self, size_t len)` | 읽기 포인터 건너뛰기 |
| `ssize_t` | `self->indexOf(ByteBuffer* self, uint8_t target)` | 특정 바이트 위치 탐색 |
| `size_t` | `self->remaining(ByteBuffer* self)` | 남은 읽기 가능 바이트 수 |
| `size_t` | `self->writableBytes(ByteBuffer* self)` | 쓰기 가능 바이트 수 |
| `[OWNED] ByteBuffer*` | `self->readSlice(ByteBuffer* self, size_t len)` | 슬라이스 추출 |

---

## 📦 ring_buffer — `ring_buffer.h / ring_buffer.c`

```
Object
└── RingBuffer
```

고성능 원형 버퍼. Thread-Safe, popWait 블로킹 지원.

> `push` 실패(버퍼 가득) 시 `false` 반환 — EventLoop 블로킹 금지 설계.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] RingBuffer*` | `new_RingBuffer(size_t capacity)` | 원형 버퍼 생성 |
| `bool` | `self->push(RingBuffer* self, void* item)` | 항목 삽입 (가득 차면 false) |
| `void*` | `self->pop(RingBuffer* self)` | 항목 꺼내기 (비어있으면 NULL) |
| `void*` | `self->popWait(RingBuffer* self, int timeout_ms)` | 항목 대기 후 꺼내기 (블로킹) |
| `size_t` | `self->getSize(RingBuffer* self)` | 현재 항목 수 |
| `bool` | `self->isEmpty(RingBuffer* self)` | 비어있는지 여부 |
| `bool` | `self->isFull(RingBuffer* self)` | 가득 찼는지 여부 |
| `void` | `self->clear(RingBuffer* self)` | 전체 항목 제거 |

---

## 📦 socket_base — `socket_base.h / socket_base.c`

```
Object
└── Socket  (추상 기반)
    ├── TcpSocket
    ├── UdpSocket
    └── UnixSocket
```

소켓 추상 기반 클래스. TCP/UDP/Unix 공통 인터페이스.

> `SOCKET_WOULD_BLOCK` 반환 시 엣지트리거 루프 종료 처리 필수.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `void` | `Socket_init_base(Socket* self, int fd, SocketProtocol protocol)` | 소켓 베이스 초기화 |
| `ssize_t` | `self->send(Socket* self, const void* buf, size_t len, const char* host, int port)` | 데이터 전송 |
| `ssize_t` | `self->recv(Socket* self, void* buf, size_t len, char* host, int* port)` | 데이터 수신 |
| `int` | `self->getFD(Socket* self)` | fd 반환 |
| `void` | `self->close(Socket* self)` | 소켓 닫기 |
| `int` | `self->bind(Socket* self, const char* host, int port)` | 주소 바인딩 |
| `int` | `self->listen(Socket* self, int backlog)` | 연결 대기 |
| `int` | `self->connect(Socket* self, const char* host, int port)` | 서버 연결 |

---

## 📦 tcp_socket — `tcp_socket.h / tcp_socket.c`

```
Object
└── Socket
    └── TcpSocket
```

TCP 소켓. 서버/클라이언트 생성, accept 지원.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] TcpSocket*` | `new_TcpServer(const char* host, int port)` | TCP 서버 소켓 생성 + bind + listen |
| `[OWNED] TcpSocket*` | `new_TcpClient(const char* host, int port)` | TCP 클라이언트 소켓 생성 + connect |
| `[OWNED] TcpSocket*` | `new_TcpSocket_from_fd(int fd)` | 기존 fd로 소켓 래핑 |
| `[OWNED] TcpSocket*` | `self->accept(TcpSocket* self, char* ip, int* port)` | 클라이언트 연결 수락 |

---

## 📦 udp_socket — `udp_socket.h / udp_socket.c`

```
Object
└── Socket
    └── UdpSocket
```

UDP 소켓. 서버/클라이언트 생성.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] UdpSocket*` | `new_UdpServer(const char* host, int port)` | UDP 서버 소켓 생성 + bind |
| `[OWNED] UdpSocket*` | `new_UdpClient(void)` | UDP 클라이언트 소켓 생성 |
| `[OWNED] UdpSocket*` | `new_UdpSocket_from_fd(int fd)` | 기존 fd로 소켓 래핑 |

---

## 📦 unix_socket — `unix_socket.h / unix_socket.c`

```
Object
└── Socket
    └── UnixSocket
```

Unix 도메인 소켓. 프로세스 간 통신 (IPC).

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] UnixSocket*` | `new_UnixServer(const char* path)` | Unix 서버 소켓 생성 + bind + listen |
| `[OWNED] UnixSocket*` | `new_UnixClient(const char* path)` | Unix 클라이언트 소켓 생성 + connect |
| `[OWNED] UnixSocket*` | `new_UnixSocket_from_fd(int fd)` | 기존 fd로 소켓 래핑 |
| `[OWNED] UnixSocket*` | `self->accept(UnixSocket* self, char* path)` | 클라이언트 연결 수락 |

---

## 📦 event_loop — `event_loop.h / event_loop.c`

```
Object
└── EventLoop
```

epoll 기반 이벤트 루프. EPOLLET 엣지 트리거, 소켓 + 타이머 통합.

> `volatile bool is_running` — SIGINT 즉시 인지. EINTR 자동 재시도.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] EventLoop*` | `new_EventLoop(int max_events)` | 이벤트 루프 생성 |
| `void` | `event_loop_run(EventLoop* self)` | 이벤트 루프 실행 (블로킹) |
| `int` | `event_loop_add_timer(EventLoop* self, Timer* timer)` | 타이머 등록 |
| `int` | `event_loop_remove_timer(EventLoop* self, Timer* timer)` | 타이머 제거 |
| `int` | `self->addSocket(EventLoop* self, Socket* sock, EventMask mask)` | 소켓 등록 |
| `int` | `self->delSocket(EventLoop* self, Socket* sock)` | 소켓 제거 |
| `int` | `self->poll(EventLoop* self, int timeout_ms)` | 단일 폴링 (비블로킹) |
| `void` | `self->run(EventLoop* self)` | 이벤트 루프 실행 |
| `void` | `self->stop(EventLoop* self)` | 이벤트 루프 중지 |

---

## 📦 timer — `timer.h / timer.c`

```
Object
└── Timer
```

timerfd 기반 타이머. 반복/단발 지원. EventLoop 연동.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Timer*` | `new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* ud)` | 타이머 생성 |
| `[OWNED] Timer*` | `new_TimerNamed(const char* name, long ms, bool repeating, TimerCallback cb, void* ud)` | 이름 있는 타이머 생성 |
| `void` | `on_timer_event(Timer* self)` | 타이머 이벤트 처리 (EventLoop 내부 호출) |
| `bool` | `self->start(Timer* self)` | 타이머 시작 |
| `void` | `self->stop(Timer* self)` | 타이머 중지 |
| `void` | `self->reset(Timer* self)` | 타이머 리셋 |
| `bool` | `self->isActive(Timer* self)` | 타이머 동작 여부 |

---

## 📦 scheduler — `scheduler.h / scheduler.c`

```
Object
└── Scheduler
    ├── [uses] ThreadPool
    └── [uses] EventLoop
```

timerfd + epoll + ThreadPool 연동 스케줄러.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Scheduler*` | `new_Scheduler(ThreadPool* pool, EventLoop* loop)` | 스케줄러 생성 |
| `bool` | `self->add(Scheduler* self, const char* name, long ms, bool repeat, TimerCallback cb, void* ud)` | 작업 등록 |
| `bool` | `self->addEx(Scheduler* self, const char* name, long ms, bool repeat, JobPriority prio, TimerCallback cb, void* ud)` | 우선순위 포함 작업 등록 |
| `bool` | `self->remove(Scheduler* self, const char* name)` | 이름으로 작업 제거 |
| `void` | `self->start(Scheduler* self)` | 스케줄러 시작 |
| `void` | `self->stop(Scheduler* self)` | 스케줄러 중지 |
| `size_t` | `self->count(Scheduler* self)` | 등록된 작업 수 |

---

## 📦 context — `context.h / context.c`

```
Object
└── Context
```

키-값 런타임 컨텍스트. Thread-Safe.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Context*` | `new_Context(void)` | 컨텍스트 생성 |
| `void` | `self->set(Context* self, const char* key, Object* value)` | 키-값 저장 |
| `[BORROWED] Object*` | `self->get(Context* self, const char* key)` | 키로 값 조회 |
| `[OWNED] Object*` | `self->remove(Context* self, const char* key)` | 키-값 제거 |
| `bool` | `self->has(Context* self, const char* key)` | 키 존재 여부 |
| `void` | `self->clear(Context* self)` | 전체 제거 |
| `int` | `self->getSize(Context* self)` | 저장된 항목 수 |
| `void` | `self->setString(Context* self, const char* key, const char* val)` | 문자열 값 저장 |
| `[BORROWED] String*` | `self->getString(Context* self, const char* key)` | 문자열 값 조회 |
| `void` | `self->setInt(Context* self, const char* key, int val)` | 정수 값 저장 |
| `int` | `self->getInt(Context* self, const char* key)` | 정수 값 조회 |

---

## 📦 config — `config.h / config.c`

```
Object
└── Config
```

INI/설정 파일 로더. HashMap 기반.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Config*` | `new_Config(void)` | 설정 객체 생성 |
| `bool` | `self->load(Config* self, const char* path)` | 설정 파일 로드 |
| `const char*` | `self->get(Config* self, const char* key)` | 값 조회 (NULL 반환 가능) |
| `const char*` | `self->getString(Config* self, const char* key, const char* def)` | 문자열 조회 (기본값 포함) |
| `int` | `self->getInt(Config* self, const char* key, int def)` | 정수 조회 (기본값 포함) |
| `bool` | `self->getBool(Config* self, const char* key, bool def)` | bool 조회 (기본값 포함) |

---

## 📦 service_registry — `service_registry.h / service_registry.c`

```
Object
└── ServiceRegistry
```

DI 컨테이너. Class 타입 기반 서비스 등록/조회.

> 매크로: `REG_GET(ClassName)` 으로 편리하게 조회 가능.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] ServiceRegistry*` | `new_ServiceRegistry(void)` | 서비스 레지스트리 생성 |
| `void` | `self->register_s(ServiceRegistry* self, const Class* cls, Object* service)` | 서비스 등록 |
| `[BORROWED] Object*` | `self->get(ServiceRegistry* self, const Class* cls)` | 타입으로 서비스 조회 |
| `bool` | `self->has(ServiceRegistry* self, const Class* cls)` | 서비스 등록 여부 |
| `void` | `self->unregister(ServiceRegistry* self, const Class* cls)` | 서비스 제거 |

---

## 📦 app_context — `app_context.h / app_context.c`

```
Object
└── AppContext
    ├── [owns] Config
    ├── [owns] Context
    └── [owns] ServiceRegistry
```

통합 애플리케이션 컨텍스트. Config + Context + ServiceRegistry 통합.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] AppContext*` | `new_AppContext(void)` | 앱 컨텍스트 생성 |
| `bool` | `self->init(AppContext* self)` | 초기화 |
| `void` | `self->destroy_all(AppContext* self)` | 전체 자원 해제 |
| `void` | `self->setConfig(AppContext* self, const char* key, const char* val)` | 설정 저장 |
| `[BORROWED] String*` | `self->getConfig(AppContext* self, const char* key)` | 설정 조회 |
| `int` | `self->getConfigInt(AppContext* self, const char* key)` | 정수 설정 조회 |
| `void` | `self->registerService(AppContext* self, const Class* cls, Object* service)` | 서비스 등록 |
| `[BORROWED] Object*` | `self->getService(AppContext* self, const Class* cls)` | 서비스 조회 |

---

## 📦 db / mysql — `db.h / db.c / mysql.c`

```
Object
└── DBClient
    └── [driver] MySQLDriver  (bind_mysql 주입)
```

MySQL/MariaDB ARC 클라이언트. Thread-Safe Snapshot, Zero-Malloc DBOption, 35개 함수.

> `escape_string()` 반환값은 반드시 `free()` 필수.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] DBClient*` | `new_DBClient(void)` | dbconfig.conf 기반 클라이언트 생성 |
| `[OWNED] DBClient*` | `new_DBClientDirect(host, dbname, id, pw, port, cs, type)` | 직접 파라미터로 생성 |
| `void` | `self->setOption(DBClient* self, int option, const void* arg, size_t size)` | DB 옵션 설정 (Lazy Init) |
| `int` | `self->connect(DBClient* self)` | DB 연결 |
| `int` | `self->reconnect(DBClient* self)` | 재연결 (옵션 자동 복원) |
| `void` | `self->disconnect(DBClient* self)` | DB 연결 해제 |
| `int` | `self->sqlQuery(DBClient* self, const char* sql)` | 직접 SQL 실행 |
| `char*` | `self->escape_string(DBClient* self, const char* str)` | SQL 이스케이프 — 반드시 `free()` |
| `int` | `self->beginTransaction(DBClient* self)` | 트랜잭션 시작 |
| `int` | `self->commit(DBClient* self)` | 트랜잭션 커밋 |
| `int` | `self->rollback(DBClient* self)` | 트랜잭션 롤백 |
| `int` | `self->insertTable(DBClient* self, const char* table, HashMap* data)` | 행 삽입 |
| `int` | `self->updateTable(DBClient* self, const char* table, HashMap* data, const char* cond)` | 행 수정 |
| `int` | `self->replaceTable(DBClient* self, const char* table, HashMap* data)` | 행 REPLACE |
| `int` | `self->deleteTable(DBClient* self, const char* table, const char* cond)` | 조건 행 삭제 |
| `HashMap*` | `self->getRecordFromQuery(DBClient* self, const char* sql)` | SQL로 단일 행 조회 |
| `ArrayList*` | `self->getRecordsFromQuery(DBClient* self, const char* sql)` | SQL로 다중 행 조회 |
| `HashMap*` | `self->getRecord(DBClient* self, const char* table, const char* cond, const char* field)` | 조건으로 단일 행 조회 |
| `ArrayList*` | `self->getRecords(DBClient* self, const char* table, const char* cond, const char* fields)` | 조건으로 다중 행 조회 |
| `int` | `self->getDataCount(DBClient* self, const char* table, const char* cond)` | 조건 행 수 반환 |
| `long long` | `self->getDataSum(DBClient* self, const char* table, const char* field, const char* cond)` | 합계 반환 |
| `long long` | `self->getTableSize(DBClient* self, const char* table)` | 테이블 크기 반환 |
| `int` | `self->table_exists(DBClient* self, const char* table)` | 테이블 존재 여부 |
| `int` | `self->dropTable(DBClient* self, const char* table_name)` | 테이블 삭제 |
| `ArrayList*` | `self->descTable(DBClient* self, const char* table)` | 테이블 스키마 조회 |

---

## 📦 crypto — `crypto.h / crypto.c`

```
Object
├── Hasher
└── Cipher
```

암호화 모듈. Hasher (단방향) + Cipher (양방향) + Base64. OpenSSL EVP 래핑.

> `Base64_encode()` / `Base64_decode()` 반환값은 사용 후 `free()` 필수.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `[OWNED] Hasher*` | `new_Hasher(const char* algo)` | 해셔 생성 (SHA-256, SHA-512 등) |
| `[OWNED] String*` | `hasher->hash(Hasher* self, const char* plain_text)` | 해시 생성 |
| `bool` | `hasher->verify(Hasher* self, const char* plain, const char* hashed)` | 해시 검증 |
| `[OWNED] Cipher*` | `new_Cipher(const char* algo)` | 암호 생성 (AES-256-CBC 등) |
| `bool` | `cipher->init(Cipher* self, const uint8_t* key, size_t klen, const uint8_t* iv, size_t ilen)` | 키/IV 초기화 |
| `[OWNED] ByteBuffer*` | `cipher->encrypt(Cipher* self, const uint8_t* data, size_t len)` | 암호화 |
| `[OWNED] ByteBuffer*` | `cipher->decrypt(Cipher* self, const uint8_t* data, size_t len)` | 복호화 |
| `char*` | `Base64_encode(const uint8_t* data, size_t len)` | Base64 인코딩 (OpenSSL) — 사용 후 `free()` |
| `uint8_t*` | `Base64_decode(const char* base64_str, size_t* out_len)` | Base64 디코딩 (OpenSSL) — 사용 후 `free()` |
| `void` | `Crypto_SHA1(const uint8_t* data, size_t len, uint8_t out_hash[20])` | SHA-1 해시 (외부 의존성 없음) |
| `char*` | `Crypto_Base64Encode(const uint8_t* data, size_t len)` | Base64 인코딩 (순수 C) — 사용 후 `free()` |

---

## 📦 ws_protocol — `ws_protocol.h / ws_protocol.c`

```
(정적 함수 모음 — 인스턴스 없음)
ws_protocol
```

WebSocket 프로토콜 처리. 핸드셰이크, 프레임 인코딩/디코딩.

> `ws_compute_accept_key()` 반환값은 사용 후 `free()` 필수.

| 반환 타입 | 함수 원형 | 설명 |
|---|---|---|
| `char*` | `ws_compute_accept_key(const char* client_key)` | WebSocket Accept 키 생성 — 사용 후 `free()` |
| `size_t` | `ws_build_text_frame(const char* msg, uint8_t* out_buf, size_t max_len)` | 텍스트 프레임 생성 (서버→클라이언트) |
| `ssize_t` | `ws_decode_frame(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out)` | 클라이언트 프레임 해독 + 마스킹 해제 |

---

**총 43개 모듈 | Valgrind 0 bytes | TSan 0 warnings | MIT License | 철컥. 🔫**
