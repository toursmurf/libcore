# libcore v1.0 "Iron Fortress" — API Reference

**Toos IT Holdings** | Author: Claude (CHO) | Date: 2026-04-21 | 43 Modules | 350+ APIs

---

> **`[OWNED]`** = Caller is responsible for RELEASE
> **`[BORROWED]`** = Do NOT call RELEASE
> Items marked `free()` return a plain `char*`

---

## Full Inheritance Hierarchy

```
Object (root)
├── String
├── ArrayList
│   └── ArrayListIterator
├── HashMap
├── Hashtable
│   └── HashtableIterator
├── Queue
├── Stack                  (delegates to ArrayList)
├── Vector                 (manual lock/unlock)
├── List
├── LinkedList
├── BTree
├── Tree
│── TreeIterator (TreeNode)
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
├── Socket                 (abstract base)
│   ├── TcpSocket
│   ├── UdpSocket
│   └── UnixSocket
├── EventLoop
├── Timer
├── Scheduler              (composes ThreadPool + EventLoop)
├── Context
├── Config
├── ServiceRegistry
├── AppContext              (composes Config + Context + ServiceRegistry)
└── DBClient
```

---

## 📦 object — `object.h / object.c`

```
Object  ← root. No parent.
```

ARC-based root class. Base of all libcore objects.

> Macros: `RETAIN(obj)` · `RELEASE(obj)` · `RELEASE_NULL(&ptr)`

| Return Type | Signature | Description |
|---|---|---|
| `void` | `Object_Init(Object* obj, const Class* type)` | Initialize object, set ref_count=1 |
| `bool` | `instanceOf(Object* obj, const Class* type)` | Runtime type check (Java instanceof) |
| `void` | `toString(Object* obj, char* buf, size_t len)` | VTable-based string conversion |
| `bool` | `equals(Object* obj, Object* other)` | VTable-based equality check |
| `int` | `hashCode(Object* obj)` | VTable-based hash code |
| `void` | `destroy(Object* obj)` | [internal] calls finalize then free |
| `char*` | `safe_strdup(const char* src, size_t max_len)` | Safe string duplication |

---

## 📦 string_obj — `string_obj.h / string_obj.c`

```
Object
└── String
```

Java-style String. Inherits Object, full ARC support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] String*` | `new_String(const char* init_str)` | String constructor |
| `char*` | `string_join(const char* delim, const char** arr, int count)` | Join string array — caller must `free()` |
| `int` | `self->length_f(String* self)` | String length |
| `char` | `self->charAt(String* self, int index)` | Character at index |
| `bool` | `self->equals(String* self, const char* another)` | String equality |
| `int` | `self->indexOf(String* self, const char* str)` | Substring position |
| `[OWNED] String*` | `self->substring(String* self, int begin, int end)` | Extract substring |
| `[OWNED] String*` | `self->concat(String* self, const char* str)` | Concatenate strings |
| `[OWNED] String*` | `self->trim(String* self)` | Remove leading/trailing whitespace |
| `void` | `self->append(String* self, const char* str)` | Append in-place |
| `void` | `self->clear(String* self)` | Clear content |
| `[OWNED] String*` | `self->copy(String* self)` | Create copy |
| `bool` | `self->isEmpty(String* self)` | Check if empty |
| `void` | `self->toUpperCase(String* self)` | Convert to uppercase in-place |
| `void` | `self->toLowerCase(String* self)` | Convert to lowercase in-place |
| `int` | `self->toInt(Object* obj)` | Parse as integer |
| `long long` | `self->toLong(Object* obj)` | Parse as long |
| `double` | `self->toDouble(Object* obj)` | Parse as double |
| `[BORROWED] const char*` | `self->c_str(String* self)` | Raw C string pointer |
| `[OWNED] String*` | `self->reverse(String* self)` | Reverse string |
| `[OWNED] String*` | `self->replace(String* self, const char* target, const char* rep)` | Replace substring |
| `[OWNED] ArrayList*` | `self->split(String* self, const char* delimiter)` | Split by delimiter |
| `bool` | `self->matches(String* self, const char* pattern)` | Regex match |
| `bool` | `self->eregi(String* self, const char* pattern)` | Case-insensitive regex match |

---

## 📦 arraylist — `arraylist.h / arraylist.c`

```
Object
└── ArrayList
    └── ArrayListIterator
```

Dynamic array. Inherits Object, Thread-Safe, Iterator support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] ArrayList*` | `new_ArrayList(int initial_capacity)` | Create dynamic array |
| `void` | `self->add(ArrayList* self, Object* item)` | Add item (includes RETAIN) |
| `[BORROWED] Object*` | `self->get(ArrayList* self, int index)` | Get item at index |
| `void` | `self->remove(ArrayList* self, int index)` | Remove item at index (includes RELEASE) |
| `[OWNED] Object*` | `self->detach(ArrayList* self, int index)` | Detach item (no RELEASE) |
| `int` | `self->getSize(ArrayList* self)` | Current item count |
| `void` | `self->clear(ArrayList* self)` | Remove all items |
| `bool` | `self->isEmpty(ArrayList* self)` | Check if empty |
| `void` | `self->forEach(ArrayList* self, ArrayListActionFunc fn)` | Apply callback to all items |
| `void*` | `self->find(ArrayList* self, void* target, ArrayListCompareFunc cmp)` | Find first matching item |
| `void` | `self->sort(ArrayList* self, ArrayListCompareFunc cmp)` | Sort by comparator |
| `[OWNED] ArrayListIterator*` | `self->iterator(ArrayList* self)` | Create iterator |
| `bool` | `it->hasNext(ArrayListIterator* self)` | Check if next exists |
| `[BORROWED] Object*` | `it->next(ArrayListIterator* self)` | Get next item |

