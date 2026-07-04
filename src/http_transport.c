#define _GNU_SOURCE
#include "http_transport.h"
#include "ssl_client.h"
#include "tcp_socket.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

HttpTransport* HttpTransport_connect(const char* url) {
    if (!url) return NULL;

    HashMap* url_info = parse_url(url);
    if (!url_info) return NULL;

    const char* scheme = hashmap_get_str(url_info, "scheme");
    const char* host = hashmap_get_str(url_info, "host");
    const char* port_str = hashmap_get_str(url_info, "port");
    const char* path = hashmap_get_str(url_info, "path");

    if (!scheme || !host) {
        RELEASE((Object*)url_info);
        return NULL;
    }

    int port = port_str ? atoi(port_str) : (strcasecmp(scheme, "https") == 0 ? 443 : 80);

    Socket* sock = NULL;
    if (strcasecmp(scheme, "https") == 0) {
        sock = (Socket*)new_SslClient(host, port);
    } else if (strcasecmp(scheme, "http") == 0) {
        sock = (Socket*)new_TcpClient(host, port);
    }

    if (!sock) {
        RELEASE((Object*)url_info);
        return NULL;
    }

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags != -1) fcntl(sock->fd, F_SETFL, flags & ~O_NONBLOCK);

    HttpTransport* transport = (HttpTransport*)calloc(1, sizeof(HttpTransport));
    if (transport) {
        transport->sock = sock;
        transport->host = strdup(host);
        transport->path = strdup((path && strlen(path) > 0) ? path : "/");
        transport->port = port;
        strncpy(transport->scheme, scheme, sizeof(transport->scheme) - 1);
        transport->read_pos = 0;
        transport->read_end = 0;
    } else {
        RELEASE((Object*)sock);
    }

    RELEASE((Object*)url_info);
    return transport;
}

ssize_t HttpTransport_send(HttpTransport* self, const void* buf, size_t len) {
    if (!self || !self->sock || !buf || len == 0) return 0;
    size_t total_sent = 0;
    const char* ptr = (const char*)buf;
    while (total_sent < len) {
        ssize_t n = self->sock->send(self->sock, ptr + total_sent, len - total_sent, NULL, 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total_sent += n;
    }
    return (ssize_t)total_sent;
}

ssize_t HttpTransport_recv(HttpTransport* self, void* buf, size_t len) {
    if (!self || !self->sock || !buf || len == 0) return -1;
    return self->sock->recv(self->sock, buf, len, NULL, NULL);
}

int HttpTransport_getc(HttpTransport* self) {
    if (!self) return -1;
    if (self->read_pos >= self->read_end) {
        self->read_pos = 0;
        self->read_end = self->sock->recv(self->sock, self->read_buf, sizeof(self->read_buf), NULL, NULL);
        if (self->read_end <= 0) return -1;
    }
    return (unsigned char)self->read_buf[self->read_pos++];
}

int HttpTransport_recv_line(HttpTransport* self, char* line_buf, int max_len) {
    int i = 0, c;
    while (i < max_len - 1) {
        c = HttpTransport_getc(self);
        if (c < 0) break;
        line_buf[i++] = (char)c;
        if (c == '\n') break;
    }
    line_buf[i] = '\0';
    if (i > 0 && line_buf[i-1] == '\n') line_buf[i-1] = '\0';
    if (i > 1 && line_buf[i-2] == '\r') line_buf[i-2] = '\0';
    return i;
}

void HttpTransport_close(HttpTransport* self) {
    if (!self) return;
    if (self->sock) RELEASE((Object*)self->sock);
    if (self->host) free(self->host);
    if (self->path) free(self->path);
    free(self);
}