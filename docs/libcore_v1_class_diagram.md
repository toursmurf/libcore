# libcore v1.0 "Iron Fortress" — Class Diagram

**Toos IT Holdings** | 44 Modules | Valgrind 0 bytes | ARC-based OOP in C99
**작성: 도과장 (B1 아키텍처 논리 연산소) | 실제 소스 기반**

---

## 📌 읽기 전에 / Before You Read

```
🇰🇷 이 문서는 libcore v1.0 의 전체 클래스 구조를 정리한 레퍼런스입니다.
     실제 헤더 소스를 기반으로 작성되었으며, 모든 필드와 VTable 함수를
     포함합니다. GitHub 공개 모듈만 수록되어 있습니다.

🇬🇧 This document is a reference for the entire class structure of libcore v1.0.
     Written based on actual header sources, it includes all fields and VTable
     functions. Only GitHub public modules are included.
```

---

## 📐 ARC 소유권 범례 / ARC Ownership Legend

| 표기 / Notation | 의미 (KOR) | Meaning (ENG) |
|---|---|---|
| `[OWNED]` | 호출자가 RELEASE 책임 | Caller must call RELEASE |
| `[BORROWED]` | RELEASE 금지 | Do NOT call RELEASE |
| `[PRIVATE]` | 내부 구현 은닉 | Internal use only |
| `must free()` | C 표준 free() 필수 | Must call C free() |
| `(RETAIN 포함)` | 내부에서 자동 RETAIN | Auto RETAIN inside |
| `(RELEASE 포함)` | 내부에서 자동 RELEASE | Auto RELEASE inside |

---

## 🏗️ 상속 계층 전체 구조 / Full Inheritance Hierarchy

```
🇰🇷 모든 클래스는 Object 를 최상위 부모로 상속받습니다.
     ARC (Automatic Reference Counting) 가 모든 객체에 적용됩니다.

🇬🇧 All classes inherit Object as the root parent.
     ARC (Automatic Reference Counting) is applied to all objects.

Object  (ARC 엔진 / ARC Engine)
├── String
├── ArrayList
│   └── ArrayListIterator
├── HashMap
├── Hashtable
│   └── HashtableIterator
├── Queue
├── Stack
├── Vector
│   └── VectorIterator
├── List
│   └── ListNode (내부 / internal)
├── LinkedList
│   └── LinkedListNode (내부 / internal)
├── BTree
│   └── BTreeNode (내부 / internal)
├── Tree
│   ├── TreeNode (내부 / internal)
│   └── TreeIterator
├── JSONNode
│   └── JsonValue (내부 리프 노드 / internal leaf node)
├── ByteBuffer
├── RingBuffer
├── Thread
├── ThreadPool
│   └── Task (내부 / internal)
├── Semaphore
├── Logger
│   └── AsyncLogger
├── Exception
├── Path
├── File
├── Directory
├── Socket  (추상 / abstract)
│   ├── TcpSocket
│   ├── UdpSocket
│   └── UnixSocket
├── EventLoop
├── Timer
├── Scheduler
│   └── ScheduleJob (내부 / internal)
├── Context
├── Config
├── ServiceRegistry
├── AppContext
├── Hasher
└── Cipher
```

---

## 📦 Object — `object.h`

```
🇰🇷 모든 libcore 객체의 최상위 부모.
     ARC 참조 카운팅의 심장이며, VTable 기반 다형성을 제공합니다.
     RETAIN/RELEASE/RELEASE_NULL 매크로로 메모리를 관리합니다.

🇬🇧 Root parent of all libcore objects.
     The heart of ARC reference counting, providing VTable-based polymorphism.
     Memory is managed via RETAIN/RELEASE/RELEASE_NULL macros.

┌─────────────────────────────────────────┐
│                 Class                   │
│           (클래스 메타정보 / Metadata)   │
├─────────────────────────────────────────┤
│ name         : const char*              │
│ size         : size_t                   │
├─────────────────────────────────────────┤
│ [VTable]                                │
│ toString()   : (Object*, char*, size_t) │
│ equals()     : (Object*, Object*) bool  │
│ hashCode()   : (Object*) int            │
│ finalize()   : (Object*)                │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│                Object                   │
│       (모든 객체의 첫 번째 멤버)          │
│       (First member of every struct)    │
├─────────────────────────────────────────┤
│ type         : const Class*             │
│ ref_count    : atomic_int               │
├─────────────────────────────────────────┤
│ [ARC 매크로 / ARC Macros]               │
│ RETAIN(obj)          ref_count++        │
│ RELEASE(obj)         ref_count-- → 0이면 finalize + free │
│ RELEASE_NULL(&ptr)   RELEASE + NULL화   │
└─────────────────────────────────────────┘
```

---

## 📦 String — `string_obj.h`

```
🇰🇷 Java 스타일 String. ARC 완전 적용.
     c_str() 은 [BORROWED] 이므로 RELEASE 금지.
     substring/concat/trim 등은 새 String 을 반환 ([OWNED]).

🇬🇧 Java-style String with full ARC support.
     c_str() is [BORROWED] — do NOT RELEASE.
     substring/concat/trim etc. return a new String ([OWNED]).

┌──────────────────────────────────────────────────────────────┐
│                          String                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ value        : char*                                         │
│ length       : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ length_f()   : int                                           │
│ charAt()     : char                                          │
│ equals()     : bool                                          │
│ indexOf()    : int                                           │
│ substring()  : String*  [OWNED]                              │
│ concat()     : String*  [OWNED]                              │
│ trim()       : String*  [OWNED]                              │
│ append()     : void     (인플레이스 / in-place)              │
│ clear()      : void                                          │
│ copy()       : String*  [OWNED]                              │
│ isEmpty()    : bool                                          │
│ toUpperCase(): void     (인플레이스 / in-place)              │
│ toLowerCase(): void     (인플레이스 / in-place)              │
│ toInt()      : int                                           │
│ toLong()     : long long                                     │
│ toDouble()   : double                                        │
│ c_str()      : const char*  [BORROWED]  ← RELEASE 금지!!    │
│ reverse()    : String*  [OWNED]                              │
│ replace()    : String*  [OWNED]                              │
│ split()      : ArrayList*  [OWNED]  ← 사용 후 RELEASE 필수  │
│ matches()    : bool     (정규표현식 / regex)                 │
│ eregi()      : bool     (대소문자 무시 / case-insensitive)   │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 ArrayList / ArrayListIterator — `arraylist.h`

```
🇰🇷 동적 배열. Thread-Safe (내부 mutex). Iterator 지원.
     add() 시 내부 RETAIN, remove() 시 내부 RELEASE 자동.
     detach() 는 RELEASE 없이 소유권 이전 ([OWNED] 반환).

