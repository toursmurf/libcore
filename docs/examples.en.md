🇺🇸 Libcore v1.0 Examples Guide

This directory contains 34 practical example codes demonstrating the usage and architecture of each module in the libcore framework. All examples strictly adhere to the ARC (Automatic Reference Counting) memory management rules.

🌐 Network & EventLoop

arc_echo_server.c / arc_echo_client.c: Basic TCP Echo server and client implementation.

arc_udp_server.c / arc_udp_client.c: Data transmission examples using UDP sockets.

arc_unix_server.c / arc_unix_client.c: Unix Domain Socket usage for Inter-Process Communication (IPC).

arc_reactor_tcp_unix_server.c: A multiplexing server handling both TCP and Unix sockets simultaneously using the EventLoop.

arc_reactor_multi_server.c: A high-performance Reactor pattern demo that processes various communication protocols non-blocking on a single EventLoop thread.

arc_chat_server.c: An EventLoop-based TCP chat server implementation supporting multiple connections and broadcasting.

🗄️ Data Structures & Collections

arc_hashmap_arraylist_hashtable_test.c: Integration test for data manipulation and ARC ownership management in core collections (HashMap, ArrayList, Hashtable).

arc_vector_test.c / arc_list_test.c / arc_linkedlist_test.c: Validation of dynamic arrays (Vector) and doubly/singly linked lists.

arc_queue_test.c / arc_stack_test.c: Tests for FIFO Queue and LIFO Stack collections.

arc_tree_test.c / arc_btree_test.c: Data sorting and search validation for Binary Search Tree (BST) and B-Tree.

arc_byte_buffer_test.c: Binary manipulation and endianness processing test using Java NIO-style ByteBuffer.

arc_ringbuffer_test.c: A high-performance RingBuffer communication example optimized for multi-threaded environments.

⚙️ System & Core Logic

arc_thread_test.c: Validation of POSIX thread wrappers, Mutex, CondVar, and ThreadPool for concurrency processing.

arc_app_context_main.c: Demo of application lifecycle management, Dependency Injection (DI), and Service Registry via AppContext.

arc_config_test.c: INI configuration file loading and Config module parsing test.

arc_scheduler_system_monitor.c: A demo that periodically monitors system resources using the Scheduler.

arc_scheduler_integration_sentinel.c: A Sentinel process example combining advanced timers and schedulers.

arc_cron_shared_primitive_Integrated.c: An integrated test preview of the CronScheduler, SharedMemory (IPC), and Primitive Wrapper modules scheduled for the upcoming v1.1 update.

📁 File, Data & Utilities

arc_file_system_test.c: File system manipulation using Path, File, and Directory objects, along with FileWatcher (inotify) monitoring test.

arc_json_test.c: Validation of the built-in JSON parser and serialization/deserialization (ObjectMapper) features.

arc_log_exception_integration_test.c: Integrated demo of file/console loggers, AsyncLogger, and ErrorCode-based Exception handling.

arc_crypto_integration_test.c: Integrated validation of OpenSSL-based AES encryption/decryption, SHA hashing, and Base64 encoding/decoding.

arc_mysql_test.c: MySQL/MariaDB client integration, SQL query execution, and transaction rollback/commit test.

arc_regex_test.c: Test for the regular expression matching features within the String object.

🚀 Integration Demos & Clients

all_test_v2.c: The most comprehensive, enterprise-grade integration test suite that links all 40+ modules of the libcore framework to verify integrity and memory leaks (Valgrind).

arc_news_crawler.c: A practical web crawler application demo combining socket communication, string processing, and data structures.

index.html: A client web page for visually verifying ws_protocol (WebSocket) communications or API test results in a browser.

arc_board_server.c: 3RDB (MySQL / PostgreSQL / SQLITE)  Web Board Demo Server Program. 