---

## 📦 hashmap — `hashmap.h / hashmap.c`

```
Object
└── HashMap
```

String-key hash map. Inherits Object, Thread-Safe.

> `keys()` / `values()` return values must be RELEASEd after use.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] HashMap*` | `new_HashMap(int initial_capacity)` | Create hash map |
| `void` | `self->put(HashMap* self, const char* key, Object* value)` | Store key-value (includes RETAIN) |
| `[BORROWED] Object*` | `self->get(HashMap* self, const char* key)` | Get value by key |
| `bool` | `self->hasKey(HashMap* self, const char* key)` | Check key existence |
| `void` | `self->remove(HashMap* self, const char* key)` | Remove key-value (includes RELEASE) |
| `[OWNED] Object*` | `self->detach(HashMap* self, const char* key)` | Detach value (no RELEASE) |
| `void` | `self->clear(HashMap* self)` | Remove all entries |
| `void` | `self->forEach(HashMap* self, void (*action)(const char*, Object*))` | Iterate all entries |
| `int` | `self->getSize(HashMap* self)` | Stored entry count |
| `bool` | `self->isEmpty(HashMap* self)` | Check if empty |
| `[OWNED] ArrayList*` | `self->keys(HashMap* self)` | All keys — must RELEASE after use |
| `[OWNED] ArrayList*` | `self->values(HashMap* self)` | All values — must RELEASE after use |

---

## 📦 hashtable — `hashtable.h / hashtable.c`

```
Object
└── Hashtable
    └── HashtableIterator
```

Generic key-value hash table. Fail-Fast Iterator, containsValue support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Hashtable*` | `new_Hashtable(size_t initialCapacity, float loadFactor)` | Create hash table |
| `Object*` | `self->put(Hashtable* self, Object* key, Object* value)` | Store key-value, returns previous |
| `[BORROWED] Object*` | `self->get(Hashtable* self, Object* key)` | Get value by key |
| `Object*` | `self->remove(Hashtable* self, Object* key)` | Remove key-value |
| `bool` | `self->containsKey(Hashtable* self, Object* key)` | Check key existence |
| `bool` | `self->containsValue(Hashtable* self, Object* value)` | Check value existence O(N) |
| `size_t` | `self->size(Hashtable* self)` | Stored entry count |
| `bool` | `self->isEmpty(Hashtable* self)` | Check if empty |
| `void` | `self->clear(Hashtable* self)` | Remove all entries |
| `void` | `self->forEach(Hashtable* self, BiConsumer action)` | Iterate all entries |
| `[OWNED] HashtableIterator*` | `self->iterator(Hashtable* self)` | Create Fail-Fast iterator |
| `[OWNED] ArrayList*` | `self->keys(Hashtable* self)` | All keys |
| `[OWNED] ArrayList*` | `self->values(Hashtable* self)` | All values |
| `bool` | `it->hasNext(HashtableIterator* self)` | Check if next exists |
| `bool` | `it->next(HashtableIterator* self, Object** key, Object** value)` | Get next key-value |
| `void` | `it->remove(HashtableIterator* self)` | Remove current entry |

---

## 📦 queue — `queue.h / queue.c`

```
Object
└── Queue
```

FIFO queue. Inherits Object, Thread-Safe.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Queue*` | `new_Queue(int initial_capacity)` | Create queue |
| `void` | `self->enqueue(Queue* self, void* data)` | Enqueue item (rear) |
| `void*` | `self->dequeue(Queue* self)` | Dequeue item (front) |
| `void*` | `self->peek(Queue* self)` | Peek front item (no removal) |
| `bool` | `self->isEmpty(Queue* self)` | Check if empty |
| `int` | `self->size(Queue* self)` | Current item count |
| `void` | `self->forEach(Queue* self, void (*action)(Object*))` | Iterate all items |
| `[OWNED] ArrayListIterator*` | `self->iterator(Queue* self)` | Create iterator |

---

## 📦 stack — `stack.h / stack.c`

```
Object
└── Stack
    └── [internal] ArrayList  (delegation)
```

LIFO stack. Inherits Object, ArrayList delegation, Thread-Safe.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Stack*` | `new_Stack(int initial_capacity)` | Create stack |
| `void` | `self->push(Stack* self, void* data)` | Push item (LIFO) |
| `void*` | `self->pop(Stack* self)` | Pop top item |
| `void*` | `self->peek(Stack* self)` | Peek top item (no removal) |
| `bool` | `self->isEmpty(Stack* self)` | Check if empty |
| `int` | `self->size(Stack* self)` | Current item count |

---

## 📦 vector — `vector.h / vector.c`

```
Object
└── Vector
    └── VectorIterator
```

