/*
 * board_handler.c
 * 게시판 핸들러 — libcore OOP 구현체
 * 🌿 Eye-Care Mode: 1 Line = 1 Statement
 */

#include "board_handler.h"
#include "multipart_parser.h"
#include "json.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdatomic.h>

extern Logger* logger;

/* ── 내부 유틸 ─────────────────────────────────── */
static int mkdir_p(const char* path, mode_t mode) {
    char   tmp[512];
    size_t len;
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) {
        return -1;
    }
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char* p = tmp + 1; *p; p++) {
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

/* ── file_sanitize ──────────────────────────────── */
static void BoardHandler_file_sanitize(BoardHandler* self,
                                        const char*   dirty,
                                        char*         clean_out,
                                        size_t        max_len) {
    if (!self || !dirty || !clean_out || max_len == 0) {
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
    char test_path[MAX_PATH_LEN + 2];
    int  n = snprintf(test_path, sizeof(test_path), "/%s", base);
    if (n < 0 || (size_t)n >= sizeof(test_path)) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }
    char canonical[MAX_PATH_LEN + 1];
    if (!self->pv->validate(self->pv, test_path,
                            canonical, sizeof(canonical))) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }
    char*       last_slash = strrchr(canonical, '/');
    const char* final_name = last_slash ? last_slash + 1 : canonical;
    if (*final_name == '\0' ||
        strcmp(final_name, ".")  == 0 ||
        strcmp(final_name, "..") == 0) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }
    strncpy(clean_out, final_name, max_len - 1);
    clean_out[max_len - 1] = '\0';
}