🇬🇧 Dynamic array. Thread-Safe (internal mutex). Iterator support.
     add() auto-RETAINs, remove() auto-RELEASEs internally.
     detach() transfers ownership without RELEASE ([OWNED]).

┌──────────────────────────────────────────────────────────────┐
│                        ArrayList                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ items        : Object**                                      │
│ size         : int                                           │
│ capacity     : int                                           │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ add()        : void     (RETAIN 포함 / auto RETAIN)          │
│ get()        : Object*  [BORROWED]                           │
│ remove()     : void     (RELEASE 포함 / auto RELEASE)        │
│ detach()     : Object*  [OWNED]  (RELEASE 안 함 / no RELEASE)│
│ getSize()    : int                                           │
│ clear()      : void     (전체 RELEASE / RELEASE all)         │
│ isEmpty()    : bool                                          │
│ forEach()    : void                                          │
│ find()       : void*                                         │
│ sort()       : void                                          │
│ iterator()   : ArrayListIterator*  [OWNED]                   │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                   ArrayListIterator                          │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ list         : ArrayList*  [BORROWED]                        │
│ currentIndex : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ hasNext()    : bool                                          │
│ next()       : Object*  [BORROWED]                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 HashMap — `hashmap.h`

```
🇰🇷 문자열 키 기반 해시맵. Thread-Safe.
     keys()/values() 반환 ArrayList 는 사용 후 RELEASE 필수.
     put() 시 이전 값 자동 RELEASE 후 새 값 RETAIN.

🇬🇧 String-key hash map. Thread-Safe.
     keys()/values() return ArrayList must be RELEASEd after use.
     put() auto-RELEASEs old value and RETAINs new value.

┌──────────────────────────────────────────────────────────────┐
│                         HashMap                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ buckets      : HashNode**                                    │
│ capacity     : int                                           │
│ size         : int                                           │
│ loadFactor   : float                                         │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: HashNode]                            │
│   key   : char*                                              │
│   value : Object*                                            │
│   next  : HashNode*                                          │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ put()        : void     (RETAIN 포함 / auto RETAIN)          │
│ get()        : Object*  [BORROWED]                           │
│ hasKey()     : bool                                          │
│ remove()     : void     (RELEASE 포함 / auto RELEASE)        │
│ detach()     : Object*  [OWNED]                              │
│ clear()      : void                                          │
│ forEach()    : void                                          │
│ getSize()    : int                                           │
│ isEmpty()    : bool                                          │
│ keys()       : ArrayList*  [OWNED]  ← 사용 후 RELEASE 필수  │
│ values()     : ArrayList*  [OWNED]  ← 사용 후 RELEASE 필수  │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Hashtable / HashtableIterator — `hashtable.h`

```
🇰🇷 제네릭 키-값 해시테이블. Fail-Fast Iterator 지원.
     modCount 로 순회 중 수정 감지.
     containsValue() 는 O(N) 선형 탐색.

🇬🇧 Generic key-value hash table. Fail-Fast Iterator support.
     Detects modification during iteration via modCount.
     containsValue() is O(N) linear search.

┌──────────────────────────────────────────────────────────────┐
│                        Hashtable                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ table        : HashtableEntry**                              │
│ capacity     : int                                           │
│ count        : int                                           │
│ threshold    : int                                           │
│ loadFactor   : float                                         │
│ modCount     : size_t    (Fail-Fast 감지 / Fail-Fast detect) │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: HashtableEntry]                      │
│   key   : Object*                                            │
│   value : Object*                                            │
│   hash  : size_t                                             │
│   next  : HashtableEntry*                                    │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ put()          : Object*  (이전 값 반환 / returns old value) │
│ get()          : Object*  [BORROWED]                         │
│ remove()       : Object*                                     │
│ containsKey()  : bool                                        │
│ containsValue(): bool     O(N)                               │
│ size()         : size_t                                      │
│ isEmpty()      : bool                                        │
│ clear()        : void                                        │
│ forEach()      : void                                        │
│ iterator()     : HashtableIterator*  [OWNED]                 │
│ keys()         : ArrayList*  [OWNED]                         │
│ values()       : ArrayList*  [OWNED]                         │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                   HashtableIterator                          │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base               : Object                                  │
│ ht                 : Hashtable*  [BORROWED]                  │
│ currentBucketIndex : size_t                                  │
│ currentEntry       : HashtableEntry*                         │
│ lastReturned       : HashtableEntry*                         │
│ expectedModCount   : size_t  (Fail-Fast 감지용)              │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ hasNext()    : bool                                          │
│ next()       : bool  (outKey, outValue 동시 반환)            │
│ remove()     : void                                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Queue — `queue.h`

```
🇰🇷 FIFO 큐. Object 상속, Thread-Safe.
     내부적으로 ArrayList 를 위임하여 구현.
     enqueue() 시 RETAIN, dequeue() 반환값은 호출자가 관리.

🇬🇧 FIFO queue. Inherits Object, Thread-Safe.
     Internally delegates to ArrayList.
     enqueue() RETAINs; caller manages dequeue() return value.

┌──────────────────────────────────────────────────────────────┐
│                          Queue                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ container    : ArrayList*  [OWNED]                           │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ enqueue()    : void                                          │
│ dequeue()    : void*    (앞에서 꺼내기 / remove from front)  │
│ peek()       : void*    [BORROWED]  (제거 없음 / no removal) │
│ isEmpty()    : bool                                          │
│ size()       : int                                           │
│ forEach()    : void                                          │
│ iterator()   : ArrayListIterator*  [OWNED]                   │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Stack — `stack.h`

```
🇰🇷 LIFO 스택. Object 상속, ArrayList 위임, Thread-Safe.
     push() 시 RETAIN, pop() 반환값은 호출자가 관리.

