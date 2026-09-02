# libcore v1.7.2 — API Reference

> 투스IT 홀딩스 | 2026.08.21 | Linux(epoll/io_uring) + macOS(kqueue)  
> "Java 함수명 + Python self + C 성능 + ARC 안전성"

---

## 목차

| # | 카테고리 | # | 카테고리 |
|---|----------|---|----------|
| 0 | [ARC 핵심 매크로](#0-arc-핵심-매크로) | 16 | [HttpClient](#16-httpclient) |
| 1 | [Object 공통](#1-object-공통) | 17 | [Multipart](#17-multipart) |
| 2 | [Primitive 래퍼](#2-primitive-래퍼) | 18 | [WebSocket 프로토콜](#18-websocket-프로토콜) |
| 3 | [String](#3-string) | 19 | [동시성 Thread / ThreadPool / Semaphore](#19-동시성) |
| 4 | [StringBuilder](#4-stringbuilder) | 20 | [스케줄러 / 타이머](#20-스케줄러--타이머) |
| 5 | [ArrayList](#5-arraylist) | 21 | [파일 시스템](#21-파일-시스템) |
| 6 | [HashMap](#6-hashmap) | 22 | [로깅 / 예외](#22-로깅--예외) |
| 7 | [Hashtable](#7-hashtable) | 23 | [암호화](#23-암호화) |
| 8 | [LinkedList / List / Queue / Stack / Vector](#8-linkedlist--list--queue--stack--vector) | 24 | [데이터베이스](#24-데이터베이스) |
| 9 | [Tree / BTree / RingBuffer / ByteBuffer](#9-tree--btree--ringbuffer--bytebuffer) | 25 | [유틸리티](#25-유틸리티) |
| 10 | [JSON](#10-json) | 26 | [의존성 주입 / AppContext](#26-의존성-주입--appcontext) |
| 11 | [네트워크 / Socket](#11-네트워크--socket) | 27 | [CoreSnmp](#27-coresnmp) |
| 12 | [TransportFactory](#12-transportfactory) | 28 | [SharedMemory](#28-sharedmemory) |
| 13 | [EventLoop](#13-eventloop) | 29 | [BoardHandler ⭐ v1.7.2](#29-boardhandler-v172-신규) |
| 14 | [HttpServer / Router](#14-httpserver--router) | 30 | [TemplateEngine ⭐ v1.7.2](#30-templateengine-v172-신규) |
| 15 | [HttpRequest / HttpResponse](#15-httprequest--httpresponse) | | |

---

## 0. ARC 핵심 매크로

```c
RETAIN(obj)         // 소유권 획득 (ref_count++)  — NULL 안전
RELEASE(obj)        // 소유권 반납 (ref_count-- → 0이면 finalize+free)
RELEASE_NULL(obj)   // RELEASE 후 포인터 NULL 처리 (댕글링 방지)
```

**소유권 원칙:**

| 표기 | 의미 |
|------|------|
| `[OWNED]` | 호출자 소유. 사용 후 `RELEASE()` 필수 |
| `[BORROWED]` | 소유권 없음. `RELEASE()` 금지 |
| `new_*()` 반환값 | 기본 `[OWNED]` |
| 컬렉션 `add(item)` | 컬렉션이 RETAIN → 호출자는 add 후 RELEASE 가능 |

```c
// 올바른 패턴
ArrayList* list = new_ArrayList(10);      // ref=1
String* s = new_String("hello");          // ref=1
list->add(list, (Object*)s);              // ref=2
RELEASE((Object*)s);                      // ref=1 (list가 소유)
RELEASE((Object*)list);                   // list finalize → s ref=0 → free
```

---

## 1. Object 공통

```c
void    Object_Init(Object* obj, const Class* type);
bool    instanceOf(Object* obj, const Class* targetType);
char*   safe_strdup(const char* src, size_t max_len);       // [OWNED] free() 필요
```

**Class 구조:**
```c
struct Class {
    const char* name;
    size_t      size;
    void (*toString)(Object*, char*, size_t);
    bool (*equals)(Object*, Object*);
    int  (*hashCode)(Object*);
    void (*finalize)(Object*);
};
```

---

## 2. Primitive 래퍼

```c
Integer* new_Integer(int value);          // [OWNED] — INT(v) 매크로
Long*    new_Long(long long value);       // [OWNED] — LONG(v) 매크로
Double*  new_Double(double value);        // [OWNED] — DOUBLE(v) 매크로
Boolean* new_Boolean(bool value);        // [OWNED] — BOOL(v) 매크로
Byte*    new_Byte(uint8_t value);         // [OWNED] — BYTE(v) 매크로
```

---

## 3. String

```c
String* new_String(const char* init_str);            // [OWNED]
String* new_StringN(const char* str, size_t len);   // [OWNED] NUL 중간 허용
char*   string_join(const char* delim, const char** arr, int cnt); // [OWNED] free() 필요
```

**메서드 (self→method(self, ...)):**

| 반환형 | 메서드 | 설명 |
|--------|--------|------|
| `int` | `self->length_f(self)` | 문자 수 |
| `char` | `self->charAt(self, int index)` | 인덱스 위치 문자 |
| `bool` | `self->equals(self, const char* s)` | 문자열 비교 |
| `int` | `self->indexOf(self, const char* s)` | 부분문자열 검색 |
| `String*` | `self->substring(self, int begin, int end)` | **[OWNED]** |
| `String*` | `self->concat(self, const char* s)` | **[OWNED]** |
| `String*` | `self->trim(self)` | **[OWNED]** 공백 제거 |
| `void` | `self->append(self, const char* s)` | 뒤에 추가 (가변) |
| `void` | `self->toUpperCase(self)` | 대문자 변환 |
| `void` | `self->toLowerCase(self)` | 소문자 변환 |
| `int` | `self->toInt(self)` | 정수 변환 |
| `long long` | `self->toLong(self)` | 64비트 정수 변환 |
| `double` | `self->toDouble(self)` | 실수 변환 |
| `const char*` | `self->c_str(self)` | **[BORROWED]** C 포인터 |
| `String*` | `self->reverse(self)` | **[OWNED]** |
| `ArrayList*` | `self->split(self, const char* delim)` | **[OWNED]** |
| `bool` | `self->matches(self, const char* pattern)` | POSIX 정규식 |
| `bool` | `self->isEmpty(self)` | 빈 문자열 여부 |

---

## 4. StringBuilder

```c
StringBuilder* new_StringBuilder(size_t initial_capacity);  // [OWNED]
```

| 반환형 | 메서드 | 설명 |
|--------|--------|------|
| `StringBuilder*` | `self->append(self, const char* str)` | 문자열 추가 |
| `StringBuilder*` | `self->appendString(self, String* str)` | String* 추가 |
| `StringBuilder*` | `self->appendBytes(self, const void* buf, size_t len)` | 바이너리 추가 |
| `StringBuilder*` | `self->appendChar(self, char ch)` | 단일 문자 추가 |
| `StringBuilder*` | `self->appendInt(self, int value)` | 정수 추가 |
| `StringBuilder*` | `self->appendLong(self, long value)` | long 추가 |
| `StringBuilder*` | `self->appendDouble(self, double value)` | 실수 추가 |
| `StringBuilder*` | `self->appendFormat(self, const char* fmt, ...)` | printf 스타일 |
| `StringBuilder*` | `self->appendLine(self)` | 줄바꿈 추가 |
| `StringBuilder*` | `self->clear(self)` | 내용 초기화 |
| `StringBuilder*` | `self->truncate(self, size_t length)` | 길이 잘라내기 |
| `bool` | `self->reserve(self, size_t capacity)` | 용량 확보 |
| `size_t` | `self->length(self)` | 현재 길이 |
| `bool` | `self->isEmpty(self)` | 빈 여부 |
| `const char*` | `self->c_str(self)` | **[BORROWED]** C 포인터 |
| `String*` | `self->toString(self)` | **[OWNED]** String 반환 |

---

## 5. ArrayList

```c
ArrayList* new_ArrayList(int initial_capacity);  // [OWNED]
```

| 반환형 | 메서드 | 설명 |
|--------|--------|------|
| `void` | `self->add(self, Object* item)` | 끝에 추가 (내부 RETAIN) |
| `Object*` | `self->get(self, int index)` | **[BORROWED]** |
| `void` | `self->remove(self, int index)` | 삭제 (내부 RELEASE) |
| `void` | `self->removeResult(self, int index)` | 삭제 후 **[OWNED]** 반환 |
| `Object*` | `self->detach(self, int index)` | **[OWNED]** 소유권 이전 |
| `int` | `self->getSize(self)` | 원소 수 |
| `void` | `self->clear(self)` | 전체 삭제 |
| `bool` | `self->isEmpty(self)` | 빈 여부 |
| `void` | `self->forEach(self, ArrayListActionFunc action)` | 순회 콜백 |
| `void*` | `self->find(self, void* target, ArrayListCompareFunc cmp)` | **[BORROWED]** |
| `void` | `self->sort(self, ArrayListCompareFunc cmp)` | 정렬 |
| `ArrayListIterator*` | `self->iterator(self)` | **[OWNED]** |
| `bool` | `iter->hasNext(iter)` | 다음 원소 존재 |
| `Object*` | `iter->next(iter)` | **[BORROWED]** |

---

## 6. HashMap

```c
HashMap* new_HashMap(int initial_capacity);  // [OWNED]
```

| 반환형 | 메서드 | 설명 |
|--------|--------|------|
| `void` | `self->put(self, const char* key, Object* value)` | 저장 (내부 RETAIN) |
| `Object*` | `self->get(self, const char* key)` | **[BORROWED]** |
| `bool` | `self->hasKey(self, const char* key)` | 키 존재 여부 |
| `void` | `self->remove(self, const char* key)` | 삭제 |
| `Object*` | `self->detach(self, const char* key)` | **[OWNED]** |
| `void` | `self->clear(self)` | 전체 삭제 |
| `int` | `self->getSize(self)` | 크기 |
| `bool` | `self->isEmpty(self)` | 빈 여부 |
| `void` | `self->forEach(self, action)` | 순회 콜백 |
| `void` | `self->iterate(self, HashMapIterator fn, void* ctx)` | 고속 순회 (Mutex 보장) |
| `ArrayList*` | `self->keys(self)` | **[OWNED]** 키 목록 |
| `ArrayList*` | `self->values(self)` | **[OWNED]** 값 목록 |

**편의 함수:**
```c
void        hashmap_put_str(self, key, const char* value);
void        hashmap_put_int(self, key, int value);
void        hashmap_put_long(self, key, long value);
const char* hashmap_get_str(self, key);   // [BORROWED]
int         hashmap_get_int(self, key);
long        hashmap_get_long(self, key);
```

---

## 7. Hashtable

```c
Hashtable* new_Hashtable(size_t initialCapacity, float loadFactor);  // [OWNED]
```

| 반환형 | 메서드 | 설명 |
|--------|--------|------|
| `Object*` | `self->put(self, Object* key, Object* value)` | **[OWNED]** 기존값 반환 |
| `Object*` | `self->get(self, Object* key)` | **[BORROWED]** |
| `Object*` | `self->remove(self, Object* key)` | **[OWNED]** |
| `bool` | `self->containsKey(self, Object* key)` | |
| `bool` | `self->containsValue(self, Object* value)` | |
| `size_t` | `self->size(self)` | |
| `bool` | `self->isEmpty(self)` | |
| `void` | `self->clear(self)` | |
| `void` | `self->forEach(self, BiConsumer action)` | |
| `HashtableIterator*` | `self->iterator(self)` | **[OWNED]** |
| `ArrayList*` | `self->keys(self)` | **[OWNED]** |
| `ArrayList*` | `self->values(self)` | **[OWNED]** |

---

## 8. LinkedList / List / Queue / Stack / Vector

```c
LinkedList* new_LinkedList(void);              // [OWNED] 단방향 연결 리스트
List*       new_List(void);                    // [OWNED] 양방향 연결 리스트
Queue*      new_Queue(int initial_capacity);   // [OWNED] FIFO
Stack*      new_Stack(int initial_capacity);   // [OWNED] LIFO
Vector*     new_Vector(int initial_capacity);  // [OWNED] 타입 안전 배열
```

**LinkedList:**
```c
void add_node(self, void* data)
void delete_node(self, void* data, int (*compare)(Object*, Object*))
void print_list(self, void (*display)(Object*))
int  get_size(self)   // snake_case
int  getSize(self)    // camelCase (둘 다 존재)
```

**List (양방향):**
```c
void    pushBack(self, Object* data)
void    pushFront(self, Object* data)
Object* popBack(self)                  // [OWNED]
Object* popFront(self)                 // [OWNED]
void    insertAt(self, int index, Object* data)
Object* removeAt(self, int index)      // [OWNED]
Object* get(self, int index)           // [BORROWED]
void    clear(self)
int     getSize(self)
```

**Queue:**
```c
void   enqueue(self, void* data)
void*  dequeue(self)
void*  peek(self)       // [BORROWED]
bool   isEmpty(self)
int    size(self)       // 둘 다 존재
int    getSize(self)    // 둘 다 존재
void   forEach(self, void (*action)(Object*))
ArrayListIterator* iterator(self)  // [OWNED]
```

**Stack:**
```c
void   push(self, void* data)
void*  pop(self)
void*  peek(self)       // [BORROWED]
bool   isEmpty(self)
bool   isFull(self)
int    size(self)
```

**Vector:**
```c
void    push_back(self, Object* item)
Object* at(self, int index)     // [BORROWED]
Object* pop_back(self)          // [OWNED]
int     get_size(self)
// 매크로
VECTOR_FOREACH(vec, Type, var) { /* ... */ }
```

---

## 9. Tree / BTree / RingBuffer / ByteBuffer

```c
Tree*   new_Tree(CompareFunc cmp);   // [OWNED] BST
BTree*  new_BTree(int t);            // [OWNED] B-Tree
RingBuffer* new_RingBuffer(size_t capacity);  // [OWNED] Thread-safe
ByteBuffer* new_ByteBuffer(size_t capacity);  // [OWNED] 최대 16MB
```

**Tree:**
```c
void      insert(self, Object* data)
Object*   search(self, Object* key)          // [BORROWED]
void      remove(self, Object* key)
void      foreach(self, void (*func)(Object*))
TreeIterator* createIterator(self)           // [OWNED]
void      clear(self)
```

**BTree:**
```c
void    insert(self, Object* key, Object* value)
Object* search(self, Object* key)            // [BORROWED]
void    clear(self)
int     getSize(self)
```

**RingBuffer:**
```c
bool   push(self, void* item)
void*  pop(self)
void*  popWait(self, int timeout_ms)         // 블로킹 대기
```

**ByteBuffer:**
```c
int     writeByte(self, uint8_t b)
int     writeInt32(self, int32_t v)
int     write(self, const void* buf, size_t len)
bool    readByte(self, uint8_t* out)
size_t  read(self, void* buf, size_t len)
void    compact(self)
size_t  remaining(self)
ByteBuffer* readSlice(self, size_t len)      // [OWNED]
```

---

## 10. JSON

```c
ParseResult parse_JSON(const char* json_str);  // result.root [OWNED]

JSONNode* new_JSON_Object(void);   // [OWNED]
JSONNode* new_JSON_Array(void);    // [OWNED]
JSONNode* new_JSON_String(const char* s);  // [OWNED]

JsonValue* new_json_string(const char* s); // [OWNED]
JsonValue* new_json_number(double d);      // [OWNED]
JsonValue* new_json_bool(int b);           // [OWNED]
JsonValue* new_json_null(void);            // [OWNED]
```

**Object 노드 메서드:**
```c
void        node->put(self, const char* key, Object* val)
Object*     node->get(self, const char* key)          // [BORROWED]
const char* node->getString(self, const char* key)    // [BORROWED]
int         node->getInt(self, const char* key)
```

**Array 노드 메서드:**
```c
void    node->add(self, Object* val)
Object* node->getIndex(self, int index)    // [BORROWED]
int     node->length(self)
```

```c
char* node->toString(self)   // [OWNED] free() 필요
```

---

## 11. 네트워크 / Socket

```c
// 비동기 (EventLoop 연동)
Socket* createServer(const char* url, Exception** err);      // [OWNED]
Socket* createClient(const char* url, Exception** err);      // [OWNED]
Socket* createUnixServer(const char* path, Exception** err); // [OWNED]
Socket* createUnixClient(const char* path, Exception** err); // [OWNED]

// 동기 (ThreadPool 워커용)
Socket* createSyncServer(const char* url, Exception** err);  // [OWNED]
Socket* createSyncClient(const char* url, Exception** err);  // [OWNED]

// URL 형식: "tcp://host:port" / "udp://host:port" / "https://host:port"

// TCP
TcpSocket* new_TcpServer(const char* host, int port);        // [OWNED]
TcpSocket* new_TcpClient(const char* host, int port);        // [OWNED]
TcpSocket* new_TcpSocket_from_fd(int fd);                    // [OWNED]
TcpSocket* self->accept(self, char* ip, int* port);          // [OWNED]

// UDP
UdpSocket* new_UdpServer(const char* host, int port);        // [OWNED]
UdpSocket* new_UdpClient(void);                              // [OWNED]

// Unix Domain
UnixSocket* new_UnixServer(const char* path);                // [OWNED]
UnixSocket* new_UnixClient(const char* path);                // [OWNED]

// SSL/TLS
SslSocket* new_SslClient(const char* host, int port);        // [OWNED]
SslSocket* new_SslServer(host, port, cert, key);             // [OWNED]
SslSocket* SslSocket_accept(SslSocket* server);              // [OWNED]
```

**Socket 공통 VTable:**
```c
ssize_t send(self, const void* buf, size_t len, const char* host, int port)
ssize_t recv(self, void* buf, size_t len, char* host, int* port)
int     getFD(self)
void    close(self)
int     bind(self, const char* host, int port)
int     listen(self, int backlog)
int     connect(self, const char* host, int port)

// 콜백
void (*on_readable)(Socket* self, void* loop_ptr)
void (*on_writable)(Socket* self, void* loop_ptr)
void (*on_error)(Socket* self, void* loop_ptr)
```

---

## 12. TransportFactory

```c
UrlInfo* UrlInfo_parse(const char* url_str);  // [OWNED]
// UrlInfo: .scheme / .host / .port / .path / .query

Socket* TransportFactory_createClient(UrlInfo* info, Exception** err);              // [OWNED]
Socket* TransportFactory_createServer(UrlInfo* info, cert, key, Exception** err);  // [OWNED]
```

---

## 13. EventLoop

```c
EventLoop* event_loop_create(void);          // [OWNED] — epoll/io_uring/kqueue 자동 선택
int        event_loop_run(EventLoop* loop);
void       event_loop_stop(EventLoop* loop);
void       event_loop_destroy(EventLoop* loop);
```

**VTable:**
```c
int  self->addSocket(self, Socket* sock, uint32_t mask)  // EV_READ | EV_WRITE
int  self->delSocket(self, Socket* sock)
int  self->addTimer(self, Timer* timer)
int  self->removeTimer(self, Timer* timer)
void self->poll(self, int timeout_ms)
void self->stop(self)
void self->deferRelease(self, Object* obj)  // 콜백 문맥 안전 지연 해제
```

---

## 14. HttpServer / Router

```c
HttpServer* new_HttpServer(EventLoop* loop, Router* router);  // [OWNED]
int  self->listen(self, int port)
void self->stop(self)

// WebSocket 콜백
void (*on_ws_open)   (HttpConnection* conn)
void (*on_ws_message)(HttpConnection* conn, const char* msg, size_t len)
void (*on_ws_close)  (HttpConnection* conn)

// WebSocket 전송
int  HttpConnection_ws_send(HttpConnection* conn, const char* msg)
void HttpConnection_ws_close(HttpConnection* conn)
```

```c
Router* new_Router(void* user_ctx);  // [OWNED]

void self->GET(self, const char* path, HttpHandler fn)
void self->POST(self, const char* path, HttpHandler fn)
void self->PUT(self, const char* path, HttpHandler fn)
void self->DELETE(self, const char* path, HttpHandler fn)
void self->addRoute(self, HttpMethod method, const char* path, HttpHandler fn)
void self->dispatch(self, HttpRequest* req, HttpResponse* res)

// typedef
typedef void (*HttpHandler)(HttpRequest* req, HttpResponse* res, void* user_ctx);

// 에러 응답 (512B 패딩 포함)
void Router_sendError(HttpResponse* res, int status, const char* error_code, const char* message)
```

---

## 15. HttpRequest / HttpResponse

**HttpRequest:**

| 필드 | 타입 | 소유권 | 설명 |
|------|------|--------|------|
| `req->method` | `HttpMethod` | N/A | HTTP_GET/POST/PUT/DELETE/... |
| `req->path` | `String*` | [BORROWED] | 요청 경로 |
| `req->headers` | `HashMap*` | [BORROWED] | 요청 헤더 |
| `req->query` | `HashMap*` | [BORROWED] | 쿼리 파라미터 (?key=val) |
| `req->params` | `HashMap*` | [BORROWED] | 동적 경로 파라미터 (:id 등) — v1.7.1 |
| `req->json` | `JSONNode*` | [BORROWED] | JSON body 파싱 결과 |
| `req->form` | `HashMap*` | [BORROWED] | form-urlencoded |
| `req->multipart` | `MultipartResult*` | [BORROWED] | multipart/form-data |

```c
HttpRequest* new_HttpRequest(void);  // [OWNED]
```

**HttpResponse VTable:**
```c
void res->setStatus(self, int code)
void res->sendStatus(self, int code)
void res->sendText(self, const char* text)
void res->sendJson(self, JSONNode* json)
void res->sendFile(self, const char* path)
void res->setHeader(self, const char* key, const char* val)
void res->redirect(self, const char* url)

const char* Http_statusMessage(int code)   // [BORROWED] "200" → "OK"
```

---

## 16. HttpClient

```c
HttpClient* new_HttpClient(EventLoop* loop);  // [OWNED]
// loop=NULL → 동기 블로킹 모드 (ThreadPool 워커 내부 사용)
```

| 반환형 | 메서드 | 설명 |
|--------|--------|------|
| `HttpClientResponse*` | `self->GET(self, url, HashMap* query)` | **[OWNED]** |
| `HttpClientResponse*` | `self->POST(self, url, HashMap* data, PayloadType type)` | **[OWNED]** |
| `HttpClientResponse*` | `self->PUT(self, url, HashMap* data, PayloadType type)` | **[OWNED]** |
| `HttpClientResponse*` | `self->DELETE(self, url)` | **[OWNED]** |
| `HttpClientResponse*` | `self->POST_RAW(self, url, body, len, content_type)` | **[OWNED]** |
| `HttpClientResponse*` | `self->POST_MULTIPART(self, url, data, files)` | **[OWNED]** |
| `HttpClientResponse*` | `self->execute(self, HttpClientRequest* req)` | **[OWNED]** |
| `void` | `self->setHeader(self, key, val)` | 기본 헤더 설정 |
| `void` | `self->setBearerToken(self, token)` | Bearer 토큰 |

```
PayloadType: PAYLOAD_FORM / PAYLOAD_JSON / PAYLOAD_RAW / PAYLOAD_MULTIPART
HttpClientResponse: .status_code / .headers / .cookies / .body / .body_len
```

---

## 17. Multipart

```c
int              Multipart_extract_boundary(const char* ct_header, char* out, size_t out_size)
MultipartResult* Multipart_parse(const void* body, size_t len, const char* boundary)  // [OWNED]
const char*      MultipartResult_get_field(const MultipartResult* self, const char* name) // [BORROWED]
HttpMultipartFile* MultipartResult_get_file(const MultipartResult* self, const char* name) // [BORROWED]

void    generate_multipart_boundary(char* buf, size_t size)
ssize_t MultipartWriter_calculate_length(data, files, boundary)
int     MultipartWriter_stream_send(data, files, boundary, transport)
```

---

## 18. WebSocket 프로토콜

```c
char*   ws_compute_accept_key(const char* client_key)                  // [OWNED] free() 필요
size_t  ws_build_text_frame(const char* msg, uint8_t* out_buf, size_t max_len)
ssize_t ws_decode_frame(in_buf, in_len, out_msg, max_out)
ssize_t ws_decode_frame2(in_buf, in_len, out_msg, max_out, consumed, is_ping)
// 반환: >0=페이로드 / 0=데이터부족 / -1=Close / -2=프로토콜위반
```

---

## 19. 동시성

```c
// Thread
Thread* new_Thread(Runnable run_func, void* arg);  // [OWNED]
void    self->start(self)
void*   self->join(self)
void    self->detach(self)

// ThreadPool
ThreadPool* new_ThreadPool(int num_threads, int max_resources);  // [OWNED]
void  self->submit(self, TaskRoutine func, void* arg)
void* self->parallel_for(self, ArrayList* list, Consumer action)
void  self->shutdown(self)
int   self->getPendingCount(self)

// Semaphore
Semaphore* new_Semaphore(int initial_value);  // [OWNED]
void self->wait(self)      // P 연산 (블로킹)
void self->post(self)      // V 연산
bool self->tryWait(self)   // 비차단 획득
int  self->getValue(self)
```

---

## 20. 스케줄러 / 타이머

```c
// Timer
Timer* new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* user_data); // [OWNED]
Timer* new_TimerNamed(const char* name, long ms, bool repeating, cb, ud);              // [OWNED]
bool   self->start(self)
void   self->stop(self)
void   self->reset(self)
bool   self->isActive(self)

// Scheduler
Scheduler* new_Scheduler(ThreadPool* pool, EventLoop* loop);  // [OWNED]
bool self->add(self, name, ms, repeat, cb, ud)
bool self->addEx(self, name, ms, repeat, priority, cb, ud)
// JobPriority: JOB_PRIO_LOW / NORMAL / HIGH / URGENT
bool self->remove(self, const char* name)
void self->start(self)
void self->stop(self)

// CronScheduler
CronScheduler* new_CronScheduler(void);  // [OWNED]
bool self->addCron(self, name, const char* expr, cb, ud)
// expr 형식: "분 시 일 월 요일"
bool self->removeCron(self, const char* name)
void self->start(self)
void self->stop(self)
```

---

## 21. 파일 시스템

```c
// File
File* new_File(const char* pathStr);       // [OWNED]
bool    self->exists(self)
int64_t self->length(self)
bool    self->isFile(self)
bool    self->isReadable(self)
bool    self->isWritable(self)
String*     self->readAllText(self)        // [OWNED]
ByteBuffer* self->readAllBytes(self)       // [OWNED]
ArrayList*  self->readLines(self)          // [OWNED]
bool        self->writeString(self, String* content)
bool        self->appendString(self, String* content)
bool        self->copyTo(self, Path* destPath)
bool        self->deleteFile(self)
bool        self->renameAtomic(self, Path* newPath)
bool        self->fsync(self)
bool        self->lockExclusive(self)
void        self->unlock(self)
String*     self->md5(self)                // [OWNED]
String*     self->sha256(self)             // [OWNED]
String*     self->guessMimeType(self)      // [OWNED]

// AsyncFile — Thread+RingBuffer 방식 (io_uring 아님)
AsyncFile* new_AsyncFile(const char* path, bool rotate_daily);  // [OWNED]
int  self->writeAsync(self, const char* data, size_t len)
void self->flush(self)
void self->start(self)
void self->stop(self)

// MappedFile
MappedFile* new_MappedFile(const char* pathStr);  // [OWNED]
bool        self->map(self, bool readOnly)
void        self->unmap(self)
bool        self->sync(self)
ByteBuffer* self->asByteBuffer(self)              // [OWNED]

// Path
Path* new_Path(const char* pathStr);  // [OWNED]
String* self->getFileName(self)       // [OWNED]
String* self->getExtension(self)      // [OWNED]
String* self->getParent(self)         // [OWNED]
Path*   self->getCanonicalPath(self)  // [OWNED]
Path*   self->resolve(self, const char* child)  // [OWNED]

// PathValidator — Stateless, 멀티스레드 공유 안전
PathValidator* new_PathValidator(void);  // [OWNED]
bool self->validate(self, raw_path, out_canonical, out_size)
// out_size: MAX_PATH_LEN+1 이상 권장

// Directory
Directory* new_Directory(const char* pathStr);  // [OWNED]
bool       self->exists(self)
bool       self->mkdirs(self)
ArrayList* self->listFiles(self)                // [OWNED] ArrayList<Path*>
ArrayList* self->walkTree(self)                 // [OWNED] ArrayList<Path*> 재귀 전체
bool       self->deleteRecursive(self)

// FileWatcher
FileWatcher* new_FileWatcher(void);  // [OWNED]
bool self->watch(self, Path* target)
void self->onEvent(self, EventCallback cb)
void self->poll(self)
void self->stop(self)

// FileUtil
File* FileUtil_tmp(void)                                       // [OWNED]
File* FileUtil_home(void)                                      // [OWNED]
File* FileUtil_cwd(void)                                       // [OWNED]
bool  FileUtil_exists(const char* path)
bool  FileUtil_mkdirs(const char* path)
void  FileUtil_delete(const char* path)
File* FileUtil_createTemp(const char* dir, const char* prefix) // [OWNED]
```

---

## 22. 로깅 / 예외

```c
// Logger
Logger* new_Logger(int level);  // [OWNED]
extern Logger* logger;          // 전역 인스턴스

void self->setLogFile(self, const char* path)
void self->setLevel(self, int level)
void self->addAppender(self, LogAppenderCallback, void* data)

// 매크로 (파일/라인 자동)
LOG_DEBUG(logger, fmt, ...)
LOG_INFO(logger, fmt, ...)
LOG_WARN(logger, fmt, ...)
LOG_ERROR(logger, fmt, ...)
// LOG_LEVEL_DEBUG=1 / INFO=2 / WARN=3 / ERROR=4

// AsyncLogger
AsyncLogger* new_AsyncLogger(int level);  // [OWNED]
void self->start(self)
void self->stop(self)
ALOG_DEBUG/INFO/WARN/ERROR(logger, fmt, ...)

// Exception
Exception* new_Exception(code, sys_errno, msg, cause, file, line);  // [OWNED]
throw_Exception(code, sys_err, msg)
throw_ExceptionCause(code, sys_err, msg, cause)

// 주요 ErrorCode
// OK=0 / ERR_NULL=1 / ERR_OOM=2 / ERR_IO=3
// ERR_SOCK_CREATE=2001 / ERR_SOCK_BIND=2002 / ERR_SOCK_CONNECT=2004
// ERR_SOCK_TIMEOUT=2005 / ERR_SOCK_REFUSED=2006
```

---

## 23. 암호화

```c
// 단방향 해시
Hasher* new_Hasher(const char* algo);   // [OWNED] "SHA-256" / "SHA-512" / "BCRYPT"
String* self->hash(self, const char* plain_text)    // [OWNED]
bool    self->verify(self, const char* plain, const char* hashed)

// 대칭키 암복호화
Cipher* new_Cipher(const char* algo);   // [OWNED] "AES-256-CBC" / "AES-256-GCM"
bool        self->init(self, key, key_len, iv, iv_len)
ByteBuffer* self->encrypt(self, data, len)   // [OWNED]
ByteBuffer* self->decrypt(self, data, len)   // [OWNED]

// Base64
char*    Base64_encode(const uint8_t* data, size_t len)      // [OWNED] free() 필요
uint8_t* Base64_decode(const char* base64_str, size_t* out)  // [OWNED] free() 필요

// Pure C (외부 의존 없음)
void  Crypto_SHA1(const uint8_t* data, size_t len, uint8_t out_hash[20])
char* Crypto_Base64Encode(const uint8_t* data, size_t len)   // [OWNED] free() 필요
```

---

## 24. 데이터베이스

```c
DBClient* new_DBClient(void);  // [OWNED] dbconfig.conf 사용
DBClient* new_DBClientDirect(host, dbname, id, pw, port, charset, type);  // [OWNED]
// type: "MYSQL" | "PGSQL"

int  self->connect(self)
void self->disconnect(self)
int  self->reconnect(self)
int  self->sqlQuery(self, const char* sql)
char* self->escape_string(self, const char* str)  // [OWNED] free() 필요

int  self->beginTransaction(self)
int  self->commit(self)
int  self->rollback(self)
int  self->setAutoCommit(self, bool c)

HashMap* self->validateFields(self, table, HashMap* raw_data)  // [OWNED]
int      self->table_exists(self, const char* table)
int      self->fieldExists(self, table, field)
long long self->getNextIdx(self, const char* table)

int self->insertTable(self, table, HashMap* data)   // last_insert_id 업데이트
int self->updateTable(self, table, HashMap* data, const char* cond)
int self->replaceTable(self, table, HashMap* data)
int self->deleteTable(self, table, const char* cond)
int self->all_delete_table(self, const char* table)

HashMap*   self->getRecord(self, table, cond, field)        // [OWNED]
ArrayList* self->getRecords(self, table, cond, fields)      // [OWNED]
HashMap*   self->getRecordFromQuery(self, const char* sql)  // [OWNED]
ArrayList* self->getRecordsFromQuery(self, const char* sql) // [OWNED]

int       self->getDataCount(self, table, cond)
long long self->getDataSum(self, table, field, cond)
long long self->getDataMax(self, table, field, cond)
long long self->getDataMin(self, table, field, cond)

int       self->copyTable(self, newT, orgT, int copyData)
char*     self->makeTable(self, const char* tablename)   // [OWNED] free() 필요
int       self->renameTable(self, old_table, new_table)
ArrayList* self->descTable(self, const char* table)      // [OWNED]
long long  self->getTableSize(self, const char* table)
int        self->dropTable(self, const char* table_name)
int        self->initTable(self, const char* table_name)

long long self->last_insert_id;   // INSERT 직후 자동 생성 ID
long long self->last_idx;
```

---

## 25. 유틸리티

```c
// DateTime
DateTime* new_DateTime_now(void);                                          // [OWNED]
DateTime* new_DateTime_now_utc(void);                                      // [OWNED]
DateTime* new_DateTime_from_timestamp(time_t ts, bool is_utc);            // [OWNED]
DateTime* new_DateTime_parse(const char* str, const char* fmt, int* err); // [OWNED]

int    self->getYear(self)   self->getMonth(self)   self->getDay(self)
int    self->getHour(self)   self->getMinute(self)  self->getSecond(self)
DateTime* self->addDays(self, int days)    // [OWNED]
long   self->diffSeconds(self, DateTime* other)
bool   self->isLeapYear(self)
String* self->toRFC1123(self)              // [OWNED] HTTP Date 헤더용

int    datetime_time(void)                 // Unix 타임스탬프 (초)
double datetime_microtime(void)            // 마이크로초 포함

// Locale
Locale* new_Locale(const char* lang, const char* country);  // [OWNED]
const char* self->getLanguage(self)    // [BORROWED]
const char* self->getCountry(self)     // [BORROWED]
Locale*     self->getFallback(self)    // [OWNED] ko_KR→ko→en_US
bool        self->isRTL(self)

int     utf8_strlen(const char* str)
String* utf8_substr(const char* str, int start, int len)   // [OWNED]

// Regex
Regex* new_Regex(const char* pattern, int flags, int* err);  // [OWNED]
// flags: REG_EXTENDED / REG_ICASE / REG_NEWLINE
bool       self->matches(self, const char* str)
int        self->search(self, const char* str)
ArrayList* self->findAll(self, const char* text)   // [OWNED]
int        self->matchCount(self, const char* text)
const char* self->getPattern(self)                 // [BORROWED]

// TextEncoder
TextEncoder* new_TextEncoder(void);  // [OWNED] 단일 인스턴스 공유 권장

// Allocation API ([OWNED] RELEASE 필요)
String*     self->escapeHtml(self, const char* input)
String*     self->urlEncode(self, const char* input)
String*     self->urlDecode(self, const char* input)
String*     self->base64Encode(self, const void* data, size_t len)
ByteBuffer* self->base64Decode(self, const char* input)

// Zero-Allocation API
bool self->escapeHtmlTo(self, StringBuilder* out, const char* input)
bool self->urlEncodeTo(self, StringBuilder* out, const char* input)
bool self->base64EncodeTo(self, StringBuilder* out, const void* data, size_t len)
bool self->base64DecodeTo(self, ByteBuffer* out, const char* input)

// TimeUtils
uint64_t now_ms(void)            // Unix 타임스탬프 (밀리초)
uint64_t now_monotonic_ms(void)  // 단조 시계 (밀리초)
```

---

## 26. 의존성 주입 / AppContext

```c
// Config
Config* new_Config(void);                    // [OWNED]
bool        self->load(self, const char* path)
const char* self->get(self, const char* key)             // [BORROWED]
int         self->getInt(self, const char* key, int def)
bool        self->getBool(self, const char* key, bool def)
const char* self->getString(self, key, const char* def)  // [BORROWED]

// Context
Context* new_Context(void);  // [OWNED] Thread-safe 키-값 저장소
void    self->set(self, const char* key, Object* value)
Object* self->get(self, const char* key)    // [BORROWED]
bool    self->has(self, const char* key)
void    self->setString(self, key, const char* val)
String* self->getString(self, key)          // [BORROWED]
void    self->setInt(self, key, int val)
int     self->getInt(self, key)

// AppContext
AppContext* new_AppContext(void);  // [OWNED]
extern AppContext* g_app;          // 전역 사령관

bool    self->init(self, const char* config_path)
void    self->registerService(self, const Class* cls, Object* svc)
Object* self->getService(self, const Class* cls)         // [BORROWED]

// ServiceRegistry
ServiceRegistry* new_ServiceRegistry(void);  // [OWNED]
REG_GET(reg, TYPE)  // 타입 안전 서비스 조회 매크로
```

---

## 27. CoreSnmp

> ⚠️ **스레드별 독립 인스턴스 생성 필수** (공유 시 Race Condition)

```c
CoreSnmp* new_Snmp(SnmpTransport transport, const char* version_str, const char* community);  // [OWNED]
// version_str: "v1" | "v2c"

CoreSnmp* new_SnmpV3(transport, username, sec_level,
    auth_proto, auth_key, auth_key_len,
    priv_proto, priv_key, priv_key_len);  // [OWNED]

SnmpTrap* new_SnmpTrap(void);  // [OWNED]

ErrorCode self->startListen(self, int port)
void      self->stopListen(self)
void      self->setTrapPort(self, int port)
void      self->setAgentPort(self, int port)

ErrorCode self->sendGet(self, ip, oid, out, sz, out_len)
ErrorCode self->sendGetNext(self, ip, oid, out, sz, out_len)
ErrorCode self->sendGetBulk(self, ip, oid, non_rep, max_rep, ArrayList* out_varbinds)
ErrorCode self->snmpWalk(self, ip, root_oid, ArrayList* out_all)
ErrorCode self->sendSet(self, ip, oid, value)
ErrorCode self->sendTrap(self, ip, oid)
ErrorCode self->sendInform(self, ip, oid)
bool      self->setOid(self, const char* oid, const char* desc)
size_t    self->getTrapCount(self)
void      self->resetStats(self)
```

---

## 28. SharedMemory

```c
SharedMemory* new_SharedMemory(const char* name, size_t size, bool create);  // [OWNED]
bool     self->write(self, const void* data, size_t len)
size_t   self->read(self, void* buf, size_t buf_len)
void     self->lock(self)
void     self->unlock(self)
void     self->clear(self)
size_t   self->dataLen(self)
uint32_t self->getWriteCnt(self)
```

---

## 29. BoardHandler (v1.7.2 신규)

게시판 CRUD + 댓글 + 첨부파일 + 스킨/Limit DB 관리.

**소유권 규칙:**
- `BoardHandler` **[OWNED]** — `RELEASE()`로 해제
- `db`, `pv` **[BORROWED]** — `finalize`에서 RELEASE 금지

### 29.1 생성자

```c
BoardHandler* new_BoardHandler(
    DBClient*      db,
    PathValidator* pv,
    const char*    base_dir,       // 절대경로 루트 (app.conf base_dir)
    const char*    upload_dir,     // 업로드 상대경로 (DB 저장 기준)
    const char*    tpl_dir,        // 템플릿 상대경로
    size_t         max_upload_size // 초과 시 HTTP 413 (기본 10MB)
);
// [OWNED]
// 생성자 내부에서 skin DB 조회 + skinDir 초기화
```

**주요 필드:**
```c
db              [BORROWED] DBClient*      — MariaDB/PostgreSQL 연결
pv              [BORROWED] PathValidator* — 경로 보안 검증기
base_dir        char[256]  — 절대경로 루트
upload_dir      char[256]  — 업로드 상대경로
tpl_dir         char[256]  — 템플릿 상대경로
skin            char[32]   — 현재 스킨명 (white | dark)
skinDir         char[512]  — 스킨 절대경로
max_upload_size size_t     — 최대 업로드 바이트
```

### 29.2 HTTP 핸들러 (VTable)

| 메서드 | HTTP | 경로 | 설명 |
|--------|------|------|------|
| `self->read_write` | GET | `/board/write` | 글쓰기 폼 SSR |
| `self->write` | POST | `/board/write` | 글 저장 + 첨부 업로드 (multipart) |
| `self->list` | GET | `/board/list` | 목록 SSR (페이지네이션+검색+NEW배지+댓글수) |
| `self->detail` | GET | `/board/:id` | 상세보기 SSR. `?mode=modify` → 수정 폼 |
| `self->modify` | PUT | `/board/:id` | 수정 (기존 첨부 조회→신규 저장→트랜잭션→old삭제) |
| `self->remove` | DELETE | `/board/:id` | soft delete (is_deleted=1) + 파일 삭제 |
| `self->attach` | GET | `/board/attach/:id` | 첨부파일 다운로드 |
| `self->comment_write` | POST | `/board/:id/comment` | 댓글/대댓글 (depth 0/1, 2단 제한) |
| `self->comment_modify` | PUT | `/board/comment/:id` | 댓글 수정 |
| `self->comment_remove` | DELETE | `/board/comment/:id` | 댓글 삭제 (depth=0 삭제 시 자식 cascade) |
| `self->skin_update` | POST | `/board/skin` | 스킨 전환 (DB UPSERT + in-memory skinDir 갱신) |
| `self->limit_update` | POST | `/board/limit` | 페이지당 목록 수 DB 관리 (5/10/15/20/30/40/50) |

### 29.3 파일 유틸 (VTable)

```c
bool self->file_size_exceeded(self, HttpMultipartFile* attach)
// 첨부 크기 > max_upload_size 여부 검사

bool self->file_save(self, HttpMultipartFile* attach, char* out_path, size_t out_size)
// 파일 저장. 저장명: {timestamp}_{microsec}_{seq}.{ext} (원본명 제거)
// 확장자 화이트리스트 자동 검사

bool self->file_delete(self, const char* path)
// 파일 삭제. PathValidator + upload_dir prefix 이중 방어

void self->file_sanitize(self, const char* dirty, char* clean_out, size_t max_len)
// 파일명 정규화 (basename 추출 + PathValidator 검증)
```

### 29.4 라우터 콜백 어댑터

```c
// ctx = BoardHandler*
void board_read_write_cb    (HttpRequest* req, HttpResponse* res, void* ctx);
void board_write_cb         (HttpRequest* req, HttpResponse* res, void* ctx);
void board_list_cb          (HttpRequest* req, HttpResponse* res, void* ctx);
void board_detail_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_modify_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_remove_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_attach_cb        (HttpRequest* req, HttpResponse* res, void* ctx);
void board_comment_write_cb (HttpRequest* req, HttpResponse* res, void* ctx);
void board_comment_modify_cb(HttpRequest* req, HttpResponse* res, void* ctx);
void board_comment_remove_cb(HttpRequest* req, HttpResponse* res, void* ctx);
void board_skin_update_cb   (HttpRequest* req, HttpResponse* res, void* ctx);
void board_limit_update_cb  (HttpRequest* req, HttpResponse* res, void* ctx);
```

### 29.5 Composition Root 패턴

```c
// arc_board_server.c 패턴
Config* cfg = new_Config();
cfg->load(cfg, "examples/board/app.conf");

size_t max_upload_size = (size_t)cfg->getInt(cfg, "max_upload_size", 10485760);

DBClient*      db = new_DBClientDirect(...);
PathValidator* pv = new_PathValidator();
BoardHandler*  bh = new_BoardHandler(db, pv, base_dir, upload_dir, tpl_dir, max_upload_size);

Router* router = new_Router(bh);
router->GET(router, "/board/list",       board_list_cb);
router->GET(router, "/board/write",      board_read_write_cb);
router->POST(router, "/board/write",     board_write_cb);
router->GET(router, "/board/:id",        board_detail_cb);
router->PUT(router, "/board/:id",        board_modify_cb);
router->DELETE(router, "/board/:id",     board_remove_cb);
router->GET(router, "/board/attach/:id", board_attach_cb);
router->POST(router, "/board/:id/comment",    board_comment_write_cb);
router->PUT(router, "/board/comment/:id",     board_comment_modify_cb);
router->DELETE(router, "/board/comment/:id",  board_comment_remove_cb);
router->POST(router, "/board/skin",      board_skin_update_cb);
router->POST(router, "/board/limit",     board_limit_update_cb);

EventLoop*  loop   = event_loop_create();
HttpServer* server = new_HttpServer(loop, router);
server->listen(server, port);
event_loop_run(loop);
```

### 29.6 보안 패치 내역

```
확장자 화이트리스트:
  .jpg .jpeg .png .gif .webp
  .pdf .txt .hwp .doc .docx
  .xls .xlsx .ppt .pptx .zip

PathValidator 샌드박스:
  upload_dir prefix 체크 (file_delete 이중 방어)

파일 업로드 용량 제한:
  max_upload_size (app.conf) → 초과 시 HTTP 413 Payload Too Large

파일 저장명:
  {timestamp}_{microsec}_{seq}.{ext} — 원본명은 DB(file_name)에만 저장

SQL 인젝션 방어:
  escape_string() + LIKE escape (BoardHandler_escape_like)
```

### 29.7 board_config 테이블

```sql
-- 스킨 설정
INSERT INTO board_config (cfg_name, cfg_value) VALUES ('skin', 'white')
  ON DUPLICATE KEY UPDATE cfg_value = 'white';

-- 목록 수 설정
INSERT INTO board_config (cfg_name, cfg_value) VALUES ('board_list', '20')
  ON DUPLICATE KEY UPDATE cfg_value = '20';

-- 허용값
-- skin:       'white' | 'dark'
-- board_list: 5 | 10 | 15 | 20 | 30 | 40 | 50
```

### 29.8 댓글 depth 구조

```
depth=0 : 1단 댓글  (parent_id = NULL)
depth=1 : 2단 답글  (parent_id = 상위 댓글 id)
depth≥2 : 차단      (3단 이상 금지)

삭제 시:
  depth=0 삭제 → 자식 답글도 cascade soft delete
  depth=1 삭제 → 해당 답글만 soft delete
```

---

## 30. TemplateEngine (v1.7.2 신규)

순수 C99 구현 서버사이드 렌더링(SSR) 엔진. JavaScript 런타임 없이 HTML 생성.

### 30.1 API

```c
TemplateEngine* new_TemplateEngine(void);  // [OWNED]

char* self->render(self, const char* template_text, JSONNode* context);
// [OWNED] free() 필요. context=NULL 허용

char* self->renderFile(self, const char* file_path, JSONNode* context);
// [OWNED] free() 필요. context=NULL 허용
```

### 30.2 지원 문법

| 문법 | 유형 | 설명 |
|------|------|------|
| `{{key}}` | 변수 치환 | JSONNode getString(key) 사용 |
| `{{#array}}...{{/array}}` | 배열 루프 | JSON Array 반복 |
| `{{#flag}}...{{/flag}}` | 조건부 렌더링 | 값이 비어있지 않으면 출력 |

### 30.3 사용 예시

```c
TemplateEngine* engine = new_TemplateEngine();

JSONNode* ctx = new_JSON_Object();
JSONNode* j_title = new_JSON_String("투스IT 게시판");
ctx->put(ctx, "title", (Object*)j_title);
RELEASE((Object*)j_title);

// 배열 컨텍스트 구성
JSONNode* posts = new_JSON_Array();
JSONNode* post = new_JSON_Object();
JSONNode* j_id = (JSONNode*)new_json_number(1);
JSONNode* j_t  = new_JSON_String("첫 번째 게시글");
post->put(post, "id", (Object*)j_id);
post->put(post, "title", (Object*)j_t);
RELEASE((Object*)j_id);
RELEASE((Object*)j_t);
posts->add(posts, (Object*)post);
RELEASE((Object*)post);
ctx->put(ctx, "posts", (Object*)posts);
RELEASE((Object*)posts);

char* html = engine->renderFile(engine,
    "templates/skin/white/index.html", ctx);
// ... html 전송 ...
free(html);

RELEASE((Object*)engine);
RELEASE((Object*)ctx);
```

**HTML 템플릿:**
```html
<h1>{{title}}</h1>
{{#posts}}
<tr>
  <td>{{no}}</td>
  <td><a href="/board/{{id}}">{{title}}</a>
      <span class="badge">{{comment_badge}}</span>
      <span class="new">{{new_badge}}</span>
  </td>
  <td>{{nickname}}</td>
  <td>{{view_count}}</td>
  <td>{{created_at}}</td>
</tr>
{{/posts}}

{{#has_prev}}
<a href="/board/list?page={{prev_page}}">이전</a>
{{/has_prev}}

{{#pages}}
<a href="/board/list?page={{num}}" class="{{is_active}}">{{num}}</a>
{{/pages}}

{{#has_next}}
<a href="/board/list?page={{next_page}}">다음</a>
{{/has_next}}
```

### 30.4 스킨 디렉토리 구조

```
templates/
  skin/
    white/
      index.html        ← 게시글 목록
      board_write.html  ← 글쓰기 폼
      board_read.html   ← 상세보기 / 수정폼
    dark/
      index.html
      board_write.html
      board_read.html
```

### 30.5 app.conf 설정

```ini
host       = 0.0.0.0
port       = 8888

db_host    = 127.0.0.1
db_port    = 3306
db_user    = board
db_pass    = your_password
db_name    = board

base_dir   = /your/path/webboard_libcore
upload_dir = examples/board/uploads
tpl_dir    = examples/board/templates
log_file   = logs/board.log

max_upload_size = 10485760   ; 10MB (초과 시 HTTP 413)
```

---

## 기술 스택 요약

| 항목 | 내용 |
|------|------|
| 언어 | C99 |
| 네트워크 | 직접 구현 (kqueue/epoll/io_uring) |
| HTTP | 직접 파싱 |
| 라우터 | libcore Router (2-Pass params 엔진) |
| 템플릿 엔진 | BoardTemplateEngine (C 구현 SSR) |
| DB | MariaDB / PostgreSQL (동일 ORM API) |
| 메모리 관리 | ARC (RETAIN/RELEASE 매크로) |
| 보안 | PathValidator, 확장자 화이트리스트, SQL escape |
| 빌드 | GNU Make |
| 플랫폼 | macOS (kqueue) / Linux (epoll / io_uring) |
| 코드베이스 | 32,000+ 줄 / 경고 0 / Valgrind 0 bytes |

---

*libcore v1.7.2 | 투스IT 홀딩스 | GitHub: https://github.com/toursmurf/libcore*
