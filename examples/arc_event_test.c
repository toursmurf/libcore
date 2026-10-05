/* TCP Echo Server */

#include "libcore.h"

static void on_client(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;
    char buf[1024];

    ssize_t n = self->recv(
        self,
        buf,
        sizeof(buf),
        NULL,
        NULL
    );

    /*
     * Non-blocking socket:
     * 아직 읽을 데이터가 없음.
     *
     * 연결을 끊으면 안 된다.
     */
    if (n == SOCKET_WOULD_BLOCK) {
        return;
    }

    /*
     * peer orderly shutdown
     */
    if (n == 0) {
        printf("client closed\n");

        loop->delSocket(loop, self);

        /*
         * 중요:
         *
         * addSocket()이 RETAIN하고
         * delSocket()이 RELEASE하는 계약이라면
         * 여기서 RELEASE(self) 하지 않는다.
         */

        return;
    }

    /*
     * real recv error
     */
    if (n < 0) {
        printf("recv error: %zd\n", n);

        loop->delSocket(loop, self);
        return;
    }

    /*
     * n > 0
     *
     * TCP payload는 binary일 수 있으므로
     * %s 대신 길이를 지정해서 출력.
     */
    printf("recv(%zd)=>", n);
    fwrite(buf, 1, (size_t)n, stdout);
    printf("\n");

    ssize_t sent = self->send(
        self,
        buf,
        (size_t)n,
        NULL,
        0
    );

    if (sent == SOCKET_WOULD_BLOCK) {
        /*
         * 여기에는 나중에 TX queue가 필요.
         *
         * 간단한 echo example이라면 우선
         * WOULD_BLOCK을 로그만 남길 수도 있지만,
         * 완전한 구현은 EV_WRITE + pending buffer 필요.
         */
        printf("send would block\n");
        return;
    }

    if (sent < 0) {
        printf("send error: %zd\n", sent);
        loop->delSocket(loop, self);
        return;
    }
}

static void on_accept(Socket* self, void* loop_ptr) {
    EventLoop* loop = (EventLoop*)loop_ptr;

    TcpSocket* client =
        ((TcpSocket*)self)->accept(
            (TcpSocket*)self,
            NULL,
            NULL
        );

    if (!client) {
        return;
    }

    client->base.on_readable = on_client;

    if (loop->addSocket(
            loop,
            (Socket*)client,
            EV_READ
        ) != 0) {

        RELEASE((Object*)client);
        return;
    }

    /*
     * EventLoop이 등록된 socket을 RETAIN하는 계약이라면
     * caller ownership을 반환한다.
     */
    RELEASE((Object*)client);
}

int main(void) {
    EventLoop* loop =
        event_loop_create();

    TcpSocket* server =
        new_TcpServer(
            "0.0.0.0",
            8888
        );

    if (!loop || !server) {
        if (server)
            RELEASE((Object*)server);

        if (loop)
            RELEASE((Object*)loop);

        return 1;
    }

    server->base.on_readable =
        on_accept;

    if (loop->addSocket(
            loop,
            (Socket*)server,
            EV_READ
        ) != 0) {

        RELEASE((Object*)server);
        RELEASE((Object*)loop);
        return 1;
    }

    /*
     * EventLoop owns registered server socket.
     */
    RELEASE((Object*)server);

    event_loop_run(loop);

    RELEASE((Object*)loop);

    return 0;
}