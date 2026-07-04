#ifndef HTTP_RESPONSE_PARSER_H
#define HTTP_RESPONSE_PARSER_H

#include "http_client.h"
#include "http_transport.h"

HttpClientResponse* HttpResponseParser_parse(HttpTransport* transport);
HttpClientResponse* HttpResponseParser_parse_with_status(HttpTransport* transport, const char* initial_status_line);

#endif /* HTTP_RESPONSE_PARSER_H */