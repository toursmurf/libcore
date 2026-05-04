# libcore — C Server Runtime Framework

> **EventLoop + ThreadPool 기반, C 서버를 100줄로.**
> **Build a C server in 100 lines with EventLoop + ThreadPool.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Valgrind](https://img.shields.io/badge/Valgrind-0%20bytes-brightgreen)](/)
[![Platform](https://img.shields.io/badge/Platform-Linux%2064bit-lightgrey)](/)
[![Version](https://img.shields.io/badge/Version-v1.0%20Iron%20Fortress-orange)](/)

---

## 한 줄로

```
🇰🇷 C99 기반 서버 런타임 프레임워크.
     EventLoop + ThreadPool + ARC 메모리 관리로
     고성능 서버를 빠르게 만들 수 있습니다.

🇬🇧 A C99 server runtime framework.
     Build high-performance servers quickly with
     EventLoop + ThreadPool + ARC memory management.
```

---

## 이렇게 씁니다 / This is how you use it

```c
/* TCP 에코 서버 — 20줄 / TCP Echo Server — 20 lines */

#include "libcore.h"

static void on_client(Socket* self, void* loop_ptr) {
    char buf[1024];
    ssize_t n = self->recv(self, buf, sizeof(buf), NULL, NULL);
    if (n > 0) self->send(self, buf, n, NULL, 0);
    else {
        ((EventLoop*)loop_ptr)->delSocket((EventLoop*)loop_ptr, self);
        RELEASE((Object*)self);
    }
}

static void on_accept(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    TcpSocket* client = ((TcpSocket*)self)->accept((TcpSocket*)self, NULL, NULL);
    if (client) {
        client->base.on_readable = on_client;
        loop->addSocket(loop, (Socket*)client, EV_READ);
        RELEASE((Object*)client);
    }
}

int main(void) {
    EventLoop* loop   = new_EventLoop(1024);
    TcpSocket* server = new_TcpServer("0.0.0.0", 8080);
    server->base.on_readable = on_accept;
    loop->addSocket(loop, (Socket*)server, EV_READ);
    loop->run(loop);                          /* 블로킹 / blocking */
    RELEASE((Object*)server);
    RELEASE((Object*)loop);
    return 0;
}
```

---

## 왜 libcore인가 / Why libcore

```
🇰🇷
C 언어로 서버를 만들 때 반복되는 문제들:
→ epoll 직접 관리 → 복잡하고 실수하기 쉬움
→ 스레드 풀 직접 구현 → 매번 같은 코드
→ 메모리 관리 → free() 타이밍 실수 → 누수
→ 소켓 추상화 없음 → TCP/UDP/Unix 각각 따로

libcore 는 이 문제들을 해결합니다.

🇬🇧
When building servers in C, you face the same problems repeatedly:
→ Managing epoll directly — complex and error-prone
→ Implementing thread pools every time — same boilerplate
→ Memory management — wrong free() timing → leaks
→ No socket abstraction — TCP/UDP/Unix all separate

libcore solves these problems.
```

---

## 핵심 구성 / Core Components

```
┌─────────────────────────────────────────────────────┐
│                    libcore v1.0                     │
├─────────────────────────────────────────────────────┤
│                                                     │
│  ┌──────────────┐    ┌──────────────────────────┐   │
│  │  EventLoop   │◄───│  Socket                  │   │
│  │  (epoll)     │    │  TcpSocket               │   │
│  │              │◄───│  UdpSocket               │   │
│  │              │◄───│  UnixSocket              │   │
│  └──────┬───────┘    └──────────────────────────┘   │
│         │                                           │
│         │            ┌──────────────────────────┐   │
│         └───────────►│  Timer / Scheduler       │   │
│                      └──────────────────────────┘   │
│                                                     │
│  ┌──────────────┐    ┌──────────────────────────┐   │
│  │  ThreadPool  │    │  ARC Memory              │   │
│  │  Thread      │    │  RETAIN / RELEASE        │   │
│  │  Semaphore   │    │  Valgrind 0 bytes        │   │
│  └──────────────┘    └──────────────────────────┘   │
│                                                     │
│  Collections  String  JSON  Logger  Crypto  ...     │
└─────────────────────────────────────────────────────┘
```

---

## 주요 특징 / Key Features

| 특징 / Feature | 설명 / Description |
|---|---|
| **EventLoop** | epoll 기반 단일 스레드 비동기 I/O | epoll-based single-thread async I/O |
| **ARC 메모리** | RETAIN/RELEASE 기반 자동 메모리 관리 | Auto memory via RETAIN/RELEASE |
| **Socket 추상화** | TCP/UDP/Unix 통일 인터페이스 | Unified TCP/UDP/Unix interface |
| **ThreadPool** | 작업 큐 기반 스레드 풀 | Task queue-based thread pool |
| **Valgrind 0 bytes** | 메모리 누수 없음 보장 | Zero memory leaks guaranteed |
| **44개 모듈** | 컬렉션/파일/암호화/JSON 등 포함 | Collections/File/Crypto/JSON etc. |
| **C99 순수 C** | 외부 의존성 없음 (MySQL 선택적) | No external deps (MySQL optional) |

---

## 빠른 시작 / Quick Start

### 1. 빌드 / Build

```bash
git clone https://github.com/toursmurf/libcore.git
cd libcore
make examples
```

### 2. 예제 실행 / Run Examples

```bash
# TCP 에코 서버 / TCP echo server
./examples/arc_echo_server

# 멀티 프로토콜 리액터 / Multi-protocol reactor
./examples/arc_reactor_multi_server

# 통합 테스트 / Integration test
./examples/all_test_v2
```

### 3. MySQL 연동 (선택) / MySQL Integration (optional)

```bash
# 라이브러리 경로 확인 / Check library path
mysql_config --libs
mysql_config --include

# MySQL 포함 빌드 / Build with MySQL
make WITH_MYSQL=1 \
  MYSQL_INC=$(mysql_config --variable=pkgincludedir) \
  MYSQL_LIB=$(mysql_config --variable=pkglibdir)
```

→ 자세한 내용: [docs/mysql_setup.md](docs/mysql_setup.md)

---

## 사용 시나리오 / Use Cases

```
🇰🇷 이런 것을 만들 때 씁니다:

✔ TCP/UDP 서버 (단일 EventLoop, 다중 클라이언트)
✔ IPC 서버 (Unix Domain Socket)
✔ 멀티 프로토콜 서버 (TCP + UDP + Unix 동시)
✔ 주기적 작업 서버 (Scheduler + ThreadPool)
✔ 고성능 데이터 수집기 (RingBuffer + DbWriter)

🇬🇧 Build these with libcore:

✔ TCP/UDP servers (single EventLoop, multiple clients)
✔ IPC servers (Unix Domain Socket)
✔ Multi-protocol servers (TCP + UDP + Unix simultaneously)
✔ Periodic task servers (Scheduler + ThreadPool)
✔ High-performance data collectors (RingBuffer + DbWriter)
```

---

## ARC 메모리 규칙 / ARC Memory Rules

```c
/*
 * 🇰🇷 3가지만 기억하세요:
 * 🇬🇧 Just remember 3 rules:
 *
 * 1. new_xxx() 생성 시 ref_count = 1 자동
 *    new_xxx() sets ref_count = 1 automatically
 *
 * 2. 컨테이너에 넣기 전 RETAIN, 다 쓰면 RELEASE
 *    RETAIN before storing, RELEASE when done
 *
 * 3. [OWNED] 반환값은 RELEASE 필수
 *    [BORROWED] 반환값은 RELEASE 금지
 *    Must RELEASE [OWNED], never RELEASE [BORROWED]
 */

ArrayList* list = new_ArrayList(10);         /* ref=1 */
String*    str  = new_String("hello");       /* ref=1 */

list->add(list, (Object*)str);               /* 내부 RETAIN → ref=2 */
RELEASE((Object*)str);                       /* ref=1, list 가 소유 */

String* item = (String*)list->get(list, 0); /* [BORROWED] RELEASE 금지 */

RELEASE((Object*)list);                      /* list + str 전부 소각 */
```

---

## 모듈 구성 / Module Overview

```
Collections   ArrayList / HashMap / Hashtable / Queue / Stack
              Vector / List / LinkedList / BTree / Tree / JSON

Concurrency   Thread / ThreadPool / Semaphore
              RingBuffer / Mutex / RWLock / CondVar

Network       Socket / TcpSocket / UdpSocket / UnixSocket
              EventLoop / Timer / Scheduler / ByteBuffer

File/IO       Path / File / Directory / FileWatcher / MappedFile
              FileUtil

Application   AppContext / Context / Config / ServiceRegistry

Utilities     Logger / AsyncLogger / Exception
              Crypto (Hasher/Cipher/Base64) / ws_protocol / String
```

---

## 예제 목록 / Examples

| 파일 / File | 설명 / Description |
|---|---|
| `arc_echo_server.c` | TCP 에코 서버 / TCP Echo Server |
| `arc_reactor_multi_server.c` | TCP+UDP+Unix 멀티플렉싱 / Multiplexing |
| `arc_chat_server.c` | WebSocket 채팅 서버 / WebSocket Chat |
| `arc_killer_demo.c` | 고부하 성능 데모 / High-load Demo |
| `arc_thread_test.c` | ThreadPool 동작 검증 / ThreadPool test |
| `arc_scheduler_system_monitor.c` | 주기 모니터링 / Periodic monitoring |
| `arc_json_test.c` | JSON 파서 / JSON parser |
| `arc_mysql_test.c` | MySQL 연동 / MySQL integration |
| `all_test_v2.c` | 전체 통합 테스트 / Full integration test |

→ 전체 목록: [docs/examples_guide.md](docs/examples_guide.md)

---

## 문서 / Documentation

| 문서 / Document | 내용 / Content |
|---|---|
| [docs/libcore_api_ko.md](docs/libcore_api_ko.md) | API 레퍼런스 한글 / Korean API reference |
| [docs/libcore_api_en.md](docs/libcore_api_en.md) | API Reference English |
| [docs/libcore_v1_class_diagram.md](docs/libcore_v1_class_diagram.md) | 클래스 다이어그램 / Class diagram |
| [docs/mysql_setup.md](docs/mysql_setup.md) | MySQL 연동 / MySQL setup |
| [docs/examples.ko.md](docs/examples.ko.md) | 예제 가이드 한글  |
| [docs/examples.en.md](docs/examples.en.md) |  Examples guide english |

---

## 요구사항 / Requirements

```
OS       : Linux 64-bit (Ubuntu / Rocky / Debian)
Compiler : GCC 9+ / Clang 10+
Make     : GNU Make
Optional : libmysqlclient (MySQL 연동 시 / for MySQL)
```

---

## 라이선스 / License

MIT License — 자유롭게 사용, 수정, 배포 가능.
MIT License — Free to use, modify, and distribute.

---

## 링크 / Links
- Author : INDONG KIM(김인동) - idong322@naver.com
- **GitHub**: https://github.com/toursmurf/libcore
- **Homepage**: https://toos.it
- **Issues**: https://github.com/toursmurf/libcore/issues

---

*libcore is a high-performance, event-driven server runtime in pure C.*
*Java-like API / Python-like usability / C-level performance / ARC memory safety.*
*Valgrind clean / Zero-Malloc / Graceful shutdown / MIT License.*

**Toos IT Holdings | Iron Fortress v1.0 | 철컥. 🔫**