/* ── file_save ──────────────────────────────────── */
static bool BoardHandler_file_save(BoardHandler*      self,
                                    HttpMultipartFile* attach,
                                    char*              out_path,
                                    size_t             out_size) {
    if (!self || !attach || !attach->data || attach->size == 0) {
        return false;
    }
    const char* original = "untitled.bin";
    if (attach->filename && attach->filename->c_str) {
        original = attach->filename->c_str(attach->filename);
    }
    char safe_name[256] = {0};
    self->file_sanitize(self, original, safe_name, sizeof(safe_name));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    static _Atomic unsigned int seq = 0;
    unsigned int s = atomic_fetch_add(&seq, 1);
    snprintf(out_path, out_size,
             "%s/%ld_%06ld_%04u_%s",
             self->upload_dir,
             (long)tv.tv_sec, (long)tv.tv_usec,
             s % 10000, safe_name);
    if (mkdir_p(self->upload_dir, 0700) == -1) {
        if (logger) {
            LOG_ERROR(logger, "mkdir_p failed: %s (errno: %d)",
                      self->upload_dir, errno);
        }
        return false;
    }
    FILE* fp = fopen(out_path, "wb");
    if (!fp) {
        if (logger) {
            LOG_ERROR(logger, "fopen failed: %s", out_path);
        }
        return false;
    }
    size_t written = fwrite(attach->data, 1, attach->size, fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    if (written != attach->size) {
        remove(out_path);
        return false;
    }
    return true;
}

/* ── file_delete ────────────────────────────────── */
static bool BoardHandler_file_delete(BoardHandler* self,
                                      const char*   path) {
    (void)self;
    if (!path || path[0] == '\0') {
        return false;
    }
    return remove(path) == 0;
}

/* ── write — POST /board/write ──────────────────── */
static void BoardHandler_write(BoardHandler* self,
                                HttpRequest*  req,
                                HttpResponse* res) {
    if (req->method != HTTP_POST) {
        res->sendStatus(res, 405);
        return;
    }
    if (!req->multipart) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Multipart payload expected.");
        return;
    }
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB unavailable.");
        return;
    }
    const char* title     = MultipartResult_get_field(req->multipart, "title");
    const char* content   = MultipartResult_get_field(req->multipart, "content");
    long        member_id = 1;
    if (!title || !content) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Missing 'title' or 'content'.");
        return;
    }
    if (strlen(content) > 65535) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Content too long.");
        return;
    }
    HttpMultipartFile* attach   = MultipartResult_get_file(req->multipart, "attach");
    char               storage_path[512] = {0};
    bool               file_saved = false;
    const char*        original_filename = "untitled.bin";
    if (attach && attach->filename && attach->filename->c_str) {
        original_filename = attach->filename->c_str(attach->filename);
    }
    if (attach && attach->data && attach->size > 0) {
        file_saved = self->file_save(self, attach,
                                     storage_path, sizeof(storage_path));
        if (!file_saved) {
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: File save failed.");
            return;
        }
    }
    self->db->beginTransaction(self->db);
    char     mid_buf[32];
    snprintf(mid_buf, sizeof(mid_buf), "%ld", member_id);
    String*  s_mid     = new_String(mid_buf);
    String*  s_title   = new_String(title);
    String*  s_content = new_String(content);
    HashMap* post_data = new_HashMap(8);
    post_data->put(post_data, "member_id", (Object*)s_mid);
    post_data->put(post_data, "title",     (Object*)s_title);
    post_data->put(post_data, "content",   (Object*)s_content);
    RELEASE((Object*)s_mid);
    RELEASE((Object*)s_title);
    RELEASE((Object*)s_content);
    int       post_ok     = self->db->insertTable(self->db, "board_posts", post_data);
    long long new_post_id = self->db->last_insert_id;
    RELEASE((Object*)post_data);
    int attach_ok = 1;
    if (post_ok && file_saved) {
        char pid_buf[32];
        char sz_buf[32];
        snprintf(pid_buf, sizeof(pid_buf), "%lld", new_post_id);
        snprintf(sz_buf,  sizeof(sz_buf),  "%zu",  attach->size);
        const char* mime_str = "application/octet-stream";
        if (attach->content_type && attach->content_type->c_str) {
            mime_str = attach->content_type->c_str(attach->content_type);
        }
        String*  sa_pid   = new_String(pid_buf);
        String*  sa_mid   = new_String(mid_buf);
        String*  sa_org   = new_String(original_filename);
        String*  sa_path  = new_String(storage_path);
        String*  sa_size  = new_String(sz_buf);
        String*  sa_mime  = new_String(mime_str);
        HashMap* att_data = new_HashMap(8);
        att_data->put(att_data, "post_id",       (Object*)sa_pid);
        att_data->put(att_data, "member_id",      (Object*)sa_mid);
        att_data->put(att_data, "original_name",  (Object*)sa_org);
        att_data->put(att_data, "storage_path",   (Object*)sa_path);
        att_data->put(att_data, "file_size",      (Object*)sa_size);
        att_data->put(att_data, "mime_type",      (Object*)sa_mime);
        RELEASE((Object*)sa_pid);
        RELEASE((Object*)sa_mid);
        RELEASE((Object*)sa_org);
        RELEASE((Object*)sa_path);
        RELEASE((Object*)sa_size);
        RELEASE((Object*)sa_mime);
        attach_ok = self->db->insertTable(self->db, "attachments", att_data);
        RELEASE((Object*)att_data);
    }
    if (!post_ok || (file_saved && !attach_ok)) {
        self->db->rollback(self->db);
        if (file_saved) {
            self->file_delete(self, storage_path);
        }
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB transaction failed.");
        return;
    }
    self->db->commit(self->db);
    JSONNode* resp     = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    JSONNode* j_pid    = (JSONNode*)new_json_number((double)new_post_id);
    JSONNode* j_msg    = new_JSON_String("게시글 및 첨부파일 영속화 완료!");
    if (j_status) { resp->put(resp, "status",  (Object*)j_status); RELEASE((Object*)j_status); }
    if (j_pid)    { resp->put(resp, "post_id", (Object*)j_pid);    RELEASE((Object*)j_pid);    }
    if (j_msg)    { resp->put(resp, "message", (Object*)j_msg);    RELEASE((Object*)j_msg);    }
    res->sendJson(res, resp);
    RELEASE((Object*)resp);
}

