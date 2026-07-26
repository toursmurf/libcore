#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include "socket_base.h"
#include <sys/types.h>

typedef struct {
    Socket* sock;
    char* host;
    char* path;
    int port;
    char scheme[16];

    char read_buf[8192];
    int read_pos;
    int read_end;
} HttpTransport;

HttpTransport* HttpTransport_connect(const char* url);
ssize_t HttpTransport_send(HttpTransport* self, const void* buf, size_t len);
ssize_t HttpTransport_recv(HttpTransport* self, void* buf, size_t len);
int HttpTransport_getc(HttpTransport* self);
int HttpTransport_recv_line(HttpTransport* self, char* line_buf, int max_len);
void HttpTransport_close(HttpTransport* self);

#endif /* HTTP_TRANSPORT_H */