🇬🇧 LIFO stack. Inherits Object, delegates to ArrayList, Thread-Safe.
     push() RETAINs; caller manages pop() return value.

┌──────────────────────────────────────────────────────────────┐
│                          Stack                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ container    : ArrayList*  [OWNED]                           │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ push()       : void                                          │
│ pop()        : void*    (맨 위 꺼내기 / remove from top)     │
│ peek()       : void*    [BORROWED]  (제거 없음 / no removal) │
│ isEmpty()    : bool                                          │
│ isFull()     : bool                                          │
│ size()       : int                                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Vector / VectorIterator — `vector.h`

```
🇰🇷 C++ STL 스타일 동적 배열. Object 상속, Thread-Safe.
     순회 시 lock()/unlock() 수동 호출 필수.
     VECTOR_FOREACH 매크로로 편리하게 순회 가능.

🇬🇧 C++ STL-style dynamic array. Inherits Object, Thread-Safe.
     Manual lock()/unlock() required during iteration.
     Use VECTOR_FOREACH macro for convenient iteration.

┌──────────────────────────────────────────────────────────────┐
│                         Vector                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ items        : Object**                                      │
│ size         : int                                           │
│ capacity     : int                                           │
│ mutex        : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ push_back()  : void                                          │
│ at()         : Object*  [BORROWED]                           │
│ pop_back()   : Object*  [OWNED]                              │
│ get_size()   : int                                           │
│ lock()       : void     (수동 잠금 / manual lock)            │
│ unlock()     : void     (수동 해제 / manual unlock)          │
│ begin()      : VectorIterator                                │
│ end()        : VectorIterator                                │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                     VectorIterator                           │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ ptr          : Object**  (ARC 포인터 / ARC pointer)          │
└──────────────────────────────────────────────────────────────┘
  매크로 / Macro: VECTOR_FOREACH(vec, type, var)
```

---

## 📦 List / ListNode — `list.h`

```
🇰🇷 이중 연결 리스트. Object 상속, Thread-Safe.
     pushBack/pushFront 는 O(1), insertAt/get 은 O(N).
     popBack/popFront 는 [OWNED] 반환 → 호출자 RELEASE 필수.

🇬🇧 Doubly linked list. Inherits Object, Thread-Safe.
     pushBack/pushFront are O(1), insertAt/get are O(N).
     popBack/popFront return [OWNED] → caller must RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                          List                                │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ head         : ListNode*                                     │
│ tail         : ListNode*                                     │
│ size         : int                                           │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: ListNode]                            │
│   data : Object*                                             │
│   prev : ListNode*  (역방향 weak 참조 / weak back-reference) │
│   next : ListNode*                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ pushBack()   : void      O(1)                                │
│ pushFront()  : void      O(1)                                │
│ popBack()    : Object*  [OWNED]  O(1)                        │
│ popFront()   : Object*  [OWNED]  O(1)                        │
│ insertAt()   : void      O(N)                                │
│ removeAt()   : Object*  [OWNED]  O(N)                        │
│ get()        : Object*  [BORROWED]  O(N)                     │
│ clear()      : void                                          │
│ getSize()    : int                                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 LinkedList / LinkedListNode — `linked_list.h`

```
🇰🇷 단순 연결 리스트. Object 상속, Thread-Safe.
     data 는 RETAIN/RELEASE 대상.
     delete_node() 는 비교함수 기반으로 노드 탐색 후 삭제.

🇬🇧 Singly linked list. Inherits Object, Thread-Safe.
     data is subject to RETAIN/RELEASE.
     delete_node() searches by comparator function then removes.

┌──────────────────────────────────────────────────────────────┐
│                       LinkedList                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ head         : LinkedListNode*                               │
│ size         : int                                           │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: LinkedListNode]                      │
│   data : Object*  (RETAIN/RELEASE 대상)                      │
│   next : LinkedListNode*                                     │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ add_node()   : void                                          │
│ delete_node(): void  (비교함수 기반 / comparator-based)      │
│ print_list() : void                                          │
│ getSize()    : int                                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 BTree / BTreeNode — `btree.h`

```
🇰🇷 B-트리 (차수 t). Object 상속, Thread-Safe.
     search() 는 [BORROWED] 반환 — RELEASE 금지.
     clear() 시 모든 키/값 RELEASE.

🇬🇧 B-tree (degree t). Inherits Object, Thread-Safe.
     search() returns [BORROWED] — do NOT RELEASE.
     clear() RELEASEs all keys and values.

┌──────────────────────────────────────────────────────────────┐
│                          BTree                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ root         : BTreeNode*                                    │
│ t            : int          (최소 차수 / minimum degree)     │
│ size         : int                                           │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: BTreeNode]                           │
│   keys     : Object**                                        │
│   values   : Object**                                        │
│   children : BTreeNode**                                     │
│   num_keys : int                                             │
│   is_leaf  : bool                                            │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ insert()     : void                                          │
│ search()     : Object*  [BORROWED]                           │
│ clear()      : void                                          │
│ getSize()    : int                                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Tree / TreeNode / TreeIterator — `tree.h`

```
🇰🇷 이진 탐색 트리 (BST). Object 상속. 중위순회 이터레이터 지원.
     BFS(너비우선) 순회도 가능.
     search() 는 [BORROWED] — RELEASE 금지.

🇬🇧 Binary Search Tree (BST). Inherits Object. In-order iterator support.
     BFS (breadth-first) traversal also supported.
     search() is [BORROWED] — do NOT RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                          Tree                                │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ root         : TreeNode*                                     │