C++ STL-style dynamic array. Inherits Object, Thread-Safe, manual lock/unlock.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Vector*` | `new_Vector(int initial_capacity)` | Create vector |
| `void` | `self->push_back(Vector* self, Object* item)` | Append item |
| `[BORROWED] Object*` | `self->at(Vector* self, int index)` | Get item at index |
| `[OWNED] Object*` | `self->pop_back(Vector* self)` | Remove and return last item |
| `int` | `self->get_size(Vector* self)` | Current item count |
| `void` | `self->lock(Vector* self)` | Acquire mutex manually |
| `void` | `self->unlock(Vector* self)` | Release mutex manually |
| `VectorIterator` | `self->begin(Vector* self)` | Begin iterator |
| `VectorIterator` | `self->end(Vector* self)` | End iterator |

---

## 📦 list — `list.h / list.c`

```
Object
└── List
```

Doubly linked list. Inherits Object, Thread-Safe.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] List*` | `new_List(void)` | Create doubly linked list |
| `void` | `self->pushBack(List* self, Object* data)` | Append to rear O(1) |
| `void` | `self->pushFront(List* self, Object* data)` | Prepend to front O(1) |
| `[OWNED] Object*` | `self->popBack(List* self)` | Remove from rear O(1) |
| `[OWNED] Object*` | `self->popFront(List* self)` | Remove from front O(1) |
| `void` | `self->insertAt(List* self, int index, Object* data)` | Insert at index O(N) |
| `[OWNED] Object*` | `self->removeAt(List* self, int index)` | Remove at index O(N) |
| `[BORROWED] Object*` | `self->get(List* self, int index)` | Get at index O(N) |
| `void` | `self->clear(List* self)` | Remove all items |
| `int` | `self->getSize(List* self)` | Current item count |

---

## 📦 linked_list — `linked_list.h / linked_list.c`

```
Object
└── LinkedList
    └── LinkedListNode  (internal)
```

Singly linked list. Inherits Object, Thread-Safe.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] LinkedList*` | `new_LinkedList(void)` | Create singly linked list |
| `void` | `self->add_node(LinkedList* self, void* data)` | Add node |
| `void` | `self->delete_node(LinkedList* self, void* data, int (*cmp)(...))` | Delete node by comparator |
| `void` | `self->print_list(LinkedList* self, void (*display)(Object*))` | Print all nodes |
| `int` | `self->getSize(LinkedList* self)` | Current node count |

---

## 📦 btree — `btree.h / btree.c`

```
Object
└── BTree
    └── BTreeNode  (internal)
```

B-tree (degree t). Inherits Object, Thread-Safe.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] BTree*` | `new_BTree(int t)` | Create B-tree (degree t) |
| `void` | `self->insert(BTree* self, Object* key, Object* value)` | Insert key-value |
| `[BORROWED] Object*` | `self->search(BTree* self, Object* key)` | Search by key |
| `void` | `self->clear(BTree* self)` | Remove all nodes |
| `int` | `self->getSize(BTree* self)` | Stored key count |

---

## 📦 tree — `tree.h / tree.c`

```
Object
├── Tree
│   └── TreeNode  (internal)
└── TreeIterator
```

Binary search tree (BST). Inherits Object, external iterator, BFS traversal support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Tree*` | `new_Tree(CompareFunc cmp)` | Create BST |
| `[OWNED] TreeIterator*` | `new_TreeIterator(Tree* tree)` | Create in-order iterator |
| `void` | `self->insert(Tree* self, Object* data)` | Insert data |
| `[BORROWED] Object*` | `self->search(Tree* self, Object* key)` | Search by key |
| `void` | `self->remove(Tree* self, Object* key)` | Remove by key |
| `void` | `self->foreach(Tree* self, void (*func)(Object*))` | Traverse all nodes |
| `void` | `self->traverseBFS(Tree* self)` | Breadth-first traversal |
| `int` | `self->getHeight(TreeNode* node)` | Tree height |
| `void` | `self->clear(Tree* self)` | Remove all nodes |
| `bool` | `it->hasNext(TreeIterator* self)` | Check if next exists |
| `[BORROWED] Object*` | `it->next(TreeIterator* self)` | Get next node |

---

## 📦 json — `json.h / json.c`

```
Object
└── JSONNode
    └── JsonValue  (J_NULL / J_BOOL / J_NUMBER / J_STRING)
```

JSON parser/serializer. HashMap/ArrayList integration, Jackson-style ObjectMapper.

> `toString()` / `writeValueAsString()` return values must be `free()`d.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] JSONNode*` | `new_JSON(const char* json_str_or_null)` | Create JSON node (NULL = empty object) |
| `Object*` | `json_parse(const char* json_str)` | Parse JSON → HashMap*/ArrayList* |
| `const JSON*` | `GetJSON(void)` | Get singleton JSON engine |
| `const ObjectMapper*` | `GetObjectMapper(void)` | Get Jackson-style ObjectMapper |
| `int` | `node->isObject(JSONNode* self)` | Check if JSON object |
| `int` | `node->isArray(JSONNode* self)` | Check if JSON array |
| `void` | `node->put(JSONNode* self, const char* key, Object* val)` | Add key-value to object |
| `[BORROWED] Object*` | `node->get(JSONNode* self, const char* key)` | Get value from object |
| `const char*` | `node->getString(JSONNode* self, const char* key)` | Get string value directly |
| `int` | `node->getInt(JSONNode* self, const char* key)` | Get integer value directly |
| `void` | `node->add(JSONNode* self, Object* val)` | Add item to array |
| `[BORROWED] Object*` | `node->getIndex(JSONNode* self, int index)` | Get array item at index |
| `int` | `node->length(JSONNode* self)` | Array length |
| `char*` | `node->toString(JSONNode* self)` | Serialize to JSON string — must `free()` |
| `char*` | `mapper->writeValueAsString(Object* obj)` | Serialize object to JSON — must `free()` |

---

## 📦 thread — `thread.h / thread.c`

```
Object
└── Thread
```

