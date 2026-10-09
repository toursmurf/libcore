#ifndef HTTP_RESPONSE_PARSER_H
#define HTTP_RESPONSE_PARSER_H

#include "http_transport.h"
#include "http_client.h"

HttpClientResponse* HttpResponseParser_parse(HttpTransport* transport);
HttpClientResponse* HttpResponseParser_parse_with_status(HttpTransport* transport, const char* initial_status_line);
HttpClientResponse* HttpResponseParser_parse_with_options(HttpTransport* transport, const char* initial_status_line, const char* request_method);

#endif /* HTTP_RESPONSE_PARSER_H */