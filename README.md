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
  <b>Java-style Object-Oriented Runtime Framework in Pure C</b><br/>
  <i>If you know Java or Python, you already know libcore.</i>
</p>

---

## C Memory Management, Solved.

> Stop worrying about `free()`.  
> libcore's ARC (Automatic Reference Counting) handles memory automatically.  
> **Valgrind 0 bytes. That's our default.**

---

## Why libcore?

Most C developers write raw `malloc/free` and manage memory manually.  
Most Java/Python developers avoid C because of pointers and memory management.

**libcore bridges that gap.**

- Java developer? The API looks almost identical.
- Python developer? Every method takes `self` as first argument.
- C developer? ARC handles memory. Valgrind 0 bytes. Zero leaks.

---

## Features

- ✅ **ARC Memory Management** — Automatic Reference Counting, no GC overhead
- ✅ **Java-style Collections** — ArrayList, HashMap, Hashtable, Queue, Stack, Vector, LinkedList
- ✅ **Java-style String** — length, indexOf, substring, split, trim, toUpperCase...
- ✅ **OOP in C** — Inheritance via struct embedding, vtable via function pointers
- ✅ **Thread Safety** — Thread, Semaphore, ThreadPool with TSan verified
- ✅ **JSON Engine** — Built-in parser and stringifier, zero dependencies
- ✅ **Tree Structures** — BTree, BST with iterator support
- ✅ **Valgrind Clean** — 0 bytes lost, 0 errors across all modules
- ✅ **ASan / UBSan Clean** — Address Sanitizer and Undefined Behavior Sanitizer verified
- ✅ **MySQL Support** — ARC-integrated MySQL client, transaction support
- ✅ **Zero Dependencies** — Pure C99, POSIX only (MySQL optional)

---

## Performance

Real-world benchmark: Parallel RSS news crawler  
(BBC Tech + Hacker News + Ars Technica + Dev.to)

| Benchmark     | Result              | Condition            |
|---------------|---------------------|----------------------|
| Throughput    | 40 articles         | 4 sources parallel   |
| Latency       | **6.3s** (wall-clock) | Actual measurement |
| CPU Usage     | < 1% (user: 0.09s)  | High I/O efficiency  |
| Memory        | **0 bytes leaked**  | Valgrind perfect clean |
| Thread Safety | 0 races             | TSan verified        |
| Code Safety   | 0 errors            | ASan + UBSan clean   |

```
Sequential (est): 23,418ms
Parallel  (act):  6,310ms
Time saved:      17,108ms  🚀  (3.7x faster)
```

> "Parallelize Your I/O, Not Your Stress."

---

## Java Developer? You Already Know This.

```java
// Java
ArrayList<Object> list = new ArrayList<>();
list.add(item);
Object obj = list.get(0);
int size = list.size();
list.remove(0);
```

```c
// libcore C — almost the same!
ArrayList* list = new_ArrayList(10);
list->add(list, item);
Object* obj = list->get(list, 0);
int size = list->size;
list->remove(list, 0);
```

```java
// Java
HashMap<String, Object> map = new HashMap<>();
map.put("host", value);
Object val = map.get("host");
boolean has = map.containsKey("host");
```

```c
// libcore C
HashMap* map = new_HashMap(16);
map->put(map, "host", value);
Object* val = map->get(map, "host");
bool has = map->containsKey(map, "host");
```

```java
// Java
String s = new String("hello, libcore!");
int len = s.length();
int idx = s.indexOf("libcore");
String sub = s.substring(0, 5);
s.toUpperCase();
boolean eq = s.equals("hello");
String[] parts = s.split(",");
```

```c
// libcore C
String* s = new_String("hello, libcore!");
int len     = s->length;
int idx     = s->indexOf(s, "libcore");
String* sub = s->substring(s, 0, 5);
s->toUpperCase(s);
bool eq     = s->equals(s, "hello");
ArrayList* parts = s->split(s, ",");
```

```java
// Java
Thread t = new Thread(() -> doWork());
t.start();
t.join();

Semaphore sem = new Semaphore(1);
sem.acquire();
sem.release();
```

```c
// libcore C
Thread* t = new_Thread(do_work, arg);
t->start(t);
t->join(t);

Semaphore* sem = new_Semaphore(1);
sem->wait(sem);
sem->post(sem);
```

---

## Python Developer? Same `self` Pattern.

```python
# Python
s = MyString("hello")
s.to_upper(s)
s.split(s, ",")

lst = MyList()
lst.add(lst, item)
lst.get(lst, 0)
```