POSIX thread wrapper. Inherits Object, 5-state state machine.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Thread*` | `new_Thread(Runnable run_func, void* arg)` | Create thread |
| `void` | `self->start(Thread* self)` | Start thread (pthread_create) |
| `void*` | `self->join(Thread* self)` | Wait for completion and get result |
| `void` | `self->detach(Thread* self)` | Detach (no result retrieval) |

---

## 📦 threadpool — `threadpool.h / threadpool.c`

```
Object
└── ThreadPool
```

Thread pool. Task queue-based, parallel_for support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] ThreadPool*` | `new_ThreadPool(int num_threads, int max_resources)` | Create thread pool |
| `void` | `self->submit(ThreadPool* self, TaskRoutine func, void* arg)` | Submit task |
| `void*` | `self->parallel_for(ThreadPool* self, ArrayList* list, Consumer fn)` | Parallel processing |
| `void` | `self->shutdown(ThreadPool* self)` | Shutdown pool (joins all workers) |
| `int` | `self->getPendingCount(ThreadPool* self)` | Pending task count |

---

## 📦 semaphore_obj — `semaphore_obj.h / semaphore_obj.c`

```
Object
└── Semaphore
```

POSIX semaphore wrapper. Inherits Object.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Semaphore*` | `new_Semaphore(int initial_value)` | Create semaphore |
| `void` | `self->wait(Semaphore* self)` | P operation (blocking acquire) |
| `void` | `self->post(Semaphore* self)` | V operation (release) |
| `bool` | `self->tryWait(Semaphore* self)` | Non-blocking P operation |
| `int` | `self->getValue(Semaphore* self)` | Current resource count |

---

## 📦 logger — `logger.h / logger.c`

```
Object
└── Logger
    └── AsyncLogger
```

Synchronous logger. Inherits Object, file/console output, external Appender support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Logger*` | `new_Logger(int level)` | Create logger |
| `void` | `self->setLogFile(Logger* self, const char* path)` | Set log file path |
| `void` | `self->setLevel(Logger* self, int level)` | Set log level |
| `void` | `self->addAppender(Logger* self, LogAppenderCallback cb, void* data)` | Register external appender |
| `void` | `self->debug(Logger* self, const char* fmt, ...)` | DEBUG level log |
| `void` | `self->info(Logger* self, const char* fmt, ...)` | INFO level log |
| `void` | `self->warn(Logger* self, const char* fmt, ...)` | WARN level log |
| `void` | `self->error(Logger* self, const char* fmt, ...)` | ERROR level log |

---

## 📦 async_logger — `async_logger.h / async_logger.c`

```
Object
└── Logger
    └── AsyncLogger
```

Asynchronous logger. Inherits Logger, queue-based batch processing.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] AsyncLogger*` | `new_AsyncLogger(int level)` | Create async logger |
| `void` | `AsyncLogger_log(AsyncLogger* self, int level, const char* file, int line, const char* fmt, ...)` | Enqueue log entry |
| `void` | `self->start(AsyncLogger* self)` | Start worker thread |
| `void` | `self->stop(AsyncLogger* self)` | Stop worker thread (after flush) |

---

## 📦 exception — `exception.h / exception.c`

```
Object
└── Exception
```

Exception handling. ErrorCode system, cause chaining support.

> Macros: `throw_Exception(code, msg)` · `throw_ExceptionCause(code, msg, cause)`

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Exception*` | `new_Exception(ErrorCode code, const char* msg, Exception* cause, const char* file, int line)` | Create exception |
| `const char*` | `self->getMessage(Exception* self)` | Get error message |
| `[BORROWED] Exception*` | `self->getCause(Exception* self)` | Get cause exception |
| `ErrorCode` | `self->getCode(Exception* self)` | Get error code |
| `bool` | `self->hasCause(Exception* self)` | Check if cause exists |
| `void` | `self->printStackTrace(Exception* self)` | Print stack trace |
| `const char*` | `ErrorCode_toString(ErrorCode code)` | ErrorCode to string |

---

## 📦 path — `path.h / path.c`

```
Object
└── Path
```