│ compare      : CompareFunc                                   │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: TreeNode]                            │
│   data  : Object*                                            │
│   left  : TreeNode*                                          │
│   right : TreeNode*                                          │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ insert()        : void                                       │
│ search()        : Object*  [BORROWED]                        │
│ remove()        : void                                       │
│ foreach()       : void     (중위순회 / in-order)             │
│ createIterator(): TreeIterator*  [OWNED]                     │
│ traverseBFS()   : void     (너비우선 / breadth-first)        │
│ getHeight()     : int                                        │
│ clear()         : void                                       │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                      TreeIterator                            │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ stack        : TreeStackNode*  (중위순회 내부 스택)           │
│ current      : TreeNode*                                     │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ hasNext()    : bool                                          │
│ next()       : Object*  [BORROWED]                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 JSONNode / JsonValue — `json.h`

```
🇰🇷 JSON 파서/직렬화. HashMap(Object 모드) 또는 ArrayList(Array 모드).
     toString()/writeValueAsString() 반환 char* 는 반드시 free() 필수.
     ObjectMapper 는 싱글톤 (GetObjectMapper() 로 획득).

🇬🇧 JSON parser/serializer. Uses HashMap (object mode) or ArrayList (array mode).
     toString()/writeValueAsString() return char* — must call free().
     ObjectMapper is singleton (obtained via GetObjectMapper()).

┌──────────────────────────────────────────────────────────────┐
│                        JSONNode                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base           : Object                                      │
│ core_data      : Object*  (HashMap* 또는 / or ArrayList*)    │
│ is_object_flag : int                                         │
│ is_array_flag  : int                                         │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 타입 체크 / Type check]                              │
│ isObject()   : int                                           │
│ isArray()    : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable: Object 모드 / Object mode]                           │
│ put()        : void                                          │
│ get()        : Object*  [BORROWED]                           │
│ getString()  : const char*                                   │
│ getInt()     : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable: Array 모드 / Array mode]                             │
│ add()        : void                                          │
│ getIndex()   : Object*  [BORROWED]                           │
│ length()     : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 공통 / Common]                                       │
│ toString()   : char*  (must free()  ← 반드시 free 필수!!)   │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│              JsonValue  (내부 리프 노드 / Internal leaf)      │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ type    : JsonValType                                        │
│          J_NULL / J_BOOL / J_NUMBER / J_STRING               │
│ union { boolean: int | number: double | string: char* }      │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                      ObjectMapper                            │
│              (싱글톤 / Singleton — GetObjectMapper())         │
├──────────────────────────────────────────────────────────────┤
│ writeValueAsString(): char*  (must free()!!)                 │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 ByteBuffer — `bytebuffer.h`

```
🇰🇷 Java NIO ByteBuffer 스타일. 동적 확장, Big-Endian 지원.
     최대 16MB. compact() 로 읽은 데이터 제거 후 공간 확보.
     readSlice() 는 [OWNED] 반환 → 호출자 RELEASE 필수.

🇬🇧 Java NIO ByteBuffer style. Dynamic expansion, Big-Endian support.
     Max 16MB. compact() removes read data and reclaims space.
     readSlice() returns [OWNED] → caller must RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                       ByteBuffer                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ data         : uint8_t*                                      │
│ capacity     : size_t   (최대 16MB / max 16MB)               │
│ read_pos     : size_t   (읽기 포인터 / read pointer)         │
│ write_pos    : size_t   (쓰기 포인터 / write pointer)        │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 쓰기 / Write]                                        │
│ writeByte()  : int                                           │
│ writeInt32() : int       (Big-Endian)                        │
│ write()      : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 읽기/확인 / Read/Peek]                               │
│ readByte()   : bool                                          │
│ readInt32()  : bool      (Big-Endian)                        │
│ peekInt32()  : bool      (포인터 이동 없음 / no pointer move)│
│ read()       : size_t                                        │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 관리 / Management]                                   │
│ compact()       : void   (읽은 데이터 제거 / remove read data)│
│ rewind()        : void   (읽기 포인터 초기화 / reset read ptr)│
│ skip()          : void                                       │
│ indexOf()       : ssize_t                                    │
│ remaining()     : size_t  (읽기 가능 바이트 / readable bytes)│
│ writableBytes() : size_t  (쓰기 가능 바이트 / writable bytes)│
│ readSlice()     : ByteBuffer*  [OWNED]                       │
├──────────────────────────────────────────────────────────────┤
│ [매크로 / Macros]                                             │
│ BB_REMAINING(bb)                                             │
│ BB_WRITABLE(bb)                                              │
│ BB_CLEAR(bb)                                                 │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 RingBuffer — `ring_buffer.h`

```
🇰🇷 고성능 원형 버퍼. Thread-Safe. DbWriter/FileWriter 내부 사용.
     push() 는 가득 차면 false 반환 (블로킹 없음).
     popWait() 는 타임아웃 기반 블로킹 대기.

🇬🇧 High-performance ring buffer. Thread-Safe. Used by DbWriter/FileWriter.
     push() returns false when full (non-blocking).
     popWait() blocks with timeout.

┌──────────────────────────────────────────────────────────────┐
│                       RingBuffer                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ items        : void**                                        │
│ capacity     : size_t                                        │
│ head         : size_t   (쓰기 위치 / write position)         │
│ tail         : size_t   (읽기 위치 / read position)          │
│ count        : size_t                                        │
│ lock         : pthread_mutex_t                               │
│ not_empty    : pthread_cond_t  (popWait 대기 / wait cond)    │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ push()       : bool    (가득 차면 false / false when full)   │
│ pop()        : void*   (비어있으면 NULL / NULL when empty)   │
│ popWait()    : void*   (타임아웃 블로킹 / timeout blocking)  │
│ getSize()    : size_t                                        │
│ isEmpty()    : bool                                          │
│ isFull()     : bool                                          │
│ clear()      : void                                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Thread — `thread.h`

```
🇰🇷 POSIX 스레드 래퍼. Object 상속. 5단계 상태머신.
     start() 후 반드시 join() 또는 detach() 호출 필수.
     join() 은 스레드 완료까지 블로킹.