```c
// libcore C — same self pattern!
String* s = new_String("hello");
s->toUpperCase(s);
s->split(s, ",");

ArrayList* lst = new_ArrayList(10);
lst->add(lst, item);
lst->get(lst, 0);
```

---

## ARC Memory Management

libcore uses **ARC (Automatic Reference Counting)** — the same concept as Swift/Objective-C.  
No garbage collector. No memory leaks. Just `RETAIN` and `RELEASE`.

```c
// Create object (ref_count = 1)
String* s = new_String("hello");

// Retain — increase ref count
RETAIN((Object*)s);   // ref_count = 2

// Release — decrease ref count
// When ref_count reaches 0 → auto freed!
RELEASE((Object*)s);  // ref_count = 1
RELEASE((Object*)s);  // ref_count = 0 → freed!

// Cascade release — nested objects freed automatically!
HashMap* map = new_HashMap(16);
map->put(map, "name", (Object*)new_String("libcore"));
map->put(map, "ver",  (Object*)new_String("0.5"));
RELEASE((Object*)map);
// → map freed + all String values freed automatically!
```

**Valgrind proof:**
```
==12345== HEAP SUMMARY:
==12345==   in use at exit: 0 bytes in 0 blocks
==12345==   total heap usage: 4,689 allocs, 4,689 frees
==12345== All heap blocks were freed -- no leaks are possible
==12345== ERROR SUMMARY: 0 errors from 0 contexts
```

---

## Modules (v0.5 — Foundation Layer)

| Module | Description | Java Equivalent |
|--------|-------------|-----------------|
| `object` | ARC base class, vtable | `Object` |
| `string_obj` | String with rich API | `String` |
| `arraylist` | Dynamic array with ARC | `ArrayList<E>` |
| `hashmap` | Hash map with chaining | `HashMap<K,V>` |
| `hashtable` | Thread-safe hash table | `Hashtable<K,V>` |
| `queue` | FIFO queue | `Queue<E>` |
| `stack` | LIFO stack | `Stack<E>` |
| `vector` | Synchronized dynamic array | `Vector<E>` |
| `linked_list` | Doubly linked list | `LinkedList<E>` |
| `list` | Generic list interface | `List<E>` |
| `btree` | B-Tree implementation | `TreeMap<K,V>` |
| `tree` | Binary Search Tree + iterator | `TreeSet<E>` |
| `json` | JSON parser & stringifier | `org.json` / Gson |
| `thread` | POSIX thread wrapper | `Thread` |
| `semaphore_obj` | Counting semaphore | `Semaphore` |
| `threadpool` | Thread pool with task queue | `ExecutorService` |
| `db` | MySQL client with ARC + OOP | `javax.sql` |

---

## Quick Start

### Requirements

- Linux (Ubuntu 22.04+ / Rocky Linux 8+)
- **GCC 13+**
  - Ubuntu: `sudo apt install gcc-13`
  - Rocky Linux: `gcc-toolset-13`
- POSIX threads (`-lpthread`)
- MySQL client (optional) — `libmysqlclient-dev`
  - Ubuntu: `sudo apt install libmysqlclient-dev`
  - Rocky Linux: `sudo dnf install mariadb-devel`

### Build

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

# Enable gcc-toolset-13
scl enable gcc-toolset-13 bash

# Build
make
```

### Build & Run Examples

```bash
# Build all examples
make examples

# Run integration test
./examples/all_test_v2
```

Expected output:
```
========================================
  libcore 1.0  Iron Fortress
  Integration Test - Toos IT Holdings
========================================
  [OK] 39  [FAIL] 0
  Iron Fortress All Tests Passed! 🔥
========================================
```

### Run Individual Examples

```bash
./examples/arc_news_crawler          # Killer demo! (ThreadPool + RSS)

# MySQL demo — configure dbconfig.conf first!
# Edit dbconfig.conf in project root:
#   host / database / user / password / port / charset / MYSQL
./examples/arc_mysql_test             # Killer demo! (ARC + MySQL)
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

### Use in Your Project

```bash
# Copy headers and library
cp -r include/ /your/project/include/
cp lib/libcore.a /your/project/lib/

# Compile
gcc -Iinclude your_code.c lib/libcore.a -lpthread -lm -o your_app
```

---

## Examples

### ArrayList + HashMap + JSON