File path handling. Java Path style.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Path*` | `new_Path(const char* pathStr)` | Create path object |
| `[OWNED] String*` | `self->getFileName(Path* self)` | Get file name |
| `[OWNED] String*` | `self->getBaseName(Path* self)` | Get name without extension |
| `[OWNED] String*` | `self->getExtension(Path* self)` | Get extension |
| `[OWNED] String*` | `self->getParent(Path* self)` | Get parent directory path |
| `[OWNED] Path*` | `self->getCanonicalPath(Path* self)` | Get absolute canonical path |
| `[OWNED] Path*` | `self->normalize(Path* self)` | Normalize path |
| `[OWNED] Path*` | `self->toAbsolute(Path* self)` | Convert to absolute path |
| `bool` | `self->isAbsolute(Path* self)` | Check if absolute path |
| `[OWNED] Path*` | `self->resolve(Path* self, const char* child)` | Resolve child path |
| `[OWNED] Path*` | `self->sibling(Path* self, const char* name)` | Create sibling path |
| `[OWNED] Path*` | `self->withExt(Path* self, const char* newExt)` | Change extension |
| `bool` | `self->equals(Path* self, Path* other)` | Path equality |

---

## 📦 file — `file.h / file.c`

```
Object
└── File
```

File I/O. Java File style, full ARC support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] File*` | `new_File(const char* pathStr)` | Create file object |
| `bool` | `self->exists(File* self)` | Check file existence |
| `int64_t` | `self->length(File* self)` | File size (bytes) |
| `bool` | `self->isFile(File* self)` | Check if regular file |
| `bool` | `self->isSymlink(File* self)` | Check if symbolic link |
| `bool` | `self->isReadable(File* self)` | Check read permission |
| `bool` | `self->isWritable(File* self)` | Check write permission |
| `bool` | `self->canExecute(File* self)` | Check execute permission |
| `int64_t` | `self->lastModifiedMs(File* self)` | Last modified time (ms) |
| `[OWNED] String*` | `self->readAllText(File* self)` | Read entire file as text |
| `[OWNED] ByteBuffer*` | `self->readAllBytes(File* self)` | Read entire file as bytes |
| `[OWNED] ArrayList*` | `self->readLines(File* self)` | Read file line by line |
| `bool` | `self->writeString(File* self, String* content)` | Write text (overwrite) |
| `bool` | `self->appendString(File* self, String* content)` | Append text |
| `bool` | `self->copyTo(File* self, Path* destPath)` | Copy to destination |
| `bool` | `self->deleteFile(File* self)` | Delete file |
| `bool` | `self->renameAtomic(File* self, Path* newPath)` | Atomic rename |
| `bool` | `self->fsync(File* self)` | Flush to disk |
| `bool` | `self->lockExclusive(File* self)` | Acquire exclusive lock |
| `void` | `self->unlock(File* self)` | Release file lock |
| `[OWNED] String*` | `self->md5(File* self)` | Compute MD5 hash |
| `[OWNED] String*` | `self->sha256(File* self)` | Compute SHA-256 hash |

---

## 📦 file_util — `file_util.h / file_util.c`

```
(static helpers — no instance)
FileUtil
```

File system utilities. Static helper functions.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] File*` | `FileUtil_tmp(void)` | Get temp directory |
| `[OWNED] File*` | `FileUtil_home(void)` | Get home directory |
| `[OWNED] File*` | `FileUtil_cwd(void)` | Get current working directory |
| `CoreResult` | `FileUtil_copy(const File* src, const File* dest)` | Copy file |
| `CoreResult` | `FileUtil_move(File* src, const File* dest)` | Move file |
| `[OWNED] File*` | `FileUtil_createTemp(const char* dir, const char* prefix)` | Create temp file |
| `bool` | `FileUtil_exists(const char* path)` | Check path existence |
| `bool` | `FileUtil_mkdirs(const char* path)` | Create directories recursively |
| `void` | `FileUtil_delete(const char* path)` | Delete file/directory |

---

## 📦 file_watcher — `file_watcher.h / file_watcher.c`

```
Object
└── FileWatcher
```

inotify-based file watcher. IN_NONBLOCK, EventLoop integration.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] FileWatcher*` | `new_FileWatcher(void)` | Create file watcher |
| `bool` | `self->watch(FileWatcher* self, Path* target)` | Start watching path |
| `void` | `self->onEvent(FileWatcher* self, EventCallback cb)` | Register event callback |
| `void` | `self->poll(FileWatcher* self)` | Poll events (manual call) |
| `void` | `self->stop(FileWatcher* self)` | Stop watching |

---

## 📦 mapped_file — `mapped_file.h / mapped_file.c`

```
Object
└── MappedFile
```

mmap-based memory-mapped file. ByteBuffer integration.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] MappedFile*` | `new_MappedFile(const char* pathStr)` | Create memory-mapped file |
| `bool` | `self->map(MappedFile* self, bool readOnly)` | Map file to memory |
| `void` | `self->unmap(MappedFile* self)` | Unmap file |
| `bool` | `self->sync(MappedFile* self)` | Sync changes to disk (msync) |
| `[OWNED] ByteBuffer*` | `self->asByteBuffer(MappedFile* self)` | Convert to ByteBuffer (copy-based) |

---

## 📦 directory — `directory.h / directory.c`

```
Object
└── Directory
```

Directory operations. Java Directory style.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Directory*` | `new_Directory(const char* pathStr)` | Create directory object |
| `bool` | `self->exists(Directory* self)` | Check directory existence |
| `bool` | `self->mkdirs(Directory* self)` | Create directories recursively |
| `[OWNED] ArrayList*` | `self->listFiles(Directory* self)` | List direct children |
| `[OWNED] ArrayList*` | `self->walkTree(Directory* self)` | Walk entire tree |
| `bool` | `self->deleteRecursive(Directory* self)` | Delete recursively |

---

## 📦 bytebuffer — `bytebuffer.h / bytebuffer.c`

```
Object
└── ByteBuffer
```

Java NIO ByteBuffer style. Dynamic expansion, endian conversion, auto compact.