🇬🇧 POSIX thread wrapper. Inherits Object. 5-state state machine.
     Must call join() or detach() after start().
     join() blocks until thread completion.

┌──────────────────────────────────────────────────────────────┐
│                         Thread                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ handle       : pthread_t                                     │
│ run_func     : Runnable  (void* (*)(void*))                  │
│ arg          : void*                                         │
│ return_value : void*                                         │
│ is_running   : bool                                          │
├──────────────────────────────────────────────────────────────┤
│ [상태머신 / State Machine: 5단계]                             │
│   NEW → RUNNING → FINISHED → JOINED / DETACHED              │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ start()      : void                                          │
│ join()       : void*   (블로킹 / blocking)                   │
│ detach()     : void    (독립 실행 / independent execution)   │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 ThreadPool / Task — `threadpool.h`

```
🇰🇷 스레드 풀. 작업 큐 기반. parallel_for 지원.
     shutdown() 은 모든 워커 스레드 join 후 종료.
     Semaphore 로 동시 실행 자원 수 제한.

🇬🇧 Thread pool. Task queue-based. parallel_for support.
     shutdown() joins all worker threads before stopping.
     Semaphore limits concurrent resource count.

┌──────────────────────────────────────────────────────────────┐
│                       ThreadPool                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ num_threads  : int                                           │
│ workers      : Thread**                                      │
│ taskQueue    : Queue*       [OWNED]                          │
│ resourceSem  : Semaphore*   [OWNED]                          │
│ lock         : pthread_mutex_t                               │
│ cond         : pthread_cond_t                                │
│ stop         : bool                                          │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: Task]                                │
│   base : Object                                              │
│   func : TaskRoutine  (void* (*)(void*))                     │
│   arg  : void*                                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ submit()        : void                                       │
│ parallel_for()  : void*   (병렬 처리 / parallel processing)  │
│ shutdown()      : void    (모든 워커 join / join all workers) │
│ getPendingCount(): int                                       │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Semaphore — `semaphore_obj.h`

```
🇰🇷 POSIX 세마포어 래퍼. Object 상속.
     wait() = P 연산 (블로킹), post() = V 연산.
     tryWait() 는 비차단 P 연산 (즉시 반환).

🇬🇧 POSIX semaphore wrapper. Inherits Object.
     wait() = P operation (blocking), post() = V operation.
     tryWait() = non-blocking P operation (returns immediately).

┌──────────────────────────────────────────────────────────────┐
│                       Semaphore                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ sem          : sem_t   (POSIX 세마포어 / POSIX semaphore)    │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ wait()       : void    P 연산 (자원 획득 / acquire)          │
│ post()       : void    V 연산 (자원 반납 / release)          │
│ tryWait()    : bool    비차단 / non-blocking                 │
│ getValue()   : int     (잔여 자원 수 / remaining count)      │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Logger / AsyncLogger — `logger.h / async_logger.h`

```
🇰🇷 Logger: 동기 로거. 파일/콘솔 출력, 외부 Appender 지원.
     AsyncLogger: Logger 상속. 큐 기반 비동기 배치 처리.
     LOG_INFO/WARN/ERROR 매크로로 편리하게 사용.

🇬🇧 Logger: Synchronous logger. File/console output, external Appender support.
     AsyncLogger: Inherits Logger. Queue-based async batch processing.
     Use LOG_INFO/WARN/ERROR macros for convenience.

┌──────────────────────────────────────────────────────────────┐
│                         Logger                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base             : Object                                    │
│ level            : int                                       │
│ logFilePath      : char*                                     │
│ fileHandle       : FILE*                                     │
│ lock             : pthread_mutex_t                           │
│ toConsole        : bool                                      │
│ toFile           : bool                                      │
│ externalAppender : LogAppenderCallback                       │
│ appenderData     : void*                                     │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ setLogFile()   : void                                        │
│ setLevel()     : void                                        │
│ addAppender()  : void                                        │
│ debug()        : void                                        │
│ info()         : void                                        │
│ warn()         : void                                        │
│ error()        : void                                        │
├──────────────────────────────────────────────────────────────┤
│ [매크로 / Macros]                                             │
│ LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR                  │
└──────────────────────────────────────────────────────────────┘
          ▲ (상속 / extends)
┌──────────────────────────────────────────────────────────────┐
│                      AsyncLogger                             │
│                    (extends Logger)                          │
├──────────────────────────────────────────────────────────────┤
│ [추가 필드 / Additional Fields]                               │
│ base       : Logger                                          │
│ queue      : Queue*   [OWNED]  (로그 큐 / log queue)         │
│ worker     : pthread_t                                       │
│ lock       : pthread_mutex_t                                 │
│ cond       : pthread_cond_t                                  │
│ running    : bool                                            │
│ batch_size : size_t                                          │
│ inner      : Logger*  [BORROWED]                             │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ start()    : void   (워커 스레드 시작 / start worker thread) │
│ stop()     : void   (flush 후 종료 / stop after flush)       │
├──────────────────────────────────────────────────────────────┤
│ [매크로 / Macros]                                             │
│ ALOG_DEBUG / ALOG_INFO / ALOG_WARN / ALOG_ERROR              │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Exception — `exception.h`

```
🇰🇷 예외 처리. ErrorCode 체계, Cause 체이닝 지원.
     throw_Exception 매크로로 파일명/라인번호 자동 포함.
     getCause() 는 [BORROWED] — RELEASE 금지.

🇬🇧 Exception handling. ErrorCode system, cause chaining support.
     throw_Exception macro auto-includes filename/line number.
     getCause() is [BORROWED] — do NOT RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                       Exception                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ code         : ErrorCode                                     │
