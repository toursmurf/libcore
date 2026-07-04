#ifndef HTTP_REQUEST_BUILDER_H
#define HTTP_REQUEST_BUILDER_H

#include "hashmap.h"
#include <stddef.h>

void normalize_path(char* path);
char* url_encode(const char* str);
char* json_escape(const char* str);
char* multipart_filename_escape(const char* str);

char* build_form_body(HashMap* data, size_t* out_len);
char* build_json_body(HashMap* data, size_t* out_len);

#endif /* HTTP_REQUEST_BUILDER_H */