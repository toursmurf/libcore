#include "http_server.h"
#include "multipart_parser.h"
#include "json.h"
#include "logger.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdatomic.h>

extern Logger* logger;

#define MAX_UPLOAD_SIZE (10 * 1024 * 1024)  /* 10MB */

/* ── mkdir -p 헬퍼 ────────────────────────────────── */
static int mkdir_p(const char *path, mode_t mode) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) {
        return -1;
    }
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* ── Basename 추출 + Path Traversal 차단 ──────────── */
static void sanitize_filename(const char* dirty, char* clean_out, size_t max_len) {
    if (max_len == 0 || !dirty) {
        return;
    }
    const char* base = dirty;
    const char* p    = dirty;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
        p++;
    }
    if (*base == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }
    size_t i = 0;
    while (*base && i < max_len - 1) {
        if (*base != ':' && *base != '?' && *base != '*' &&
            *base != '"' && *base != '<' && *base != '>' && *base != '|') {
            clean_out[i++] = *base;
        }
        base++;
    }
    clean_out[i] = '\0';
    if (i == 0) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
    }
}

/* ── POST /board/write ───────────────────────────── */
void board_write_handler(HttpRequest* req, HttpResponse* res, void* user_ctx) {
    DBClient* db = (DBClient*)user_ctx;

    if (req->method != HTTP_POST) {
        res->sendStatus(res, 405);
        return;
    }
    if (!req->multipart) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Multipart payload expected.");
        return;
    }
    if (!db) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB Connection unavailable.");
        return;
    }

    const char* title   = MultipartResult_get_field(req->multipart, "title");
    const char* content = MultipartResult_get_field(req->multipart, "content");
    long current_member_id = 1; /* TODO: 실제 세션 연동 */

    if (!title || !content) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Missing 'title' or 'content'.");
        return;
    }
    if (strlen(content) > 65535) {
        if (logger) {
            LOG_WARN(logger, "Content exceeds TEXT column limit (64KB).");
        }
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Content is too long (Max 65,535 bytes).");
        return;
    }

    HttpMultipartFile* attach = MultipartResult_get_file(req->multipart, "attach");

    /* String* → char* 추출 */
    const char* original_filename = "untitled.bin";
    if (attach && attach->filename && attach->filename->c_str) {
        original_filename = attach->filename->c_str(attach->filename);
    }

    const char* mime_str = "application/octet-stream";
    if (attach && attach->content_type && attach->content_type->c_str) {
        mime_str = attach->content_type->c_str(attach->content_type);
    }

    char storage_path[512] = {0};
    bool file_saved        = false;

    /* ── 1. 파일 디스크 기록 ── */
    if (attach && attach->data && attach->size > 0) {
        if (attach->size > MAX_UPLOAD_SIZE) {
            res->setStatus(res, 413);
            res->sendText(res, "Payload Too Large: Max upload size is 10MB.");
            return;
        }
        char safe_name[256] = {0};
        sanitize_filename(original_filename, safe_name, sizeof(safe_name));

        struct timeval tv;
        gettimeofday(&tv, NULL);

        static _Atomic unsigned int seq_counter = 0;
        unsigned int seq = atomic_fetch_add(&seq_counter, 1);

        snprintf(storage_path, sizeof(storage_path),
                 "/var/toostalk/uploads/%ld_%06ld_%04u_%s",
                 (long)tv.tv_sec, (long)tv.tv_usec, (seq % 10000), safe_name);

        if (mkdir_p("/var/toostalk/uploads", 0755) == -1) {
            if (logger) {
                LOG_ERROR(logger, "mkdir_p failed (errno: %d)", errno);
            }
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: Storage initialization failed.");
            return;
        }

        FILE* fp = fopen(storage_path, "wb");
        if (fp) {
            size_t written = fwrite(attach->data, 1, attach->size, fp);
            fflush(fp);
            fsync(fileno(fp));
            fclose(fp);
            if (written != attach->size) {
                remove(storage_path);
                res->setStatus(res, 500);
                res->sendText(res, "Internal Server Error: Disk write error.");
                return;
            }
            file_saved = true;
        } else {
            if (logger) {
                LOG_ERROR(logger, "fopen failed: %s (errno: %d)", storage_path, errno);
            }
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: Disk access denied.");
            return;
        }
    }

    /* ── 2. 트랜잭션 (ACID) ── */
    db->beginTransaction(db);

    char mid_buf[32];
    snprintf(mid_buf, sizeof(mid_buf), "%ld", current_member_id);

    String*  s_mid     = new_String(mid_buf);
    String*  s_title   = new_String(title);
    String*  s_content = new_String(content);

    HashMap* post_data = new_HashMap(8);
    post_data->put(post_data, "member_id", (Object*)s_mid);
    post_data->put(post_data, "title",     (Object*)s_title);
    post_data->put(post_data, "content",   (Object*)s_content);

    RELEASE(s_mid);
    RELEASE(s_title);
    RELEASE(s_content);

    int       post_ok     = db->insertTable(db, "board_posts", post_data);
    long long new_post_id = db->last_insert_id;
    RELEASE(post_data);

    int attach_ok = 1; /* 파일 없는 게시글은 attach 단계 스킵 — 정상 케이스 */

    if (post_ok && file_saved) {
        char pid_buf[32];
        snprintf(pid_buf, sizeof(pid_buf), "%lld", new_post_id);

        char sz_buf[32];
        snprintf(sz_buf, sizeof(sz_buf), "%zu", attach->size);

        String* sa_pid  = new_String(pid_buf);
        String* sa_mid  = new_String(mid_buf);
        String* sa_org  = new_String(original_filename);
        String* sa_path = new_String(storage_path);
        String* sa_size = new_String(sz_buf);
        String* sa_mime = new_String(mime_str);

        HashMap* att_data = new_HashMap(8);
        att_data->put(att_data, "post_id",       (Object*)sa_pid);
        att_data->put(att_data, "member_id",     (Object*)sa_mid);
        att_data->put(att_data, "original_name", (Object*)sa_org);
        att_data->put(att_data, "storage_path",  (Object*)sa_path);
        att_data->put(att_data, "file_size",     (Object*)sa_size);
        att_data->put(att_data, "mime_type",     (Object*)sa_mime);

        RELEASE(sa_pid);
        RELEASE(sa_mid);
        RELEASE(sa_org);
        RELEASE(sa_path);
        RELEASE(sa_size);
        RELEASE(sa_mime);

        attach_ok = db->insertTable(db, "attachments", att_data);
        RELEASE(att_data);
    }

    if (!post_ok || (file_saved && !attach_ok)) {
        db->rollback(db);
        if (file_saved && storage_path[0] != '\0') {
            remove(storage_path);
        }
        if (logger) {
            LOG_ERROR(logger, "Transaction failed. Rolled back DB and cleaned disk.");
        }
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: Database transaction failed.");
        return;
    }

    db->commit(db);

    /* ── 3. 응답 ── */
    JSONNode* resp_json = new_JSON_Object();

    JSONNode* j_status = new_JSON_String("success");
    if (j_status) {
        resp_json->put(resp_json, "status", (Object*)j_status);
        RELEASE((Object*)j_status);
    }

    JSONNode* j_pid = (JSONNode*)new_json_number((double)new_post_id);
    if (j_pid) {
        resp_json->put(resp_json, "post_id", (Object*)j_pid);
        RELEASE((Object*)j_pid);
    }

    JSONNode* j_msg = new_JSON_String("게시글 및 첨부파일 영속화 완료!");
    if (j_msg) {
        resp_json->put(resp_json, "message", (Object*)j_msg);
        RELEASE((Object*)j_msg);
    }

    res->sendJson(res, resp_json);
    RELEASE((Object*)resp_json);
}