│ message      : char*                                         │
│ fileName     : char*                                         │
│ lineNumber   : int                                           │
│ cause        : Exception*  (원인 체이닝 / cause chaining)    │
├──────────────────────────────────────────────────────────────┤
│ [ErrorCode]                                                  │
│ OK=0 / ERR_NULL / ERR_OOM / ERR_IO / ERR_PARSE / ERR_INVALID│
│ ERR_FILE_NOT_FOUND/PERM/READ/WRITE (200~203)                 │
│ ERR_NET_CONNECT/TIMEOUT/HTTP (300~302)                       │
│ ERR_CONFIG=500                                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ getMessage()       : const char*                             │
│ getCause()         : Exception*  [BORROWED]                  │
│ getCode()          : ErrorCode                               │
│ hasCause()         : bool                                    │
│ printStackTrace()  : void                                    │
├──────────────────────────────────────────────────────────────┤
│ [매크로 / Macros]                                             │
│ throw_Exception(code, msg)                                   │
│ throw_ExceptionCause(code, msg, cause)                       │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Path / File / Directory — `path.h / file.h / directory.h`

```
🇰🇷 Path: 경로 처리. Java Path 스타일.
     File: 파일 I/O. fd 초기값 -1. ARC 완전 적용.
     Directory: 디렉토리 조작. walkTree() 로 전체 트리 순회.

🇬🇧 Path: Path handling. Java Path style.
     File: File I/O. fd initialized to -1. Full ARC support.
     Directory: Directory operations. walkTree() for full tree traversal.

┌──────────────────────────────────────────────────────────────┐
│                          Path                                │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ path         : const char* const  (strdup 소유)              │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ getFileName()      : String*  [OWNED]                        │
│ getBaseName()      : String*  [OWNED]                        │
│ getExtension()     : String*  [OWNED]                        │
│ getParent()        : String*  [OWNED]                        │
│ getCanonicalPath() : Path*    [OWNED]                        │
│ normalize()        : Path*    [OWNED]                        │
│ toAbsolute()       : Path*    [OWNED]                        │
│ isAbsolute()       : bool                                    │
│ resolve()          : Path*    [OWNED]                        │
│ sibling()          : Path*    [OWNED]                        │
│ withExt()          : Path*    [OWNED]                        │
│ equals()           : bool                                    │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                          File                                │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ filePath     : Path*   [OWNED]                               │
│ fd           : int     (초기값 -1 / initialized to -1)       │
│ is_open      : bool                                          │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 정보 / Info]                                         │
│ exists() / length() / isFile() / isSymlink()                 │
│ isReadable() / isWritable() / canExecute()                   │
│ lastModifiedMs() / lastAccessedMs() / creationTimeMs()       │
├──────────────────────────────────────────────────────────────┤
│ [VTable: I/O]                                                │
│ readAllText()    : String*      [OWNED]                      │
│ readAllBytes()   : ByteBuffer*  [OWNED]                      │
│ readLines()      : ArrayList*   [OWNED]                      │
│ writeString()    : bool                                      │
│ appendString()   : bool                                      │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 조작 / Operations]                                   │
│ copyTo() / deleteFile() / renameAtomic()                     │
│ fsync() / lockExclusive() / unlock()                         │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 해시/메타 / Hash/Meta]                               │
│ md5()            : String*  [OWNED]                          │
│ sha256()         : String*  [OWNED]                          │
│ equalsContent()  : bool                                      │
│ guessMimeType()  : String*  [OWNED]                          │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                       Directory                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ dirPath      : Path*   [OWNED]                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ exists()          : bool                                     │
│ mkdirs()          : bool    (재귀 생성 / recursive create)   │
│ listFiles()       : ArrayList*  [OWNED]                      │
│ walkTree()        : ArrayList*  [OWNED]  (전체 순회 / full)  │
│ deleteRecursive() : bool                                     │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Socket 계층 / Socket Hierarchy — `socket_base.h / tcp_socket.h / udp_socket.h / unix_socket.h`

```
🇰🇷 Socket 은 추상 기반 클래스. TCP/UDP/Unix 가 상속.
     on_readable 콜백으로 EventLoop 와 연동.
     SOCKET_WOULD_BLOCK 반환 시 엣지트리거 루프 종료 처리 필수.

🇬🇧 Socket is abstract base. TCP/UDP/Unix inherit from it.
     on_readable callback integrates with EventLoop.
     Must handle SOCKET_WOULD_BLOCK — exit edge-trigger loop.

┌──────────────────────────────────────────────────────────────┐
│                    Socket  (추상 / Abstract)                  │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ fd           : int           (초기값 -1 / init -1)           │
│ is_open      : bool                                          │
│ protocol     : SocketProtocol  (TCP/UDP/UNIX)                │
├──────────────────────────────────────────────────────────────┤
│ [VTable: 공통 / Common]                                       │
│ send()       : ssize_t                                       │
│ recv()       : ssize_t                                       │
│ getFD()      : int                                           │
│ close()      : void                                          │
│ bind()       : int                                           │
│ listen()     : int                                           │
│ connect()    : int                                           │
├──────────────────────────────────────────────────────────────┤
│ [EventLoop 연동 콜백 / EventLoop callbacks]                   │
│ on_readable(): void  (수신 이벤트 / read event)              │
│ on_writable(): void  (송신 이벤트 / write event)             │
│ on_error()  : void                                           │
└──────────────────────────────────────────────────────────────┘
     ▲ (상속 / extends)

┌────────────────────┐ ┌────────────────────┐ ┌───────────────────────┐
│     TcpSocket      │ │     UdpSocket      │ │      UnixSocket       │
│  (extends Socket)  │ │  (extends Socket)  │ │   (extends Socket)    │
├────────────────────┤ ├────────────────────┤ ├───────────────────────┤
│ base : Socket      │ │ base : Socket      │ │ base       : Socket   │
│                    │ │ (추가 필드 없음)    │ │ bound_path : char[108]│
│ [추가 VTable]      │ │ (no extra fields)  │ │                       │
│ accept()           │ │                    │ │ [추가 VTable]         │
│  → TcpSocket*      │ │                    │ │ accept()              │
│  [OWNED]           │ │                    │ │  → UnixSocket* [OWNED]│
└────────────────────┘ └────────────────────┘ └───────────────────────┘