/* ── list — GET /board/list ─────────────────────── */
static void BoardHandler_list(BoardHandler* self,
                               HttpRequest*  req,
                               HttpResponse* res) {
    (void)req;
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB unavailable.");
        return;
    }
    /* TODO: board_posts SELECT (페이지네이션) */
    res->setStatus(res, 200);
    res->sendText(res, "{\"status\":\"ok\",\"posts\":[]}");
}

/* ── detail — GET /board/:id ────────────────────── */
static void BoardHandler_detail(BoardHandler* self,
                                 HttpRequest*  req,
                                 HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB unavailable.");
        return;
    }
    const char* path_str = req->path ? req->path->c_str(req->path) : "/";
    int post_id = 0;
    sscanf(path_str, "/board/%d", &post_id);
    if (post_id <= 0) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Invalid post ID.");
        return;
    }
    /* TODO: board_posts + attachments SELECT */
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"post_id\":%d}", post_id);
    res->setStatus(res, 200);
    res->sendText(res, buf);
}

/* ── modify — PUT /board/:id ────────────────────── */
static void BoardHandler_modify(BoardHandler* self,
                                 HttpRequest*  req,
                                 HttpResponse* res) {
    if (req->method != HTTP_PUT) {
        res->sendStatus(res, 405);
        return;
    }
    if (!req->multipart) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Multipart payload expected.");
        return;
    }
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB unavailable.");
        return;
    }
    const char* path_str = req->path ? req->path->c_str(req->path) : "/";
    int post_id = 0;
    sscanf(path_str, "/board/%d", &post_id);
    if (post_id <= 0) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Invalid post ID.");
        return;
    }
    const char* title   = MultipartResult_get_field(req->multipart, "title");
    const char* content = MultipartResult_get_field(req->multipart, "content");
    if (!title || !content) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Missing 'title' or 'content'.");
        return;
    }
    if (strlen(content) > 65535) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Content too long.");
        return;
    }
    HttpMultipartFile* attach    = MultipartResult_get_file(req->multipart, "attach");
    char               new_path[512] = {0};
    bool               file_saved   = false;
    if (attach && attach->data && attach->size > 0) {
        file_saved = self->file_save(self, attach,
                                     new_path, sizeof(new_path));
        if (!file_saved) {
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: File save failed.");
            return;
        }
    }
    self->db->beginTransaction(self->db);
    char pid_buf[32];
    snprintf(pid_buf, sizeof(pid_buf), "%d", post_id);
    String*  s_title   = new_String(title);
    String*  s_content = new_String(content);
    String*  s_pid     = new_String(pid_buf);
    HashMap* upd_data  = new_HashMap(8);
    upd_data->put(upd_data, "title",   (Object*)s_title);
    upd_data->put(upd_data, "content", (Object*)s_content);
    upd_data->put(upd_data, "id",      (Object*)s_pid);
    RELEASE((Object*)s_title);
    RELEASE((Object*)s_content);
    RELEASE((Object*)s_pid);
    int update_ok = self->db->updateTable(self->db,
                        "board_posts", upd_data, "id");
    RELEASE((Object*)upd_data);
    int attach_ok = 1;
    if (update_ok && file_saved) {
        /* TODO: 기존 첨부파일 조회 후 삭제 */
        const char* original_filename = "untitled.bin";
        if (attach->filename && attach->filename->c_str) {
            original_filename = attach->filename->c_str(attach->filename);
        }
        char sz_buf[32];
        snprintf(sz_buf, sizeof(sz_buf), "%zu", attach->size);
        const char* mime_str = "application/octet-stream";
        if (attach->content_type && attach->content_type->c_str) {
            mime_str = attach->content_type->c_str(attach->content_type);
        }
        String*  sa_pid   = new_String(pid_buf);
        String*  sa_org   = new_String(original_filename);
        String*  sa_path  = new_String(new_path);
        String*  sa_size  = new_String(sz_buf);
        String*  sa_mime  = new_String(mime_str);
        HashMap* att_data = new_HashMap(8);
        att_data->put(att_data, "post_id",       (Object*)sa_pid);
        att_data->put(att_data, "original_name",  (Object*)sa_org);
        att_data->put(att_data, "storage_path",   (Object*)sa_path);
        att_data->put(att_data, "file_size",      (Object*)sa_size);
        att_data->put(att_data, "mime_type",      (Object*)sa_mime);
        RELEASE((Object*)sa_pid);
        RELEASE((Object*)sa_org);
        RELEASE((Object*)sa_path);
        RELEASE((Object*)sa_size);
        RELEASE((Object*)sa_mime);
        attach_ok = self->db->updateTable(self->db,
                        "attachments", att_data, "post_id");
        RELEASE((Object*)att_data);
    }
    if (!update_ok || (file_saved && !attach_ok)) {
        self->db->rollback(self->db);
        if (file_saved) {
            self->file_delete(self, new_path);
        }
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB transaction failed.");
        return;
    }
    self->db->commit(self->db);
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"post_id\":%d}", post_id);
    res->setStatus(res, 200);
    res->sendText(res, buf);
}