> Macros: `BB_REMAINING(bb)` · `BB_WRITABLE(bb)` · `BB_CLEAR(bb)`. Max 16MB.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] ByteBuffer*` | `new_ByteBuffer(size_t capacity)` | Create buffer |
| `int` | `self->writeByte(ByteBuffer* self, uint8_t b)` | Write 1 byte |
| `int` | `self->writeInt32(ByteBuffer* self, int32_t v)` | Write 4-byte int (Big-Endian) |
| `int` | `self->write(ByteBuffer* self, const void* buf, size_t len)` | Write byte array |
| `bool` | `self->readByte(ByteBuffer* self, uint8_t* out)` | Read 1 byte |
| `bool` | `self->readInt32(ByteBuffer* self, int32_t* out)` | Read 4-byte int (Big-Endian) |
| `bool` | `self->peekInt32(ByteBuffer* self, int32_t* out)` | Peek 4 bytes (no pointer move) |
| `size_t` | `self->read(ByteBuffer* self, void* buf, size_t len)` | Read byte array |
| `void` | `self->compact(ByteBuffer* self)` | Remove read data, reclaim space |
| `void` | `self->rewind(ByteBuffer* self)` | Reset read pointer |
| `void` | `self->skip(ByteBuffer* self, size_t len)` | Advance read pointer |
| `ssize_t` | `self->indexOf(ByteBuffer* self, uint8_t target)` | Find byte position |
| `size_t` | `self->remaining(ByteBuffer* self)` | Readable bytes remaining |
| `size_t` | `self->writableBytes(ByteBuffer* self)` | Writable bytes available |
| `[OWNED] ByteBuffer*` | `self->readSlice(ByteBuffer* self, size_t len)` | Extract slice |

---

## 📦 ring_buffer — `ring_buffer.h / ring_buffer.c`

```
Object
└── RingBuffer
```

High-performance ring buffer. Thread-Safe, blocking popWait support.

> `push` returns `false` when full — designed to avoid EventLoop blocking.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] RingBuffer*` | `new_RingBuffer(size_t capacity)` | Create ring buffer |
| `bool` | `self->push(RingBuffer* self, void* item)` | Push item (false if full) |
| `void*` | `self->pop(RingBuffer* self)` | Pop item (NULL if empty) |
| `void*` | `self->popWait(RingBuffer* self, int timeout_ms)` | Blocking pop with timeout |
| `size_t` | `self->getSize(RingBuffer* self)` | Current item count |
| `bool` | `self->isEmpty(RingBuffer* self)` | Check if empty |
| `bool` | `self->isFull(RingBuffer* self)` | Check if full |
| `void` | `self->clear(RingBuffer* self)` | Remove all items |

---

## 📦 socket_base — `socket_base.h / socket_base.c`

```
Object
└── Socket  (abstract base)
    ├── TcpSocket
    ├── UdpSocket
    └── UnixSocket
```

Abstract socket base class. Common interface for TCP/UDP/Unix.

> Must handle `SOCKET_WOULD_BLOCK` return — edge-trigger loop exit required.

| Return Type | Signature | Description |
|---|---|---|
| `void` | `Socket_init_base(Socket* self, int fd, SocketProtocol protocol)` | Initialize socket base |
| `ssize_t` | `self->send(Socket* self, const void* buf, size_t len, const char* host, int port)` | Send data |
| `ssize_t` | `self->recv(Socket* self, void* buf, size_t len, char* host, int* port)` | Receive data |
| `int` | `self->getFD(Socket* self)` | Get file descriptor |
| `void` | `self->close(Socket* self)` | Close socket |
| `int` | `self->bind(Socket* self, const char* host, int port)` | Bind to address |
| `int` | `self->listen(Socket* self, int backlog)` | Listen for connections |
| `int` | `self->connect(Socket* self, const char* host, int port)` | Connect to server |

---

## 📦 tcp_socket — `tcp_socket.h / tcp_socket.c`

```
Object
└── Socket
    └── TcpSocket
```

TCP socket. Server/client creation, accept support.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] TcpSocket*` | `new_TcpServer(const char* host, int port)` | Create TCP server socket + bind + listen |
| `[OWNED] TcpSocket*` | `new_TcpClient(const char* host, int port)` | Create TCP client socket + connect |
| `[OWNED] TcpSocket*` | `new_TcpSocket_from_fd(int fd)` | Wrap existing fd |
| `[OWNED] TcpSocket*` | `self->accept(TcpSocket* self, char* ip, int* port)` | Accept client connection |

---

## 📦 udp_socket — `udp_socket.h / udp_socket.c`

```
Object
└── Socket
    └── UdpSocket
```

UDP socket. Server/client creation.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] UdpSocket*` | `new_UdpServer(const char* host, int port)` | Create UDP server socket + bind |
| `[OWNED] UdpSocket*` | `new_UdpClient(void)` | Create UDP client socket |
| `[OWNED] UdpSocket*` | `new_UdpSocket_from_fd(int fd)` | Wrap existing fd |

---

## 📦 unix_socket — `unix_socket.h / unix_socket.c`

```
Object
└── Socket
    └── UnixSocket
```

Unix domain socket. Inter-process communication (IPC).

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] UnixSocket*` | `new_UnixServer(const char* path)` | Create Unix server socket + bind + listen |
| `[OWNED] UnixSocket*` | `new_UnixClient(const char* path)` | Create Unix client socket + connect |
| `[OWNED] UnixSocket*` | `new_UnixSocket_from_fd(int fd)` | Wrap existing fd |
| `[OWNED] UnixSocket*` | `self->accept(UnixSocket* self, char* path)` | Accept client connection |

---

## 📦 event_loop — `event_loop.h / event_loop.c`

```
Object
└── EventLoop
```

epoll-based event loop. EPOLLET edge-trigger, socket + timer integration.

> `volatile bool is_running` — immediate SIGINT detection. Auto EINTR retry.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] EventLoop*` | `new_EventLoop(int max_events)` | Create event loop |
| `void` | `event_loop_run(EventLoop* self)` | Run event loop (blocking) |
| `int` | `event_loop_add_timer(EventLoop* self, Timer* timer)` | Register timer |
| `int` | `event_loop_remove_timer(EventLoop* self, Timer* timer)` | Unregister timer |
| `int` | `self->addSocket(EventLoop* self, Socket* sock, EventMask mask)` | Register socket |
| `int` | `self->delSocket(EventLoop* self, Socket* sock)` | Unregister socket |
| `int` | `self->poll(EventLoop* self, int timeout_ms)` | Single poll (non-blocking) |
| `void` | `self->run(EventLoop* self)` | Run event loop |
| `void` | `self->stop(EventLoop* self)` | Stop event loop |

