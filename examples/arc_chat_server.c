/**
 * @file arc_chat_server.c
 * @brief 🇰🇷 EventLoop 기반의 다중 접속 및 브로드캐스팅을 지원하는 TCP/WebSocket 채팅 서버 구현체입니다.
 * 🇬🇧 TCP/WebSocket chat server implementation supporting multi-connection and broadcasting based on EventLoop.
 * @note  This example strictly follows the ARC memory management rules.
 */

// ============================================================================
// 🚨 TARGET OS: 64-bit Linux Only (32-bit not supported) 🚨
// 🛡️ ARCHITECTURE: libcore Standard Object System Applied
// ============================================================================
#include "event_loop.h"
#include "tcp_socket.h"
#include "logger.h"
#include "ws_protocol.h"
#include "arraylist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

static EventLoop* g_loop = NULL;

// 🏛️ [의장님 검수 1] ClientSession용 libcore Class 정의
typedef struct {
    Object base;      // libcore 객체 지향의 심장
    TcpSocket* sock;  // 실제 소켓
    char uin[16];     // 닉네임 매핑용 UIN
} ClientSession;

static void ClientSession_onRelease(Object* obj) {
    ClientSession* s = (ClientSession*)obj;
    if (s->sock) {
        RELEASE((Object*)s->sock);
        s->sock = NULL;
    }
}

// libcore 표준 Class 메타데이터
static const Class _sessionClass = {
    .name     = "ClientSession",
    .size     = sizeof(ClientSession),
    .finalize = ClientSession_onRelease
};

// 🛠️ ClientSession 생성자: Object_Init 규격 준수
ClientSession* new_ClientSession(TcpSocket* sock) {
    ClientSession* s = (ClientSession*)malloc(sizeof(ClientSession));
    if (!s) return NULL;

    // ✅ libcore 표준 API: Object_Init 사용
    Object_Init((Object*)s, &_sessionClass);

    RETAIN((Object*)sock);
    s->sock = sock;
    memset(s->uin, 0, 16);
    return s;
}

typedef struct {
    ArrayList* sessions;
    pthread_mutex_t lock;
} ClientManager;
static ClientManager manager;

// --- ClientManager (세션 관리부) ---
void ClientManager_Init() {
    manager.sessions = new_ArrayList(10);
    pthread_mutex_init(&manager.lock, NULL);
}

void ClientManager_Destroy() {
    pthread_mutex_lock(&manager.lock);
    if (manager.sessions) {
        RELEASE((Object*)manager.sessions);
        manager.sessions = NULL;
    }
    pthread_mutex_unlock(&manager.lock);
    pthread_mutex_destroy(&manager.lock);
}

void ClientManager_Add(TcpSocket* client) {
    pthread_mutex_lock(&manager.lock);
    ClientSession* s = new_ClientSession(client);
    if (s) {
        manager.sessions->add(manager.sessions, (Object*)s);
        RELEASE((Object*)s); // ArrayList가 소유권을 가짐
    }
    pthread_mutex_unlock(&manager.lock);
}

void ClientManager_Remove(TcpSocket* client) {
    pthread_mutex_lock(&manager.lock);
    if (!manager.sessions) { pthread_mutex_unlock(&manager.lock); return; }
    size_t size = manager.sessions->getSize(manager.sessions);
    for (size_t i = 0; i < size; i++) {
        ClientSession* s = (ClientSession*)manager.sessions->get(manager.sessions, i);
        if (s->sock == client) {
            manager.sessions->remove(manager.sessions, i);
            break;
        }
    }
    pthread_mutex_unlock(&manager.lock);
}

void ClientManager_UpdateUIN(TcpSocket* client, const char* uin) {
    pthread_mutex_lock(&manager.lock);
    size_t size = manager.sessions->getSize(manager.sessions);
    for (size_t i = 0; i < size; i++) {
        ClientSession* s = (ClientSession*)manager.sessions->get(manager.sessions, i);
        if (s->sock == client) {
            strncpy(s->uin, uin, 15);
            break;
        }
    }
    pthread_mutex_unlock(&manager.lock);
}

void ClientManager_Broadcast(const char* msg) {
    uint8_t frame[8192];
    size_t frame_len = ws_build_text_frame(msg, frame, sizeof(frame));
    if (frame_len == 0) return;

    pthread_mutex_lock(&manager.lock);
    size_t count = manager.sessions->getSize(manager.sessions);
    if (count == 0) { pthread_mutex_unlock(&manager.lock); return; }

    TcpSocket** snapshot = malloc(sizeof(TcpSocket*) * count);
    for (size_t i = 0; i < count; i++) {
        ClientSession* s = (ClientSession*)manager.sessions->get(manager.sessions, i);
        snapshot[i] = s->sock;
        RETAIN((Object*)snapshot[i]);
    }
    pthread_mutex_unlock(&manager.lock);

    for (size_t i = 0; i < count; i++) {
        snapshot[i]->base.send(&snapshot[i]->base, frame, frame_len, NULL, 0);
        RELEASE((Object*)snapshot[i]);
    }
    free(snapshot);
}