/* ── remove — DELETE /board/:id ─────────────────── */
static void BoardHandler_remove(BoardHandler* self,
                                 HttpRequest*  req,
                                 HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB unavailable.");
        return;
    }
    const char* path_str = req->path ? req->path->c_str(req->path) : "/";
    int post_id = 0;
    sscanf(path_str, "/board/%d", &post_id);
    if (post_id <= 0) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Invalid post ID.");
        return;
    }
    /* TODO: soft delete + 첨부파일 file_delete */
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"deleted\":%d}", post_id);
    res->setStatus(res, 200);
    res->sendText(res, buf);
}

/* ── ARC ────────────────────────────────────────── */
static void BoardHandler_finalize(Object* obj) {
    (void)obj;
    /* db / pv 는 [BORROWED] — RELEASE 하지 않음!! */
}

static const Class BoardHandler_Class = {
    .name     = "BoardHandler",
    .size     = sizeof(BoardHandler),
    .finalize = BoardHandler_finalize
};

/* ── 생성자 ─────────────────────────────────────── */
BoardHandler* new_BoardHandler(DBClient*      db,
                                PathValidator* pv,
                                const char*    upload_dir) {
    BoardHandler* self = (BoardHandler*)calloc(1, sizeof(BoardHandler));
    if (!self) {
        return NULL;
    }
    Object_Init((Object*)self, &BoardHandler_Class);
    self->db = db;
    self->pv = pv;
    snprintf(self->upload_dir, sizeof(self->upload_dir),
             "%s", upload_dir ? upload_dir : "/var/toostalk/uploads");
    self->write         = BoardHandler_write;
    self->list          = BoardHandler_list;
    self->detail        = BoardHandler_detail;
    self->modify        = BoardHandler_modify;
    self->remove        = BoardHandler_remove;
    self->file_save     = BoardHandler_file_save;
    self->file_delete   = BoardHandler_file_delete;
    self->file_sanitize = BoardHandler_file_sanitize;
    return self;
}

/* ── 라우터 콜백 어댑터 ──────────────────────────── */
void board_write_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    BoardHandler* bh = (BoardHandler*)ctx;
    bh->write(bh, req, res);
}
void board_list_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    BoardHandler* bh = (BoardHandler*)ctx;
    bh->list(bh, req, res);
}
void board_detail_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    BoardHandler* bh = (BoardHandler*)ctx;
    bh->detail(bh, req, res);
}
void board_modify_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    BoardHandler* bh = (BoardHandler*)ctx;
    bh->modify(bh, req, res);
}
void board_remove_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    BoardHandler* bh = (BoardHandler*)ctx;
    bh->remove(bh, req, res);
}