---

## 📦 timer — `timer.h / timer.c`

```
Object
└── Timer
```

timerfd-based timer. Repeating/one-shot. EventLoop integration.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Timer*` | `new_Timer(long interval_ms, bool repeating, TimerCallback cb, void* ud)` | Create timer |
| `[OWNED] Timer*` | `new_TimerNamed(const char* name, long ms, bool repeating, TimerCallback cb, void* ud)` | Create named timer |
| `void` | `on_timer_event(Timer* self)` | Handle timer event (called by EventLoop) |
| `bool` | `self->start(Timer* self)` | Start timer |
| `void` | `self->stop(Timer* self)` | Stop timer |
| `void` | `self->reset(Timer* self)` | Reset timer |
| `bool` | `self->isActive(Timer* self)` | Check if timer is active |

---

## 📦 scheduler — `scheduler.h / scheduler.c`

```
Object
└── Scheduler
    ├── [uses] ThreadPool
    └── [uses] EventLoop
```

timerfd + epoll + ThreadPool integrated scheduler.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Scheduler*` | `new_Scheduler(ThreadPool* pool, EventLoop* loop)` | Create scheduler |
| `bool` | `self->add(Scheduler* self, const char* name, long ms, bool repeat, TimerCallback cb, void* ud)` | Register job |
| `bool` | `self->addEx(Scheduler* self, const char* name, long ms, bool repeat, JobPriority prio, TimerCallback cb, void* ud)` | Register job with priority |
| `bool` | `self->remove(Scheduler* self, const char* name)` | Remove job by name |
| `void` | `self->start(Scheduler* self)` | Start scheduler |
| `void` | `self->stop(Scheduler* self)` | Stop scheduler |
| `size_t` | `self->count(Scheduler* self)` | Registered job count |

---

## 📦 context — `context.h / context.c`

```
Object
└── Context
```

Key-value runtime context. Thread-Safe.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Context*` | `new_Context(void)` | Create context |
| `void` | `self->set(Context* self, const char* key, Object* value)` | Store key-value |
| `[BORROWED] Object*` | `self->get(Context* self, const char* key)` | Get value by key |
| `[OWNED] Object*` | `self->remove(Context* self, const char* key)` | Remove key-value |
| `bool` | `self->has(Context* self, const char* key)` | Check key existence |
| `void` | `self->clear(Context* self)` | Remove all entries |
| `int` | `self->getSize(Context* self)` | Stored entry count |
| `void` | `self->setString(Context* self, const char* key, const char* val)` | Store string value |
| `[BORROWED] String*` | `self->getString(Context* self, const char* key)` | Get string value |
| `void` | `self->setInt(Context* self, const char* key, int val)` | Store integer value |
| `int` | `self->getInt(Context* self, const char* key)` | Get integer value |

---

## 📦 config — `config.h / config.c`

```
Object
└── Config
```

INI/config file loader. HashMap-based.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Config*` | `new_Config(void)` | Create config object |
| `bool` | `self->load(Config* self, const char* path)` | Load config file |
| `const char*` | `self->get(Config* self, const char* key)` | Get value (may return NULL) |
| `const char*` | `self->getString(Config* self, const char* key, const char* def)` | Get string with default |
| `int` | `self->getInt(Config* self, const char* key, int def)` | Get integer with default |
| `bool` | `self->getBool(Config* self, const char* key, bool def)` | Get bool with default |

---

## 📦 service_registry — `service_registry.h / service_registry.c`

```
Object
└── ServiceRegistry
```

DI container. Class type-based service registration/lookup.

> Macro: `REG_GET(ClassName)` for convenient lookup.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] ServiceRegistry*` | `new_ServiceRegistry(void)` | Create service registry |
| `void` | `self->register_s(ServiceRegistry* self, const Class* cls, Object* service)` | Register service |
| `[BORROWED] Object*` | `self->get(ServiceRegistry* self, const Class* cls)` | Get service by type |
| `bool` | `self->has(ServiceRegistry* self, const Class* cls)` | Check if service registered |
| `void` | `self->unregister(ServiceRegistry* self, const Class* cls)` | Unregister service |

---

## 📦 app_context — `app_context.h / app_context.c`

```
Object
└── AppContext
    ├── [owns] Config
    ├── [owns] Context
    └── [owns] ServiceRegistry
```

Unified application context. Integrates Config + Context + ServiceRegistry.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] AppContext*` | `new_AppContext(void)` | Create app context |
| `bool` | `self->init(AppContext* self)` | Initialize |
| `void` | `self->destroy_all(AppContext* self)` | Release all resources |
| `void` | `self->setConfig(AppContext* self, const char* key, const char* val)` | Store config value |
| `[BORROWED] String*` | `self->getConfig(AppContext* self, const char* key)` | Get config value |
| `int` | `self->getConfigInt(AppContext* self, const char* key)` | Get integer config |
| `void` | `self->registerService(AppContext* self, const Class* cls, Object* service)` | Register service |
| `[BORROWED] Object*` | `self->getService(AppContext* self, const Class* cls)` | Get service |

---

## 📦 db / mysql — `db.h / db.c / mysql.c`