// --- 이벤트 콜백 ---
static void on_client_readable(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    TcpSocket* client = (TcpSocket*)self;
    uint8_t buf[8192] = {0};
    ssize_t received = client->base.recv(&client->base, buf, sizeof(buf), NULL, 0);

    if (received > 0) {
        if ((buf[0] & 0x0F) == 0x09) {
            buf[0] = (buf[0] & 0xF0) | 0x0A;
            client->base.send(&client->base, buf, received, NULL, 0);
            return;
        }
        char msg[4096] = {0};
        ssize_t payload_len = ws_decode_frame(buf, received, msg, sizeof(msg) - 1);
        if (payload_len > 0) {
            if (strstr(msg, "\"type\":\"announce\"") || strstr(msg, "\"type\":\"ping\"")) {
                char* uin_ptr = strstr(msg, "\"uin\":\"");
                if (uin_ptr) {
                    char uin_val[16] = {0};
                    sscanf(uin_ptr + 7, "%[^\"]", uin_val);
                    ClientManager_UpdateUIN(client, uin_val);
                }
            }
            ClientManager_Broadcast(msg);
            return;
        }
    }

    // 퇴장 처리
    char target_uin[16] = {0};
    pthread_mutex_lock(&manager.lock);
    size_t size = manager.sessions->getSize(manager.sessions);
    for (size_t i = 0; i < size; i++) {
        ClientSession* s = (ClientSession*)manager.sessions->get(manager.sessions, i);
        if (s->sock == client) {
            strncpy(target_uin, s->uin, 15);
            manager.sessions->remove(manager.sessions, i);
            break;
        }
    }
    pthread_mutex_unlock(&manager.lock);
    loop->delSocket(loop, self);
    if (target_uin[0] != '\0') {
        char leave_json[256];
        snprintf(leave_json, sizeof(leave_json), "{\"type\":\"leave\",\"uin\":\"%s\"}", target_uin);
        ClientManager_Broadcast(leave_json);
    }
}

static void on_handshake_pending(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    TcpSocket* client = (TcpSocket*)self;
    char buf[4096] = {0};
    ssize_t received = client->base.recv(&client->base, buf, sizeof(buf)-1, NULL, 0);

    if (received > 0 && strstr(buf, "Sec-WebSocket-Key: ")) {
        char* key_start = strstr(buf, "Sec-WebSocket-Key: ") + 19;
        char* key_end = strchr(key_start, '\r'); if (key_end) *key_end = '\0';
        char* accept_key = ws_compute_accept_key(key_start);
        char response[512];
        snprintf(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
        client->base.send(&client->base, response, strlen(response), NULL, 0);
        free(accept_key);
        client->base.on_readable = on_client_readable;
    } else if (received > 0) {
        // ✅ [의장님 검수 3] 핸드셰이크 실패 시 좀비 세션 방지
        loop->delSocket(loop, self);
        ClientManager_Remove(client);
    }
}

static void on_client_accept(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    TcpSocket* server = (TcpSocket*)self;
    TcpSocket* client = server->accept(server, NULL, NULL);
    if (client) {
        ClientManager_Add(client);
        client->base.on_readable = on_handshake_pending;
        loop->addSocket(loop, (Socket*)client, EV_READ);
        RELEASE((Object*)client);
    }
}

void handle_sigint(int sig) {
	(void)sig;
	if (g_loop) g_loop->stop(g_loop);
}

int main() {
    signal(SIGINT, handle_sigint);
    ClientManager_Init();
    logger = new_Logger(LOG_LEVEL_DEBUG);
    TcpSocket* server = new_TcpServer("0.0.0.0", 8080);
    g_loop = new_EventLoop(1024);
    server->base.on_readable = on_client_accept;
    g_loop->addSocket(g_loop, (Socket*)server, EV_READ);
    LOG_INFO(logger, "[P3] Fixed Iron Fortress Engine Ready!! (Port 8080)");
    g_loop->run(g_loop);

    ClientManager_Destroy();
    g_loop->delSocket(g_loop, (Socket*)server);
    RELEASE((Object*)g_loop);
    RELEASE((Object*)server);
    RELEASE((Object*)logger);
    return 0;
}