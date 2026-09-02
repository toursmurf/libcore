#include "http_request_builder.h"
#include "string_obj.h"
#include "arraylist.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void normalize_path(char* path) {
    if (!path || path[0] == '\0') return;
    char *input = strdup(path);
    if (!input) return; /* 🚨 NULL 방어막 */

    ArrayList* stack = new_ArrayList(16);
    if (!stack) {
      free(input);
      return;
    }

    char *saveptr;
    char *token = strtok_r(input, "/", &saveptr);

    while (token) {
        if (strcmp(token, ".") == 0) {
            /* Do nothing */
        } else if (strcmp(token, "..") == 0) {
            if (stack->getSize(stack) > 0) {
                stack->removeResult(stack, stack->getSize(stack) - 1);
            }
        } else {
            String* token_str = new_String(token);
            if (token_str) stack->add(stack, (Object*)token_str);
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    path[0] = '/';
    char* p = path + 1;
    for (int i = 0; i < stack->getSize(stack); i++) {
        String* s = (String*)stack->get(stack, i);
        size_t slen = s->length_f(s);
        memcpy(p, s->c_str(s), slen);
        p += slen;
        if (i < stack->getSize(stack) - 1) *p++ = '/';
        RELEASE(s);
    }
    *p = '\0';
    RELEASE((Object*)stack);
    free(input);
}

char* url_encode(const char* str) {
    if (!str) return strdup("");
    char* buf = (char*)malloc(strlen(str) * 3 + 1);
    if (!buf) return NULL; /* 🚨 NULL 방어막 완비! */

    char* p = buf;
    while (*str) {
        if ((*str >= 'a' && *str <= 'z') || (*str >= 'A' && *str <= 'Z') ||
            (*str >= '0' && *str <= '9') || strchr("-_.~", *str)) *p++ = *str;
        else {
            snprintf(p, 4, "%%%02X", (unsigned char)*str);
            p += 3;
        }
        str++;
    }
    *p = '\0';
    return buf;
}

char* json_escape(const char* str) {
    if (!str) return strdup("");
    char* buf = (char*)malloc(strlen(str) * 6 + 1);
    if (!buf) return NULL; /* 🚨 NULL 방어막 완비! */

    char* p = buf;
    while (*str) {
        switch (*str) {
            case '\"': *p++ = '\\'; *p++ = '\"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b'; break;
            case '\f': *p++ = '\\'; *p++ = 'f'; break;
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            default:
                if ((unsigned char)*str < 0x20) {
                    snprintf(p, 7, "\\u%04x", (unsigned char)*str);
                    p += 6;
                } else {
                    *p++ = *str;
                }
                break;
        }
        str++;
    }
    *p = '\0';
    return buf;
}

char* multipart_filename_escape(const char* str) {
    if (!str) return strdup("");
    char* buf = (char*)malloc(strlen(str) * 2 + 1);
    if (!buf) return NULL; /* 🚨 NULL 방어막 완비! */

    char* p = buf;
    while (*str) {
        if (*str == '\"' || *str == '\\' || *str == '\r' || *str == '\n') {
            *p++ = '\\'; *p++ = *str;
        } else { *p++ = *str; }
        str++;
    }
    *p = '\0';
    return buf;
}

char* build_form_body(HashMap* data, size_t* out_len) {
    if (!data || data->getSize(data) == 0) { *out_len = 0; return NULL; }
    String* s = new_String("");
    if (!s) return NULL;

    ArrayList* keys = data->keys(data);
    if (keys) {
        for (int i = 0; i < keys->getSize(keys); i++) {
            String* k = (String*)keys->get(keys, i);
            String* v = (String*)data->get(data, k->c_str(k));
            if (k && v) {
                if (i > 0) s->append(s, "&");
                char* ek = url_encode(k->c_str(k));
                char* ev = url_encode(v->c_str(v));
                if (ek && ev) {
                    s->append(s, ek); s->append(s, "="); s->append(s, ev);
                }
                if (ek) free(ek);
                if (ev) free(ev);
            }
        }
        RELEASE((Object*)keys);
    }
    *out_len = s->length_f(s);
    char* res = strdup(s->c_str(s));
    RELEASE((Object*)s);
    return res;
}

char* build_json_body(HashMap* data, size_t* out_len) {
    if (!data) { *out_len = 0; return NULL; }
    String* s = new_String("{");
    if (!s) return NULL;

    ArrayList* keys = data->keys(data);
    if (keys) {
        for (int i = 0; i < keys->getSize(keys); i++) {
            String* k = (String*)keys->get(keys, i);
            String* v = (String*)data->get(data, k->c_str(k));
            if (k && v) {
                if (i > 0) s->append(s, ",");
                char* ek = json_escape(k->c_str(k));
                char* ev = json_escape(v->c_str(v));
                if (ek && ev) {
                    s->append(s, "\""); s->append(s, ek); s->append(s, "\":\""); s->append(s, ev); s->append(s, "\"");
                }
                if (ek) free(ek);
                if (ev) free(ev);
            }
        }
        RELEASE((Object*)keys);
    }
    s->append(s, "}");
    *out_len = s->length_f(s);
    char* res = strdup(s->c_str(s));
    RELEASE((Object*)s);
    return res;
}