#ifndef MULTIPART_WRITER_H
#define MULTIPART_WRITER_H

#include "hashmap.h"
#include "http_transport.h"
#include <stddef.h>

void generate_multipart_boundary(char* buf, size_t size);
ssize_t MultipartWriter_calculate_length(HashMap* data, HashMap* files, const char* boundary);
int MultipartWriter_stream_send(HashMap* data, HashMap* files, const char* boundary, HttpTransport* transport);

#endif /* MULTIPART_WRITER_H */