생성자 / Constructors:
  new_TcpServer(host, port)   → bind + listen 자동
  new_TcpClient(host, port)   → connect 자동
  new_UdpServer(host, port)   → bind 자동
  new_UdpClient()
  new_UnixServer(path)        → bind + listen 자동
  new_UnixClient(path)        → connect 자동
```

---

## 📦 EventLoop — `event_loop.h`

```
🇰🇷 epoll 기반 이벤트 루프. EPOLLET 엣지 트리거.
     소켓 + 타이머 통합 관리. SIGINT 즉시 인지.
     EINTR 자동 재시도. run() 은 블로킹 루프.

🇬🇧 epoll-based event loop. EPOLLET edge trigger.
     Unified management of sockets + timers. Immediate SIGINT detection.
     Auto EINTR retry. run() is a blocking loop.

┌──────────────────────────────────────────────────────────────┐
│                       EventLoop                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base          : Object                                       │
│ epoll_fd      : int                                          │
│ is_running    : volatile bool  (SIGINT 즉각 인지)            │
│ max_events    : int                                          │
│ event_buffer  : struct epoll_event*                          │
│ logger        : Logger*  [BORROWED]                          │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ addSocket()    : int    (소켓 등록 / register socket)        │
│ delSocket()    : int    (소켓 제거 / unregister socket)      │
│ addTimer()     : int    (타이머 등록 / register timer)       │
│ removeTimer()  : int                                         │
│ poll()         : int    (비블로킹 단일 폴링 / single poll)   │
│ run()          : void   (블로킹 루프 시작 / start loop)      │
│ stop()         : void   (루프 중지 / stop loop)              │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Timer — `timer.h`

```
🇰🇷 Linux timerfd 기반 타이머. 반복/단발 지원.
     EventLoop 에 등록하여 사용. 이름 있는 타이머 지원.
     isActive() 로 동작 여부 확인.

🇬🇧 Linux timerfd-based timer. Repeating/one-shot support.
     Register with EventLoop for use. Named timer support.
     isActive() checks if timer is running.

┌──────────────────────────────────────────────────────────────┐
│                         Timer                                │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ tfd          : int        (Linux timerfd)                    │
│ repeating    : bool                                          │
│ interval_ms  : long                                          │
│ callback     : TimerCallback                                 │
│ user_data    : void*  [BORROWED]                             │
│ active       : bool                                          │
│ name         : char[64]                                      │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ start()      : bool                                          │
│ stop()       : void                                          │
│ reset()      : void                                          │
│ isActive()   : bool                                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Scheduler / ScheduleJob — `scheduler.h`

```
🇰🇷 timerfd + epoll + ThreadPool 연동 스케줄러.
     우선순위 기반 작업 등록 지원 (addEx).
     이름으로 작업 제거 가능.

🇬🇧 Scheduler integrating timerfd + epoll + ThreadPool.
     Priority-based job registration (addEx).
     Jobs can be removed by name.

┌──────────────────────────────────────────────────────────────┐
│                       Scheduler                              │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ jobs         : ArrayList*   [OWNED]                          │
│ lock         : pthread_mutex_t                               │
│ pool         : ThreadPool*  [BORROWED]                       │
│ loop         : EventLoop*   [BORROWED]                       │
├──────────────────────────────────────────────────────────────┤
│ [내부 구조체 / Internal: ScheduleJob]                         │
│   base      : Object                                         │
│   name      : char[64]                                       │
│   timer     : Timer*       [OWNED]                           │
│   callback  : TimerCallback                                  │
│   user_data : void*        [BORROWED]                        │
│   scheduler : Scheduler*   [BORROWED]                        │
│   run_count : atomic_size_t                                  │
│   last_run  : time_t                                         │
│   priority  : JobPriority  (LOW/NORMAL/HIGH/URGENT)          │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ add()        : bool                                          │
│ addEx()      : bool   (우선순위 포함 / with priority)        │
│ remove()     : bool   (이름으로 / by name)                   │
│ start()      : void                                          │
│ stop()       : void                                          │
│ count()      : size_t                                        │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Context — `context.h`

```
🇰🇷 키-값 런타임 컨텍스트. Thread-Safe. HashMap 내부 사용.
     setString/getInt 등 타입별 편의 함수 제공.
     remove() 는 [OWNED] 반환 → 호출자 RELEASE 필수.

🇬🇧 Key-value runtime context. Thread-Safe. Uses HashMap internally.
     Convenience functions per type: setString/getInt etc.
     remove() returns [OWNED] → caller must RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                        Context                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ data         : HashMap*  [OWNED]                             │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ set()        : void                                          │
│ get()        : Object*  [BORROWED]                           │
│ remove()     : Object*  [OWNED]                              │
│ has()        : bool                                          │
│ clear()      : void                                          │
│ getSize()    : int                                           │
│ setString()  : void                                          │
│ getString()  : String*  [BORROWED]                           │
│ setInt()     : void                                          │
│ getInt()     : int                                           │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Config — `config.h`

```
🇰🇷 INI 설정 파일 로더. HashMap 기반.
     get() 은 키 없으면 NULL 반환.
     getString/getInt/getBool 은 기본값 지원.

🇬🇧 INI config file loader. HashMap-based.
     get() returns NULL if key not found.
     getString/getInt/getBool support default values.

┌──────────────────────────────────────────────────────────────┐
│                         Config                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ map          : HashMap*  [OWNED]                             │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ load()       : bool                                          │
│ get()        : const char*  (NULL 반환 가능 / may be NULL)   │
│ getString()  : const char*  (기본값 / with default)          │
│ getInt()     : int          (기본값 / with default)          │
│ getBool()    : bool         (기본값 / with default)          │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 ServiceRegistry — `service_registry.h`

