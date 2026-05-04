# libcore v1.0 Coding Guide

**Toos IT Holdings | Iron Fortress v1.0 | English Edition**

---

## 1. Before You Start

```
This guide is for developers new to libcore.
It covers ARC memory management, the Object system,
and how to use the core modules.

Requirements:
→ Linux 64-bit (Ubuntu / Rocky / Debian)
→ GCC 9+ / Clang 10+
→ GNU Make
→ Basic C99 knowledge
```

---

## 2. Build

```bash
# Basic build
git clone https://github.com/toursmurf/libcore.git
cd libcore
make examples

# Run first example
./examples/arc_echo_server

# Test from another terminal
nc localhost 8080
hello
# → hello (echo response)

# Build with MySQL (optional)
make WITH_MYSQL=1 \
  MYSQL_INC=$(mysql_config --variable=pkgincludedir) \
  MYSQL_LIB=$(mysql_config --variable=pkglibdir)
```

---

## 3. Source Structure

```
libcore/
├── include/        Header files (.h) — read these first
│   ├── object.h    Object / Class / ARC macros
│   ├── arraylist.h ArrayList / Iterator
│   ├── hashmap.h   HashMap
│   ├── tcp_socket.h TcpSocket
│   ├── event_loop.h EventLoop
│   └── ...         (all 44 modules)
│
├── src/            Implementation files (.c)
├── examples/       Example sources (34 files) ← start here!!
├── docs/           Documentation
└── Makefile
```

> ⭐ **Start from the `examples/` folder**

---

## 4. Design Philosophy

libcore follows four core principles.

**① Object as the first member**
Every struct must have `Object base` as its very first member.

```c
struct ArrayList {
    Object base;    // MUST be first!! The heart of ARC
    int    size;
    int    capacity;
    // ...
};
```

**② new_ClassName() constructor**
All objects are created with `new_ClassName()`. Sets `ref_count = 1` automatically.

```c
ArrayList* list = new_ArrayList(10);  // ref_count = 1
```

**③ finalize destructor**
When `ref_count` reaches 0, `finalize()` is called automatically to release internal resources.

**④ Valgrind 0 bytes**
All modules must pass Valgrind with 0 bytes memory leak. No exceptions.

---

## 5. ARC Memory Management

### 5-1. Three Macros

```c
/* RETAIN — ref_count++ */
RETAIN((Object*)item);

/* RELEASE — ref_count-- / calls finalize + free when 0 */
RELEASE((Object*)item);

/* RELEASE_NULL — RELEASE then set pointer to NULL */
RELEASE_NULL((Object**)&ptr);
```

### 5-2. [OWNED] vs [BORROWED]

| Notation | Meaning | RELEASE |
|---|---|---|
| `[OWNED]` | Ownership transfers to caller | Required |
| `[BORROWED]` | No ownership transfer | Forbidden |

```c
/* [OWNED] — must RELEASE */
ArrayList* list = new_ArrayList(10);   // [OWNED]
String*    str  = new_String("hello"); // [OWNED]
RELEASE((Object*)str);   // required after use!!
RELEASE((Object*)list);

/* [BORROWED] — must NOT RELEASE */
String* item = (String*)list->get(list, 0);  // [BORROWED]
printf("%s\n", item->c_str(item));           // safe to use
// RELEASE(item) ← FORBIDDEN!! will crash!!
```

### 5-3. Basic Pattern

```c
/* ① Create → Use → Release */
ArrayList* list = new_ArrayList(10);   // ref_count = 1
String*    str  = new_String("apple"); // ref_count = 1

list->add(list, (Object*)str);         // auto RETAIN → ref_count = 2
RELEASE((Object*)str);                 // ref_count = 1, list owns it

/* ② Releasing list also releases str automatically */
RELEASE((Object*)list);  // list ref=0 → str released → all gone

/* ③ Returning [OWNED] from a function */
String* make_greeting(const char* name) {
    String* s = new_String("Hello, ");
    s->append(s, name);
    return s;  // [OWNED] — caller is responsible for RELEASE
}

String* greeting = make_greeting("World");
printf("%s\n", greeting->c_str(greeting));
RELEASE((Object*)greeting);  // required!!
```

---

## 6. Object System

```c
/* Creating a new class */
typedef struct MyClass MyClass;
struct MyClass {
    Object base;      // MUST be first!!
    int    value;
    void (*print)(MyClass* self);
};

/* Destructor */
static void myclass_finalize(Object* obj) {
    MyClass* self = (MyClass*)obj;
    // release internal resources here
}

/* Class metadata */
static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),      // required!!
    .finalize = myclass_finalize
};

/* Constructor */
MyClass* new_MyClass(int value) {
    MyClass* self = (MyClass*)calloc(1, sizeof(MyClass));
    if (!self) return NULL;
    Object_Init((Object*)self, &_myClass);  // initialize ARC
    self->value = value;
    self->print = myclass_print;
    return self;
}
```

