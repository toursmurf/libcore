#include "multipart_writer.h"
#include "http_request_builder.h"
#include "http_client.h"
#include "arraylist.h"
#include "string_obj.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdatomic.h>

void generate_multipart_boundary(char* buf, size_t size) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    static _Atomic unsigned int multipart_counter = 0;
    unsigned int count = atomic_fetch_add(&multipart_counter, 1);
    snprintf(buf, size, "----WebCoreFormBoundary%08lX%04X%04X",
             (long)tv.tv_usec, getpid() & 0xFFFF, count & 0xFFFF);
}

static const char* infer_mime_type(const char* filename) {
    if (!filename) return "application/octet-stream";
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".txt") == 0) return "text/plain";
    return "application/octet-stream";
}

static ssize_t process_multipart(HashMap* data, HashMap* files, const char* boundary, HttpTransport* transport, bool action_send) {
    size_t total_len = 0;
    char buf[2048];
    int len;

    if (data) {
        ArrayList* keys = data->keys(data);
        if (keys) {
            for (int i = 0; i < keys->getSize(keys); i++) {
                String* k = (String*)keys->get(keys, i);
                String* v = (String*)data->get(data, k->c_str(k));
                if (k && v) {
                    len = snprintf(buf, sizeof(buf), "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n",
                                   boundary, k->c_str(k), v->c_str(v));
                    total_len += len;
                    if (action_send && HttpTransport_send(transport, buf, len) < 0) {
                        RELEASE((Object*)keys);
                        return -1;
                    }
                }
            }
            RELEASE((Object*)keys);
        }
    }

    if (files) {
        ArrayList* keys = files->keys(files);
        if (keys) {
            for (int i = 0; i < keys->getSize(keys); i++) {
                String* k = (String*)keys->get(keys, i);
                HttpMultipartFile* f = (HttpMultipartFile*)files->get(files, k->c_str(k));

                if (k && f && f->filename) {
                    const char* fname_cstr = f->filename->c_str(f->filename);
                    size_t file_actual_size = f->size;

                    if (f->data == NULL) {
                        struct stat st;
                        if (stat(fname_cstr, &st) == 0) {
                            file_actual_size = st.st_size;
                        } else {
                            RELEASE((Object*)keys);
                            return -1;
                        }
                    }

                    char* safe_fname = multipart_filename_escape(fname_cstr);
                    if (!safe_fname) { RELEASE((Object*)keys); return -1; }

                    const char* ctype_cstr = f->content_type ? f->content_type->c_str(f->content_type) : NULL;
                    const char* mime = ctype_cstr ? ctype_cstr : infer_mime_type(safe_fname);

                    len = snprintf(buf, sizeof(buf),
                             "--%s\r\n"
                             "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                             "Content-Type: %s\r\n\r\n",
                             boundary, k->c_str(k), safe_fname, mime);
                    total_len += len;

                    if (action_send && HttpTransport_send(transport, buf, len) < 0) {
                        free(safe_fname);
                        RELEASE((Object*)keys);
                        return -1;
                    }
                    free(safe_fname);

                    total_len += file_actual_size;

                    if (action_send) {
                        if (f->data) {
                            if (HttpTransport_send(transport, f->data, f->size) < 0) {
                                RELEASE((Object*)keys);
                                return -1;
                            }
                        } else {
                            FILE* fp = fopen(fname_cstr, "rb");
                            if (!fp) { RELEASE((Object*)keys); return -1; }
                            char chunk[65536];
                            size_t n;
                            while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
                                if (HttpTransport_send(transport, chunk, n) < 0) {
                                    fclose(fp);
                                    RELEASE((Object*)keys);
                                    return -1;
                                }
                            }
                            fclose(fp);
                        }
                    }

                    total_len += 2;
                    if (action_send && HttpTransport_send(transport, "\r\n", 2) < 0) {
                        RELEASE((Object*)keys);
                        return -1;
                    }
                }
            }
            RELEASE((Object*)keys);
        }
    }

    len = snprintf(buf, sizeof(buf), "--%s--\r\n", boundary);
    total_len += len;
    if (action_send && HttpTransport_send(transport, buf, len) < 0) return -1;

    return (ssize_t)total_len;
}

ssize_t MultipartWriter_calculate_length(HashMap* data, HashMap* files, const char* boundary) {
    return process_multipart(data, files, boundary, NULL, false);
}

int MultipartWriter_stream_send(HashMap* data, HashMap* files, const char* boundary, HttpTransport* transport) {
    if (process_multipart(data, files, boundary, transport, true) < 0) return -1;
    return 0;
}