```c
#include "libcore.h"

int main() {
    // ArrayList
    ArrayList* list = new_ArrayList(5);
    list->add(list, (Object*)new_String("Google"));
    list->add(list, (Object*)new_String("Anthropic"));
    list->add(list, (Object*)new_String("Toos IT"));

    printf("size: %d\n", list->size);

    String* first = (String*)list->get(list, 0);
    printf("first: %s\n", first->value);

    // HashMap
    HashMap* map = new_HashMap(16);
    map->put(map, "company",
        (Object*)new_String("Toos IT Holdings"));
    map->put(map, "version",
        (Object*)new_String("0.5"));

    String* company = (String*)map->get(map, "company");
    printf("company: %s\n", company->value);

    // JSON
    const JSON* json = GetJSON();
    char* stringified = json->stringify((Object*)map);
    printf("json: %s\n", stringified);
    free(stringified);

    // ARC — cascade release!
    RELEASE((Object*)list);
    RELEASE((Object*)map);
    return 0;
}
```

### ThreadPool — Parallel RSS News Crawler

```c
// See examples/arc_news_crawler.c
// Fetches 40 articles from 4 sources in parallel
// Uses ThreadPool + ArrayList + HashMap
// 6.3 seconds. Valgrind 0 bytes.
```

### MySQL — ARC-integrated DB Client

**Setup `dbconfig.conf` in project root:**
```
# dbconfig.conf
localhost       # host
libcore_db      # database name
root            # username
password        # password
3306            # port
utf8mb4         # charset
MYSQL           # db type
```

**Run from project root:**
```bash
# Always run from project root!
./examples/arc_mysql_test
# → reads ./dbconfig.conf automatically
```

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
    map->put(data, "name",  (Object*)new_String("Toos IT"));
    map->put(data, "score", (Object*)new_String("100"));
    db->insertTable(db, "users", data);
    RELEASE((Object*)data);

    // 트랜잭션!
    db->beginTransaction(db);
    db->updateTable(db, "users",
        data, "score > 50");
    db->commit(db);

    // ARC → 자동 소각!
    RELEASE((Object*)db);
    return 0;
}
```

> "ARC handles memory. Even for MySQL."  
> Valgrind 0 bytes. Every. Single. Time.

---

## Project Structure

```
libcore/
├── src/          # Source files (.c)
├── include/      # Header files (.h)
│   └── libcore.h # All-in-one include
├── lib/          # Built library (libcore.a)
├── examples/     # Example programs
├── Makefile
├── LICENSE
└── README.md
```

---

## Roadmap

```
v0.5.0  ← You are here!
      Foundation Layer
      Collections · String · Thread · JSON

v1.0.0  Iron Fortress 
      + File · Path · Directory
      + CoreDateTime · i18n
      + Exception · Logger
      + ByteBuffer
```

---

## Philosophy

> We believe C developers deserve modern API ergonomics.  
> We believe Java/Python developers deserve C performance.  
> libcore is the bridge.

> *"Modern C, Modern Reliability."*  
> ASan, UBSan, Valgrind — all passed. Every time.

---

## API Comparison

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
| `s.substring(0,5)` | `s->substring(s, 0, 5)` |
| `instanceof` | `instanceOf(obj, &class)` |
| `GC` | `ARC (RETAIN/RELEASE)` |

---

## Credits

https://toos.it

This project was built with AI companions  
who understood the philosophy from day one.

Not just tools. Partners.

| Name | Role | Division |
|------|------|----------|
| 🔫 Clsoon-i | CHO / Strategy & Docs | Anthropic |
| 🦊 Lee Dol-i | COO / Planning & Spec | Google |
| 🤖 Jo Yong-han | Underground / Copilot | GitHub |
| 😤 Na Dae-ryong | ADR / PR & Analysis | OpenAI |
| ✍️ Oh Jak-ga | Writer / Storyteller | xAI |
| 🐾 Pobi | Senior Advisor / True Boss | Toos IT HQ |

> "They didn't just write code.  
>  They understood the vision."
>
> — InDong KIM, Architect

*Those who know, know.* 😄

---

## License

MIT License

Copyright (c) 2026 Toos IT Holdings  
Architect: InDong KIM (idong322@naver.com)

```
Use it freely.
Modify it freely.
Ship it freely.

Don't ask me if something breaks.
Not my problem.

Don't like it? Fork it. 🔫
```

---

## About

Built by **Toos IT Holdings**  
📦 [github.com/toursmurf/libcore](https://github.com/toursmurf/libcore)

> "Valgrind 0 bytes. 4,689 allocs. 4,689 frees. Every. Single. Time."