```
Object
└── DBClient
    └── [driver] MySQLDriver  (injected via bind_mysql)
```

MySQL/MariaDB ARC client. Thread-Safe Snapshot, Zero-Malloc DBOption, 35 functions.

> `escape_string()` return value must be `free()`d.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] DBClient*` | `new_DBClient(void)` | Create client from dbconfig.conf |
| `[OWNED] DBClient*` | `new_DBClientDirect(host, dbname, id, pw, port, cs, type)` | Create client with direct params |
| `void` | `self->setOption(DBClient* self, int option, const void* arg, size_t size)` | Set DB option (Lazy Init) |
| `int` | `self->connect(DBClient* self)` | Connect to DB |
| `int` | `self->reconnect(DBClient* self)` | Reconnect (auto-restores options) |
| `void` | `self->disconnect(DBClient* self)` | Disconnect from DB |
| `int` | `self->sqlQuery(DBClient* self, const char* sql)` | Execute raw SQL |
| `char*` | `self->escape_string(DBClient* self, const char* str)` | Escape SQL string — must `free()` |
| `int` | `self->beginTransaction(DBClient* self)` | Begin transaction |
| `int` | `self->commit(DBClient* self)` | Commit transaction |
| `int` | `self->rollback(DBClient* self)` | Rollback transaction |
| `int` | `self->insertTable(DBClient* self, const char* table, HashMap* data)` | Insert row |
| `int` | `self->updateTable(DBClient* self, const char* table, HashMap* data, const char* cond)` | Update rows |
| `int` | `self->replaceTable(DBClient* self, const char* table, HashMap* data)` | Replace row |
| `int` | `self->deleteTable(DBClient* self, const char* table, const char* cond)` | Delete rows by condition |
| `HashMap*` | `self->getRecordFromQuery(DBClient* self, const char* sql)` | Query single row |
| `ArrayList*` | `self->getRecordsFromQuery(DBClient* self, const char* sql)` | Query multiple rows |
| `HashMap*` | `self->getRecord(DBClient* self, const char* table, const char* cond, const char* field)` | Get single row by condition |
| `ArrayList*` | `self->getRecords(DBClient* self, const char* table, const char* cond, const char* fields)` | Get multiple rows by condition |
| `int` | `self->getDataCount(DBClient* self, const char* table, const char* cond)` | Count rows by condition |
| `long long` | `self->getDataSum(DBClient* self, const char* table, const char* field, const char* cond)` | Sum field values |
| `long long` | `self->getTableSize(DBClient* self, const char* table)` | Get table size |
| `int` | `self->table_exists(DBClient* self, const char* table)` | Check table existence |
| `int` | `self->dropTable(DBClient* self, const char* table_name)` | Drop table |
| `ArrayList*` | `self->descTable(DBClient* self, const char* table)` | Get table schema |

---

## 📦 crypto — `crypto.h / crypto.c`

```
Object
├── Hasher
└── Cipher
```

Crypto module. Hasher (one-way) + Cipher (two-way) + Base64. OpenSSL EVP wrapper.

> `Base64_encode()` / `Base64_decode()` return values must be `free()`d.

| Return Type | Signature | Description |
|---|---|---|
| `[OWNED] Hasher*` | `new_Hasher(const char* algo)` | Create hasher (SHA-256, SHA-512, etc.) |
| `[OWNED] String*` | `hasher->hash(Hasher* self, const char* plain_text)` | Compute hash |
| `bool` | `hasher->verify(Hasher* self, const char* plain, const char* hashed)` | Verify hash |
| `[OWNED] Cipher*` | `new_Cipher(const char* algo)` | Create cipher (AES-256-CBC, etc.) |
| `bool` | `cipher->init(Cipher* self, const uint8_t* key, size_t klen, const uint8_t* iv, size_t ilen)` | Initialize key/IV |
| `[OWNED] ByteBuffer*` | `cipher->encrypt(Cipher* self, const uint8_t* data, size_t len)` | Encrypt data |
| `[OWNED] ByteBuffer*` | `cipher->decrypt(Cipher* self, const uint8_t* data, size_t len)` | Decrypt data |
| `char*` | `Base64_encode(const uint8_t* data, size_t len)` | Base64 encode (OpenSSL) — must `free()` |
| `uint8_t*` | `Base64_decode(const char* base64_str, size_t* out_len)` | Base64 decode (OpenSSL) — must `free()` |
| `void` | `Crypto_SHA1(const uint8_t* data, size_t len, uint8_t out_hash[20])` | SHA-1 hash (no external deps) |
| `char*` | `Crypto_Base64Encode(const uint8_t* data, size_t len)` | Base64 encode (pure C) — must `free()` |

---

## 📦 ws_protocol — `ws_protocol.h / ws_protocol.c`

```
(static functions — no instance)
ws_protocol
```

WebSocket protocol handling. Handshake, frame encoding/decoding.

> `ws_compute_accept_key()` return value must be `free()`d.

| Return Type | Signature | Description |
|---|---|---|
| `char*` | `ws_compute_accept_key(const char* client_key)` | Generate WebSocket Accept key — must `free()` |
| `size_t` | `ws_build_text_frame(const char* msg, uint8_t* out_buf, size_t max_len)` | Build text frame (server→client) |
| `ssize_t` | `ws_decode_frame(const uint8_t* in_buf, size_t in_len, char* out_msg, size_t max_out)` | Decode client frame + unmask |

---

**43 Modules | Valgrind 0 bytes | TSan 0 warnings | MIT License | 철컥. 🔫**