---

## 7. Core Modules

### 7-1. String

```c
/* Create */
String* s = new_String("hello");

/* Common API */
int         len   = s->length_f(s);
const char* raw   = s->c_str(s);            // [BORROWED]
String*     upper = s->copy(s);             // [OWNED]
upper->toUpperCase(upper);                  // in-place
String*     cat   = s->concat(s, " world"); // [OWNED]
String*     sub   = s->substring(s, 0, 3);  // [OWNED]
bool        eq    = s->equals(s, (Object*)other);

/* Type conversion */
int    n = s->toInt(s);
double d = s->toDouble(s);

/* Regex */
bool      match = s->matches(s, "^hello.*");
ArrayList* parts = s->split(s, ",");  // [OWNED]

/* Release */
RELEASE((Object*)upper);
RELEASE((Object*)cat);
RELEASE((Object*)sub);
RELEASE((Object*)parts);
RELEASE((Object*)s);
```

### 7-2. ArrayList

```c
ArrayList* list = new_ArrayList(10);

/* Add — auto RETAIN internally */
String* item = new_String("apple");
list->add(list, (Object*)item);
RELEASE((Object*)item);  // list owns it now

/* Get — [BORROWED] */
String* got = (String*)list->get(list, 0);
printf("%s\n", got->c_str(got));  // do NOT RELEASE!!

/* Size */
int  sz    = list->getSize(list);
bool empty = list->isEmpty(list);

/* Iterate */
list->forEach(list, my_print_fn);

/* Iterator */
ArrayListIterator* it = list->iterator(list);  // [OWNED]
while (it->hasNext(it)) {
    String* s = (String*)it->next(it);  // [BORROWED]
    printf("%s\n", s->c_str(s));
}
RELEASE((Object*)it);

/* Release — all items auto-released */
RELEASE((Object*)list);
```

### 7-3. HashMap

```c
HashMap* map = new_HashMap(16);

/* Insert — auto RETAIN internally */
String* val = new_String("value1");
map->put(map, "key1", (Object*)val);
RELEASE((Object*)val);  // map owns it

/* Lookup — [BORROWED] */
String* found = (String*)map->get(map, "key1");
if (found) printf("%s\n", found->c_str(found));

/* Check / Remove */
bool exists = map->hasKey(map, "key1");
map->remove(map, "key1");  // auto RELEASE internally

/* Key list — [OWNED] */
ArrayList* keys = map->keys(map);  // must RELEASE
RELEASE((Object*)keys);

RELEASE((Object*)map);
```

### 7-4. Thread / ThreadPool

```c
/* Thread */
void* my_task(void* arg) {
    printf("task running\n");
    return NULL;
}

CoreThread* t = new_CoreThread(my_task, NULL);
t->start(t);
t->join(t);
RELEASE((Object*)t);

/* ThreadPool */
ThreadPool* pool = new_ThreadPool(4);  // 4 workers
pool->submit(pool, my_task, NULL);
pool->submit(pool, my_task, NULL);
pool->shutdown(pool);
RELEASE((Object*)pool);

/* Semaphore */
Semaphore* sem = new_Semaphore(1);
sem->wait(sem);   // P operation — blocking
// ... critical section ...
sem->post(sem);   // V operation
RELEASE((Object*)sem);
```

### 7-5. Socket / EventLoop

```c
/* Create server sockets */
TcpSocket*  tcp  = new_TcpServer("0.0.0.0", 8080);
UdpSocket*  udp  = new_UdpServer("0.0.0.0", 9000);
UnixSocket* unix = new_UnixServer("/tmp/arc.sock");

/* EventLoop */
EventLoop* loop = new_EventLoop(1024);

/* Register callbacks */
tcp->base.on_readable = on_accept;
udp->base.on_readable = on_udp;

/* Add to loop */
loop->addSocket(loop, (Socket*)tcp, EV_READ);
loop->addSocket(loop, (Socket*)udp, EV_READ);

/* Run — blocking */
loop->run(loop);

/* Remove socket */
loop->delSocket(loop, (Socket*)tcp);

/* Stop */
loop->stop(loop);

RELEASE((Object*)tcp);
RELEASE((Object*)udp);
RELEASE((Object*)loop);
```

### 7-6. Logger

