#ifndef TYPE_INFO_H
#define TYPE_INFO_H

/* =================================================================
 * [Toos IT Holdings] libcore 전역 Type ID (고유 식별자) 명세서
 * ================================================================= */
typedef enum {
    TYPE_OBJECT = 0,

    /* ── 1. Primitive & String ── */
    TYPE_STRING,         // string_obj.c

    /* ── 2. Linear Collections ── */
    TYPE_ARRAYLIST,      // arraylist.c
    TYPE_VECTOR,         // vector.c
    TYPE_LINKEDLIST,     // linked_list.c
    TYPE_LIST,           // list.c (인터페이스형)

    /* ── 3. Adapters (LIFO / FIFO) ── */
    TYPE_STACK,          // stack.c
    TYPE_QUEUE,          // queue.c

    /* ── 4. Map & Hash Collections ── */
    TYPE_HASHMAP,        // hashmap.c
    TYPE_HASHTABLE,      // hashtable.c

    /* ── 5. Tree Collections ── */
    TYPE_TREE,           // tree.c (BST)
    TYPE_BTREE,          // btree.c

    /* ── 6. Concurrency & Threading ── */
    TYPE_THREAD,         // thread.c
    TYPE_THREADPOOL,     // threadpool.c
    TYPE_SEMAPHORE,      // semaphore_obj.c

    /* ── 7. JSON Engine ── */
    TYPE_JSON_NODE,      // json.c
    TYPE_JSON_VALUE,     // json.c

    /* ============================================================
     * ▼ 여기서부터 Iron Fortress 추가 객체들 업데이트 완료! ▼
     * ============================================================ */

    /* ── 8. Buffers & I/O ── */
    TYPE_BYTEBUFFER,     // bytebuffer.c
    TYPE_RINGBUFFER,     // ring_buffer.c

    /* ── 9. File System & Path ── */
    TYPE_PATH,           // path.c
    TYPE_FILE,           // file.c
    TYPE_MAPPED_FILE,    // mapped_file.c (mmap 기반 고성능 파일 처리)
    TYPE_FILE_WATCHER,   // file_watcher.c (inotify 기반 파일 감시)
    TYPE_ASYNC_FILE,     // async_file.c (io_uring/AIO 기반 비동기 I/O)
    TYPE_DIRECTORY,      // directory.c

    /* ── 10. Networking 계층 ── */
    TYPE_SOCKET,         // socket_base.c (Abstract)
    TYPE_TCPSOCKET,      // tcp_socket.c
    TYPE_UDPSOCKET,      // udp_socket.c
    TYPE_UNIXSOCKET,     // unix_socket.c

    /* ── 11. Async & Event Engine ── */
    TYPE_EVENTLOOP,      // event_loop.c
    TYPE_TIMER,          // timer.c
    TYPE_SCHEDULER,      // scheduler.c
    TYPE_CRON_SCHEDULER,      // cron_scheduler.c

    /* ── 12. App Context & DI Container ── */
    TYPE_CONTEXT,        // context.c
    TYPE_CONFIG,         // config.c
    TYPE_SERVICE_REGISTRY, // service_registry.c
    TYPE_APP_CONTEXT,    // app_context.c

    /* ── 13. Logging & Exception ── */
    TYPE_LOGGER,         // logger.c
    TYPE_ASYNC_LOGGER,   // async_logger.c
    TYPE_EXCEPTION,      // exception.c

    /* ── 14. Crypto & Security ── */
    TYPE_HASHER,         // crypto.c (MD5, SHA)
    TYPE_CIPHER,         // crypto.c (AES)

		/* ── 15. database ── */
    TYPE_DBCLIENT,          // db.c

		/* 16. shared memory */
		TYPE_SHARED_MEMORY, //shared_memory.c

		/* 17. primitive */
		TYPE_INTEGER, //primitive.c
		TYPE_LONG, //primitive.c
		TYPE_DOUBLE, //primitive.c
		TYPE_BOOLEAN, //primitive.c
		TYPE_BYTE, //primitive.c

    /* ============================================================
     * 사용자 정의(User-Defined) 영역: 1000번부터 시작!
     * (코어 모듈이 계속 늘어날 것을 대비하여 오프셋을 크게 밀었습니다)
     * ============================================================ */
    TYPE_SENSOR_DATA = 1000,
    TYPE_USER_DEFINED = 1001

} TypeId;

#endif // TYPE_INFO_H