```
🇰🇷 DI 컨테이너. Class 타입 기반 서비스 등록/조회.
     REG_GET 매크로로 타입 안전하게 조회.
     get() 은 [BORROWED] → RELEASE 금지.

🇬🇧 DI container. Class type-based service registration/lookup.
     REG_GET macro for type-safe lookup.
     get() is [BORROWED] → do NOT RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                    ServiceRegistry                           │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ services     : HashMap*  [OWNED]                             │
│ lock         : pthread_mutex_t                               │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ register_s() : void                                          │
│ get()        : Object*  [BORROWED]                           │
│ has()        : bool                                          │
│ unregister() : void                                          │
├──────────────────────────────────────────────────────────────┤
│ [매크로 / Macro]                                              │
│ REG_GET(reg, TYPE)  ← 타입 안전 조회 / type-safe lookup      │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 AppContext — `app_context.h`

```
🇰🇷 통합 앱 컨텍스트. Config + Context + ServiceRegistry 통합.
     전역 인스턴스 g_app 제공.
     getService() 는 [BORROWED] → RELEASE 금지.

🇬🇧 Unified app context. Integrates Config + Context + ServiceRegistry.
     Global instance g_app provided.
     getService() is [BORROWED] → do NOT RELEASE.

┌──────────────────────────────────────────────────────────────┐
│                       AppContext                             │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ config       : Context*          [OWNED]                     │
│ runtime      : Context*          [OWNED]                     │
│ reg          : ServiceRegistry*  [OWNED]                     │
│ initialized  : bool                                          │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ init()             : bool                                    │
│ destroy_all()      : void                                    │
│ setConfig()        : void                                    │
│ getConfig()        : String*  [BORROWED]                     │
│ getConfigInt()     : int                                     │
│ registerService()  : void                                    │
│ getService()       : Object*  [BORROWED]                     │
├──────────────────────────────────────────────────────────────┤
│ [전역 / Global]                                               │
│ extern AppContext* g_app                                     │
├──────────────────────────────────────────────────────────────┤
│ [매크로 / Macro]                                              │
│ APP_GET_SERVICE(app, TYPE)                                   │
└──────────────────────────────────────────────────────────────┘
```

---

## 📦 Hasher / Cipher — `crypto.h`

```
🇰🇷 암호화 모듈. OpenSSL EVP 래핑.
     Hasher: 단방향 해시 (SHA-256/SHA-512 등).
     Cipher: 양방향 암호화 (AES-256-CBC 등).
     Base64_encode/decode 반환 char* 는 반드시 free() 필수.

🇬🇧 Crypto module. OpenSSL EVP wrapper.
     Hasher: One-way hash (SHA-256/SHA-512 etc.).
     Cipher: Two-way encryption (AES-256-CBC etc.).
     Base64_encode/decode return char* — must call free().

┌──────────────────────────────────────────────────────────────┐
│                         Hasher                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ algo         : char[32]   ("SHA-256" / "SHA-512" 등)         │
│ ctx          : void*      [PRIVATE]                          │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ hash()       : String*  [OWNED]                              │
│ verify()     : bool                                          │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                         Cipher                               │
│                    (extends Object)                          │
├──────────────────────────────────────────────────────────────┤
│ [필드 / Fields]                                               │
│ base         : Object                                        │
│ algo         : char[32]   ("AES-256-CBC" 등)                 │
│ ctx          : void*      [PRIVATE]                          │
│ key          : uint8_t[64]                                   │
│ iv           : uint8_t[64]                                   │
│ key_len      : size_t                                        │
│ iv_len       : size_t                                        │
├──────────────────────────────────────────────────────────────┤
│ [VTable]                                                     │
│ init()       : bool                                          │
│ encrypt()    : ByteBuffer*  [OWNED]                          │
│ decrypt()    : ByteBuffer*  [OWNED]                          │
├──────────────────────────────────────────────────────────────┤
│ [정적 유틸리티 / Static Utilities]                            │
│ Base64_encode()      : char*     (must free()!!)             │
│ Base64_decode()      : uint8_t*  (must free()!!)             │
│ Crypto_SHA1()        : void      (외부 의존성 없음 / no deps)│
│ Crypto_Base64Encode(): char*     (must free()!!)             │
└──────────────────────────────────────────────────────────────┘
```

---

## 🔗 의존성 맵 / Dependency Map

```
🇰🇷 핵심 모듈 간 의존 관계입니다.
🇬🇧 Key dependency relationships between modules.

Object
  └── [모든 클래스의 기반 / Base of all classes]

AppContext
  ├── Context  (config 저장 / config storage)
  ├── Context  (runtime 저장 / runtime storage)
  └── ServiceRegistry
        └── HashMap  (내부 / internal)

Scheduler
  ├── ArrayList  (jobs 목록 / job list)
  ├── ThreadPool  [BORROWED]
  │     ├── Thread[]
  │     ├── Queue   (작업 큐 / task queue)
  │     └── Semaphore  (자원 제한 / resource limit)
  └── EventLoop  [BORROWED]
        └── Timer  (timerfd 연동 / timerfd integration)

File
  ├── Path  [OWNED]
  └── ByteBuffer  (readAllBytes 반환 / returned by readAllBytes)

JSONNode
  ├── HashMap   (Object 모드 / object mode)
  └── ArrayList (Array 모드 / array mode)

Socket 계층 / Socket Hierarchy
  ├── TcpSocket  ──▶  EventLoop  (on_readable/writable/error)
  ├── UdpSocket  ──▶  EventLoop
  └── UnixSocket ──▶  EventLoop

AsyncLogger
  ├── Logger   (inner — 실제 출력 / actual output)
  └── Queue    (로그 버퍼 / log buffer)

Context / Config / ServiceRegistry
  └── HashMap  (내부 저장소 / internal storage)
```

---

**44 Modules | Valgrind 0 bytes | TSan 0 warnings | MIT License**
**도과장 작성 (실제 소스 기반) | 클순이 부장 검수 | 철컥. 🔫**