```c
Logger* logger = new_Logger(LOG_LEVEL_INFO);

/* File output (optional) */
logger->setLogFile(logger, "/var/log/myapp.log");

/* Log output */
LOG_DEBUG(logger, "debug: %s", "message");
LOG_INFO (logger, "info: %d", 42);
LOG_WARN (logger, "warning: %s", "caution");
LOG_ERROR(logger, "error: %s", "failed");

RELEASE((Object*)logger);
```

---

## 8. TCP Echo Server — Full Example

```c
#include "libcore.h"

/* ③ Receive data from client → echo back */
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

/* ② Accept new connection */
static void on_accept(Socket* self, void* loop_ptr) {
    EventLoop* loop   = (EventLoop*)loop_ptr;
    TcpSocket* client = ((TcpSocket*)self)->accept(
                            (TcpSocket*)self, NULL, NULL);
    if (client) {
        client->base.on_readable = on_client;
        loop->addSocket(loop, (Socket*)client, EV_READ);
        RELEASE((Object*)client);  // hand over to loop
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

## 9. Multi-Protocol Server

```c
/* TCP + UDP + Unix simultaneously — single EventLoop */

int main(void) {
    EventLoop*  loop = new_EventLoop(1024);
    TcpSocket*  tcp  = new_TcpServer("0.0.0.0", 8001);
    UdpSocket*  udp  = new_UdpServer("0.0.0.0", 8002);
    UnixSocket* unix = new_UnixServer("/tmp/arc.sock");

    tcp->base.on_readable  = on_accept;
    udp->base.on_readable  = on_udp;     // UDP handled directly
    unix->base.on_readable = on_accept;

    loop->addSocket(loop, (Socket*)tcp,  EV_READ);
    loop->addSocket(loop, (Socket*)udp,  EV_READ);
    loop->addSocket(loop, (Socket*)unix, EV_READ);

    loop->run(loop);  // single thread handles 3 protocols!!
    /* ... cleanup ... */
    return 0;
}
```

---

## 10. Common Mistakes

### ❌ Mistake 1: RELEASE on [BORROWED] return value

```c
/* Wrong */
String* s = list->get(list, 0);
RELEASE((Object*)s);  // ❌ crash!!

/* Correct */
String* s = list->get(list, 0);
printf("%s\n", s->c_str(s));  // use only. do NOT RELEASE!!
```

### ❌ Mistake 2: Forgetting RELEASE after new

```c
/* Wrong */
String* s = new_String("hi");
// no RELEASE → memory leak!!

/* Correct */
String* s = new_String("hi");
// after use
RELEASE((Object*)s);  // ✅ required!!
```

### ❌ Mistake 3: Forgetting RELEASE after accept()

```c
/* Wrong */
TcpSocket* c = server->accept(...);
loop->addSocket(loop, c, EV_READ);
// no RELEASE → leak!!

/* Correct */
TcpSocket* c = server->accept(...);
loop->addSocket(loop, c, EV_READ);
RELEASE((Object*)c);  // ✅ loop takes ownership
```

### ❌ Mistake 4: Object base not first

```c
/* Wrong */
struct Bad {
    int    field;
    Object base;  // ❌ wrong offset!!
};

/* Correct */
struct Good {
    Object base;  // ✅ must be first!!
    int    field;
};
```

### ❌ Mistake 5: Missing finalize

```c
/* Wrong */
static const Class _myClass = {
    .name = "MyClass",
    .size = sizeof(MyClass),
    // no finalize → internal resource leak!!
};

/* Correct */
static const Class _myClass = {
    .name     = "MyClass",
    .size     = sizeof(MyClass),
    .finalize = myclass_finalize  // ✅ required!!
};
```

---

## 11. Coding Contract (Required Rules)

```
1. Object base must be the first member of every struct
2. .size = sizeof(struct) must always be specified
3. finalize must always be implemented
4. Never call free() directly — use RELEASE only
5. Use new_ClassName() constructor format
6. Always check for NULL
7. Never use strtok — use strtok_r
8. Protect shared resources with Mutex
9. Initialize fd to -1
10. Use epsilon for float/double comparison
11. Thread-safety is mandatory
```

---

## 12. Remember These 3 Things

```
① Object base must always be the first struct member

② [OWNED]    = RELEASE required
   [BORROWED] = RELEASE absolutely forbidden

③ EventLoop  handles I/O
   ThreadPool handles computation
```

---

## 13. Documentation Links

| Doc | Content |
|---|---|
| [quickstart.md](quickstart.md) | Quick start |
| [api_en.md](api_en.md) | Full API Reference |
| [class_diagram.md](class_diagram.md) | Class diagram |
| [examples_guide.md](examples_guide.md) | Examples guide |
| [mysql_setup.md](mysql_setup.md) | MySQL setup |

---

**libcore v1.0 Iron Fortress | Toos IT Holdings | MIT License**
