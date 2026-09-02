/*
  board_handler.c
  게시판 핸들러 — libcore OOP 구현체
  (v1.7.2 Final + Strict DBClient 3-State + TRUE Strict Digits-Only + Fast TX + MIME Guard & Fallback)
*/

#include "board_handler.h"
#include "board_template_engine.h"
#include "multipart_parser.h"
#include "json.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

extern Logger* logger;

#define BOARD_SEARCH_MAX_BYTES     400
#define BOARD_LIKE_MAX_BYTES       (BOARD_SEARCH_MAX_BYTES * 2 + 1)
#define BOARD_WHERE_MAX_BYTES      4096
#define BOARD_COUNT_SQL_MAX_BYTES  6144
#define BOARD_LIST_SQL_MAX_BYTES   8192

/* 🚨 C11 stdatomic 대신 pthread_mutex를 이용한 시퀀스 생성 */
static pthread_mutex_t file_seq_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int file_seq = 0;

static unsigned int BoardHandler_next_file_seq(void) {
    unsigned int value;
    pthread_mutex_lock(&file_seq_lock);
    value = file_seq++;
    pthread_mutex_unlock(&file_seq_lock);
    return value;
}

/* 🚨 Bounded Strlen (무한 루프 문자열 스캔 방어용) */
static size_t BoardHandler_bounded_strlen(const char* s, size_t max_len) {
    size_t n = 0;
    if (!s) return 0;
    while (n < max_len && s[n] != '\0') {
        n++;
    }
    return n;
}

/* 🚨 엄격한 양의 정수 파싱 헬퍼 (PK: id, post_id 등 0 불가 데이터용) */
static bool parse_positive_int(const char* s, int* out) {
    if (!s || !out || s[0] == '\0') return false;

    /* Strict ASCII digits-only */
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }

    errno = 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);

    if (errno == ERANGE ||
        end == s ||
        *end != '\0' ||
        v <= 0 ||
        v > INT_MAX) {
        return false;
    }

    *out = (int)v;
    return true;
}

/* 🚨 상태값 검증용 엄격한 0 포함 양수 파싱 헬퍼 (view_count, size, depth 등) */
static bool parse_nonnegative_int(const char* s, int* out) {
    if (!s || !out || s[0] == '\0') return false;

    /* Strict ASCII digits-only */
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }

    errno = 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);

    if (errno == ERANGE ||
        end == s ||
        *end != '\0' ||
        v < 0 ||
        v > INT_MAX) {
        return false;
    }

    *out = (int)v;
    return true;
}

static const char* ALLOWED_EXT[] = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp",
    ".pdf", ".txt", ".hwp", ".doc", ".docx",
    ".xls", ".xlsx", ".ppt", ".pptx", ".zip",
    NULL
};

static bool is_allowed_ext(const char* ext) {
    if (!ext || ext[0] == '\0') return false;
    for (int i = 0; ALLOWED_EXT[i]; i++) {
        if (strcasecmp(ext, ALLOWED_EXT[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int mkdir_p(const char* path, mode_t mode) {
    char tmp[512];
    int n = snprintf(tmp, sizeof(tmp), "%s", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return -1;

    size_t len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int BoardHandler_escape_like(const char* src, char* dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return 0;
    size_t pos = 0;
    while (*src) {
        if (*src == '=' || *src == '%' || *src == '_') {
            if (pos + 2 >= dst_size) return 0;
            dst[pos++] = '=';
        } else if (pos + 1 >= dst_size) {
            return 0;
        }
        dst[pos++] = *src++;
    }
    dst[pos] = '\0';
    return 1;
}

static void BoardHandler_file_sanitize(BoardHandler* self, const char* dirty, char* clean_out, size_t max_len) {
    if (!self || !self->pv || !dirty || !clean_out || max_len == 0) return;
    const char* base = dirty;
    const char* p = dirty;
    while (*p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }
    char test_path[MAX_PATH_LEN + 2];
    int n = snprintf(test_path, sizeof(test_path), "/%s", base);
    if (n < 0 || (size_t)n >= sizeof(test_path)) {
        int n_clean = snprintf(clean_out, max_len, "untitled.bin");
        if (n_clean < 0 || (size_t)n_clean >= max_len) { if(max_len > 0) clean_out[0] = '\0'; }
        return;
    }
    char canonical[MAX_PATH_LEN + 1];
    if (!self->pv->validate(self->pv, test_path, canonical, sizeof(canonical))) {
        int n_clean = snprintf(clean_out, max_len, "untitled.bin");
        if (n_clean < 0 || (size_t)n_clean >= max_len) { if(max_len > 0) clean_out[0] = '\0'; }
        return;
    }
    char* last_slash = strrchr(canonical, '/');
    const char* final_name = last_slash ? last_slash + 1 : canonical;
    if (*final_name == '\0' || strcmp(final_name, ".") == 0 || strcmp(final_name, "..") == 0) {
        int n_clean = snprintf(clean_out, max_len, "untitled.bin");
        if (n_clean < 0 || (size_t)n_clean >= max_len) { if(max_len > 0) clean_out[0] = '\0'; }
        return;
    }
    int n2 = snprintf(clean_out, max_len, "%s", final_name);
    if (n2 < 0 || (size_t)n2 >= max_len) {
        int n_clean = snprintf(clean_out, max_len, "untitled.bin");
        if (n_clean < 0 || (size_t)n_clean >= max_len) { if(max_len > 0) clean_out[0] = '\0'; }
    }
}

static bool BoardHandler_file_size_exceeded(BoardHandler* self, HttpMultipartFile* attach) {
    if (!attach || !attach->data || attach->size == 0) {
        return false;
    }
    return attach->size > self->max_upload_size;
}

static bool BoardHandler_file_save(BoardHandler* self, HttpMultipartFile* attach, char* out_path, size_t out_size) {
    if (!self || !attach || !attach->data || attach->size == 0) return false;
    const char* original = "untitled.bin";
    if (attach->filename && attach->filename->c_str) {
        original = attach->filename->c_str(attach->filename);
    }
    char safe_name[256] = {0};
    self->file_sanitize(self, original, safe_name, sizeof(safe_name));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned int s = BoardHandler_next_file_seq();

    const char* ext = strrchr(safe_name, '.');
    if (!is_allowed_ext(ext)) return false;

    int n = snprintf(out_path, out_size, "%s/%ld_%06ld_%04u%s", self->upload_dir, (long)tv.tv_sec, (long)tv.tv_usec, s % 10000, ext);
    if (n < 0 || (size_t)n >= out_size) return false;

    char abs_path[1024];
    char abs_dir[1024];
    int n1 = snprintf(abs_path, sizeof(abs_path), "%s/%s", self->base_dir, out_path);
    int n2 = snprintf(abs_dir, sizeof(abs_dir), "%s/%s", self->base_dir, self->upload_dir);
    if (n1 < 0 || (size_t)n1 >= sizeof(abs_path) || n2 < 0 || (size_t)n2 >= sizeof(abs_dir)) return false;

    if (mkdir_p(abs_dir, 0700) == -1) return false;
    FILE* fp = fopen(abs_path, "wb");
    if (!fp) return false;

    size_t written = fwrite(attach->data, 1, attach->size, fp);
    bool ok = true;
    if (fflush(fp) != 0) ok = false;
    if (fsync(fileno(fp)) != 0) ok = false;
    if (fclose(fp) != 0) ok = false;

    if (!ok || written != attach->size) {
        remove(abs_path);
        return false;
    }
    return true;
}

static bool BoardHandler_file_delete(BoardHandler* self, const char* path) {
    if (!self || !self->pv || !path || path[0] == '\0') return false;
    char abs_path[1024];
    int n = snprintf(abs_path, sizeof(abs_path), "%s/%s", self->base_dir, path);
    if (n < 0 || (size_t)n >= sizeof(abs_path)) return false;

    char canonical[MAX_PATH_LEN + 1];
    if (!self->pv->validate(self->pv, abs_path, canonical, sizeof(canonical))) {
        return false;
    }

    struct stat st;
    if (lstat(canonical, &st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }

    if (remove(canonical) == 0) return true;
    return errno == ENOENT;
}

static bool board_config_upsert(DBClient* db, const char* cfg_name, const char* cfg_value) {
    if (!db || !cfg_name || !cfg_value) return false;

    char sql[512];
    int n = snprintf(sql, sizeof(sql),
        "SELECT cfg_value FROM board_config WHERE cfg_name = '%s'", cfg_name);
    if (n < 0 || (size_t)n >= sizeof(sql)) return false;

    ArrayList* list = db->getRecordsFromQuery(db, sql);
    if (!list) return false;

    if (list->getSize(list) > 0) {
        n = snprintf(sql, sizeof(sql),
            "UPDATE board_config SET cfg_value = '%s' WHERE cfg_name = '%s'",
            cfg_value, cfg_name);
    } else {
        n = snprintf(sql, sizeof(sql),
            "INSERT INTO board_config (cfg_name, cfg_value) VALUES ('%s', '%s')",
            cfg_name, cfg_value);
    }
    RELEASE((Object*)list);

    if (n < 0 || (size_t)n >= sizeof(sql)) return false;
    return db->sqlQuery(db, sql) != 0;
}

static bool BoardHandler_refresh_skin(BoardHandler* self) {
    if (!self || !self->db || !self->db->isConnected) return false;

    ArrayList* skin_list = self->db->getRecordsFromQuery(self->db, "SELECT cfg_value FROM board_config WHERE cfg_name = 'skin' LIMIT 1");
    if (!skin_list) return false;

    char current_skin[64] = "white";
    bool need_store = true;

    if (skin_list->getSize(skin_list) > 0) {
        HashMap* skin_row = (HashMap*)skin_list->get(skin_list, 0);
        String* val = (String*)skin_row->get(skin_row, "cfg_value");
        if (val && (strcmp(val->value, "white") == 0 || strcmp(val->value, "dark") == 0)) {
            int sn = snprintf(current_skin, sizeof(current_skin), "%s", val->value);
            if (sn >= 0 && (size_t)sn < sizeof(current_skin)) {
                need_store = false;
            }
        }
    }
    RELEASE((Object*)skin_list);

    char check_path[1024];
    int n = snprintf(check_path, sizeof(check_path), "%s/%s/skin/%s", self->base_dir, self->tpl_dir, current_skin);
    if (n < 0 || (size_t)n >= sizeof(check_path)) return false;

    struct stat st;
    bool valid = false;

    if (stat(check_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        int sn = snprintf(self->skin, sizeof(self->skin), "%s", current_skin);
        if (sn >= 0 && (size_t)sn < sizeof(self->skin)) valid = true;
    } else {
        n = snprintf(check_path, sizeof(check_path), "%s/%s/skin/white", self->base_dir, self->tpl_dir);
        if (n < 0 || (size_t)n >= sizeof(check_path)) return false;

        if (stat(check_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            int sn = snprintf(self->skin, sizeof(self->skin), "white");
            if (sn >= 0 && (size_t)sn < sizeof(self->skin)) {
                valid = true;
                need_store = true;
            }
        }
    }

    if (!valid) return false;

    if (need_store) {
        if (!board_config_upsert(self->db, "skin", self->skin)) {
            return false;
        }
    }

    char abs_tpl[1024];
    n = snprintf(abs_tpl, sizeof(abs_tpl), "%s/%s", self->base_dir, self->tpl_dir);
    if (n < 0 || (size_t)n >= sizeof(abs_tpl)) return false;

    n = snprintf(self->skinDir, sizeof(self->skinDir), "%s/skin/%s", abs_tpl, self->skin);
    if (n < 0 || (size_t)n >= sizeof(self->skinDir)) return false;

    return true;
}

static void BoardHandler_read_write(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    (void) req;
    if (req->method != HTTP_GET) {
        res->sendStatus(res, 405);
        return;
    }

    if (!BoardHandler_refresh_skin(self)) {
        res->setStatus(res, 500);
        res->sendText(res, "Skin configuration failed.");
        return;
    }

    JSONNode* resp = new_JSON_Object();
    char size_buf[64];
    double bytes = (double)self->max_upload_size;
    int sn = 0;

    if (bytes >= (1024.0 * 1024.0 * 1024.0)) {
        sn = snprintf(size_buf, sizeof(size_buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= (1024.0 * 1024.0)) {
        sn = snprintf(size_buf, sizeof(size_buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        sn = snprintf(size_buf, sizeof(size_buf), "%.1f KB", bytes / 1024.0);
    }

    if (sn < 0 || (size_t)sn >= sizeof(size_buf)) {
        RELEASE((Object*)resp);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer overflow.");
        return;
    }

    JSONNode* j_max_size = new_JSON_String(size_buf);
    resp->put(resp, "max_upload_size_str", (Object*)j_max_size);
    RELEASE((Object*)j_max_size);

    JSONNode* j_max_bytes = (JSONNode*)new_json_number((double)self->max_upload_size);
    resp->put(resp, "max_upload_size_bytes", (Object*)j_max_bytes);
    RELEASE((Object*)j_max_bytes);

    TemplateEngine* engine = new_TemplateEngine();
    char* html_out = NULL;
    if (engine) {
        char skinDir[512];
        int n = snprintf(skinDir, sizeof(skinDir), "%s/board_write.html", self->skinDir);
        if (n >= 0 && (size_t)n < sizeof(skinDir)) {
            html_out = engine->renderFile(engine, skinDir, resp);
        }
        RELEASE((Object*)engine);
    }
    RELEASE((Object*)resp);

    if (!html_out) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: Template rendering failed.");
        return;
    }
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "text/html; charset=utf-8");
    res->sendText(res, html_out);
    free(html_out);
}

static void BoardHandler_write(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (req->method != HTTP_POST) {
        res->sendStatus(res, 405);
        return;
    }
    if (!req->multipart) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request.");
        return;
    }
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }
    const char* title = MultipartResult_get_field(req->multipart, "title");
    const char* content = MultipartResult_get_field(req->multipart, "content");

    if (!title || !content || title[0] == '\0' || content[0] == '\0') {
        res->setStatus(res, 400);
        res->sendText(res, "Missing fields.");
        return;
    }

    HttpMultipartFile* attach = MultipartResult_get_file(req->multipart, "attach");
    char storage_path[512] = {0};
    bool file_saved = false;
    const char* original_filename = "untitled.bin";

    if (attach && attach->filename && attach->filename->c_str) {
        original_filename = attach->filename->c_str(attach->filename);
    }

    if (attach && attach->data && attach->size > 0) {
        if (self->file_size_exceeded(self, attach)) {
            if (logger) LOG_WARN(logger, "File too large: %zu > %zu", attach->size, self->max_upload_size);
            res->setStatus(res, 413);
            res->sendText(res, "File too large. Max size exceeded.");
            return;
        }
        file_saved = self->file_save(self, attach, storage_path, sizeof(storage_path));
        if (!file_saved) {
            res->setStatus(res, 500);
            res->sendText(res, "File save failed.");
            return;
        }
    }

    if (!self->db->beginTransaction(self->db)) {
        if (file_saved) self->file_delete(self, storage_path);
        res->setStatus(res, 500);
        res->sendText(res, "DB TX failed.");
        return;
    }

    char mid_buf[32];
    int sn1 = snprintf(mid_buf, sizeof(mid_buf), "1");
    if (sn1 < 0 || (size_t)sn1 >= sizeof(mid_buf)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, storage_path);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    String* s_mid = new_String(mid_buf);
    String* s_title = new_String(title);
    String* s_content = new_String(content);
    HashMap* post_data = new_HashMap(8);
    post_data->put(post_data, "member_id", (Object*)s_mid);
    post_data->put(post_data, "title", (Object*)s_title);
    post_data->put(post_data, "content", (Object*)s_content);
    RELEASE((Object*)s_mid);
    RELEASE((Object*)s_title);
    RELEASE((Object*)s_content);

    int post_ok = self->db->insertTable(self->db, "board_posts", post_data);
    long long new_post_id = self->db->last_insert_id;
    RELEASE((Object*)post_data);

    if (!post_ok || new_post_id <= 0 || new_post_id > INT_MAX) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, storage_path);
        res->setStatus(res, 500);
        res->sendText(res, "Invalid inserted post ID.");
        return;
    }

    int attach_ok = 1;
    if (file_saved) {
        char pid_buf[32];
        char sz_buf[32];
        int sn2 = snprintf(pid_buf, sizeof(pid_buf), "%lld", new_post_id);
        int sn3 = snprintf(sz_buf, sizeof(sz_buf), "%zu", attach->size);

        if (sn2 < 0 || (size_t)sn2 >= sizeof(pid_buf) || sn3 < 0 || (size_t)sn3 >= sizeof(sz_buf)) {
            self->db->rollback(self->db);
            self->file_delete(self, storage_path);
            res->setStatus(res, 500);
            res->sendText(res, "Buffer error.");
            return;
        }

        const char* mime_str = "application/octet-stream";
        if (attach->content_type && attach->content_type->c_str) {
            mime_str = attach->content_type->c_str(attach->content_type);
        }
        String* sa_pid = new_String(pid_buf);
        String* sa_mid = new_String(mid_buf);
        String* sa_org = new_String(original_filename);
        String* sa_path = new_String(storage_path);
        String* sa_size = new_String(sz_buf);
        String* sa_mime = new_String(mime_str);

        HashMap* att_data = new_HashMap(8);
        att_data->put(att_data, "post_id", (Object*)sa_pid);
        att_data->put(att_data, "member_id", (Object*)sa_mid);
        att_data->put(att_data, "file_name", (Object*)sa_org);
        att_data->put(att_data, "saved_name", (Object*)sa_path);
        att_data->put(att_data, "file_size", (Object*)sa_size);
        att_data->put(att_data, "mime_type", (Object*)sa_mime);

        RELEASE((Object*)sa_pid);
        RELEASE((Object*)sa_mid);
        RELEASE((Object*)sa_org);
        RELEASE((Object*)sa_path);
        RELEASE((Object*)sa_size);
        RELEASE((Object*)sa_mime);

        attach_ok = self->db->insertTable(self->db, "board_attachments", att_data);
        RELEASE((Object*)att_data);
    }

    if (file_saved && !attach_ok) {
        self->db->rollback(self->db);
        self->file_delete(self, storage_path);
        res->setStatus(res, 500);
        res->sendText(res, "Attachment DB fail.");
        return;
    }

    if (!self->db->commit(self->db)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, storage_path);
        res->setStatus(res, 500);
        res->sendText(res, "Commit fail.");
        return;
    }

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    JSONNode* j_pid = (JSONNode*)new_json_number((double)new_post_id);
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    resp->put(resp, "post_id", (Object*)j_pid);
    RELEASE((Object*)j_pid);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_list(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    if (!BoardHandler_refresh_skin(self)) {
        res->setStatus(res, 500);
        res->sendText(res, "Skin configuration failed.");
        return;
    }

    int limit = 20;
    ArrayList* limit_list = self->db->getRecordsFromQuery(self->db, "SELECT cfg_value FROM board_config WHERE cfg_name = 'board_list' LIMIT 1");

    if (!limit_list) {
        res->setStatus(res, 500);
        res->sendText(res, "Board config query failed.");
        return;
    }

    if (limit_list->getSize(limit_list) > 0) {
        HashMap* limit_row = (HashMap*)limit_list->get(limit_list, 0);
        String* val = (String*)limit_row->get(limit_row, "cfg_value");
        if (val && val->value[0] != '\0') {
            int req_limit = 0;
            if (parse_positive_int(val->value, &req_limit)) {
                /*페이지당 목록수 */
                switch(req_limit) {
                    case 5:
                    case 10:
                    case 15:
                    case 20:
                    case 30:
                    case 40:
                    case 50:
                        limit = req_limit;
                        break;
                    default:
                        limit = 20;
                        break;
                }
            }
        }
    } else {
        if (!board_config_upsert(self->db, "board_list", "20")) {
            RELEASE((Object*)limit_list);
            res->setStatus(res, 500);
            res->sendText(res, "Board config initialization failed.");
            return;
        }
    }
    RELEASE((Object*)limit_list);

    const char* page_str = req->query ? hashmap_get_str(req->query, "page") : NULL;
    int page = 1;
    if (page_str) {
        if (!parse_positive_int(page_str, &page)) {
            page = 1;
        }
    }

    const char* search_type = req->query ? hashmap_get_str(req->query, "search_type") : NULL;
    const char* keyword = req->query ? hashmap_get_str(req->query, "keyword") : NULL;
    if (!search_type || (strcmp(search_type, "title") != 0 && strcmp(search_type, "content") != 0 && strcmp(search_type, "title_content") != 0 && strcmp(search_type, "writer") != 0)) {
        search_type = "title";
    }
    if (!keyword) keyword = "";

    size_t kw_len = BoardHandler_bounded_strlen(keyword, BOARD_SEARCH_MAX_BYTES + 1);
    if (kw_len > BOARD_SEARCH_MAX_BYTES) {
        res->setStatus(res, 400);
        res->sendText(res, "Search keyword too long.");
        return;
    }

    char like_keyword[BOARD_LIKE_MAX_BYTES];
    if (!BoardHandler_escape_like(keyword, like_keyword, sizeof(like_keyword))) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid search keyword.");
        return;
    }
    char* safe_sql_keyword = self->db->escape_string(self->db, like_keyword);
    if (!safe_sql_keyword) {
        res->setStatus(res, 500);
        res->sendText(res, "Search escaping failed.");
        return;
    }

    char where_clause[BOARD_WHERE_MAX_BYTES];
    int used = snprintf(where_clause, sizeof(where_clause), "p.is_deleted = 0");
    if (used < 0 || (size_t)used >= sizeof(where_clause)) {
        free(safe_sql_keyword);
        res->setStatus(res, 500);
        res->sendText(res, "Error constructing query.");
        return;
    }

    if (safe_sql_keyword[0] != '\0') {
        int written = 0;
        size_t remain = sizeof(where_clause) - (size_t)used;
        if (strcmp(search_type, "content") == 0) {
            written = snprintf(where_clause + used, remain, " AND p.content LIKE '%%%s%%' ESCAPE '='", safe_sql_keyword);
        } else if (strcmp(search_type, "title_content") == 0) {
            written = snprintf(where_clause + used, remain, " AND (p.title LIKE '%%%s%%' ESCAPE '=' OR p.content LIKE '%%%s%%' ESCAPE '=')", safe_sql_keyword, safe_sql_keyword);
        } else if (strcmp(search_type, "writer") == 0) {
            written = snprintf(where_clause + used, remain, " AND COALESCE(m.nickname, '익명') LIKE '%%%s%%' ESCAPE '='", safe_sql_keyword);
        } else {
            written = snprintf(where_clause + used, remain, " AND p.title LIKE '%%%s%%' ESCAPE '='", safe_sql_keyword);
        }

        if (written < 0 || (size_t)written >= remain) {
            free(safe_sql_keyword);
            res->setStatus(res, 500);
            res->sendText(res, "Error constructing query.");
            return;
        }
    }
    free(safe_sql_keyword);
    safe_sql_keyword = NULL;

    int total_count = 0;
    char count_sql[BOARD_COUNT_SQL_MAX_BYTES];
    int cn = snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) AS cnt FROM board_posts p LEFT JOIN board_members m ON p.member_id = m.id WHERE %s", where_clause);
    if (cn < 0 || (size_t)cn >= sizeof(count_sql)) {
        res->setStatus(res, 500);
        res->sendText(res, "Count query buffer overflow.");
        return;
    }

    ArrayList* cnt_list = self->db->getRecordsFromQuery(self->db, count_sql);
    if (!cnt_list) {
        res->setStatus(res, 500);
        res->sendText(res, "Board count query failed.");
        return;
    }
    if (cnt_list->getSize(cnt_list) != 1) {
        RELEASE((Object*)cnt_list);
        res->setStatus(res, 500);
        res->sendText(res, "Invalid count query result.");
        return;
    }

    HashMap* cnt_map = (HashMap*)cnt_list->get(cnt_list, 0);
    String* cnt_val = cnt_map ? (String*)cnt_map->get(cnt_map, "cnt") : NULL;
    if (!cnt_val || !parse_nonnegative_int(cnt_val->value, &total_count)) {
        RELEASE((Object*)cnt_list);
        res->setStatus(res, 500);
        res->sendText(res, "Invalid count value.");
        return;
    }
    RELEASE((Object*)cnt_list);

    int total_pages = total_count > 0 ? ((total_count - 1) / limit) + 1 : 1;

    if (page > total_pages) {
        page = total_pages;
    }
    long offset = (long)(page - 1) * (long)limit;

    char is_new_expr[128];
    int en = 0;
    if (strcmp(self->db->db_type, "PGSQL") == 0) {
        en = snprintf(is_new_expr, sizeof(is_new_expr),
            "CASE WHEN p.created_at >= NOW() - INTERVAL '1 day' THEN 1 ELSE 0 END");
    } else if (strcmp(self->db->db_type, "SQLITE") == 0) {
        en = snprintf(is_new_expr, sizeof(is_new_expr),
            "CASE WHEN p.created_at >= datetime('now', 'localtime', '-1 day') THEN 1 ELSE 0 END");
    } else {
        en = snprintf(is_new_expr, sizeof(is_new_expr),
            "CASE WHEN p.created_at >= NOW() - INTERVAL 1 DAY THEN 1 ELSE 0 END");
    }

    if (en < 0 || (size_t)en >= sizeof(is_new_expr)) {
        res->setStatus(res, 500);
        res->sendText(res, "Expr buffer overflow.");
        return;
    }

    char sql[BOARD_LIST_SQL_MAX_BYTES];
    int sn = snprintf(sql, sizeof(sql),
        "SELECT p.id, p.title, COALESCE(m.nickname, '익명') AS nickname, p.view_count, p.created_at, "
        "COALESCE(c.comment_count, 0) AS comment_count, "
        "%s AS is_new, "
        "CASE WHEN EXISTS (SELECT 1 FROM board_attachments a WHERE a.post_id = p.id AND a.is_deleted = 0) THEN 1 ELSE 0 END AS has_attachment "
        "FROM board_posts p "
        "LEFT JOIN board_members m ON p.member_id = m.id "
        "LEFT JOIN (SELECT post_id, COUNT(*) AS comment_count FROM board_comments WHERE is_deleted = 0 GROUP BY post_id) c ON c.post_id = p.id "
        "WHERE %s ORDER BY p.id DESC LIMIT %d OFFSET %ld",
        is_new_expr, where_clause, limit, offset);

    if (sn < 0 || (size_t)sn >= sizeof(sql)) {
        res->setStatus(res, 500);
        res->sendText(res, "List query buffer overflow.");
        return;
    }

    ArrayList* posts = self->db->getRecordsFromQuery(self->db, sql);
    if (!posts) {
        res->setStatus(res, 500);
        res->sendText(res, "Board list query failed (Null list returned).");
        return;
    }

    JSONNode* resp = new_JSON_Object();

    String *s_dbtype = new_String(self->db->db_type);
    JSONNode* j_dbtype = new_JSON_String(s_dbtype->value);
    resp->put(resp, "db_type", (Object*)j_dbtype);
    RELEASE((Object*)j_dbtype);
    RELEASE((Object*)s_dbtype);

    JSONNode* j_page = (JSONNode*)new_json_number((double)page);
    resp->put(resp, "page", (Object*)j_page);
    RELEASE((Object*)j_page);

    JSONNode* j_limit = (JSONNode*)new_json_number((double)limit);
    resp->put(resp, "limit", (Object*)j_limit);
    RELEASE((Object*)j_limit);

    JSONNode* j_total = (JSONNode*)new_json_number((double)total_count);
    resp->put(resp, "total_count", (Object*)j_total);
    RELEASE((Object*)j_total);

    JSONNode* j_keyword = new_JSON_String(keyword);
    resp->put(resp, "keyword", (Object*)j_keyword);
    RELEASE((Object*)j_keyword);

    int block_size = 5;
    int start_page = ((page - 1) / block_size) * block_size + 1;
    int end_page = start_page + block_size - 1;
    if (end_page > total_pages) end_page = total_pages;

    JSONNode* j_pages = new_JSON_Array();
    for (int p = start_page; p <= end_page; p++) {
        JSONNode* p_item = new_JSON_Object();

        JSONNode* jp_num = (JSONNode*)new_json_number((double)p);
        p_item->put(p_item, "num", (Object*)jp_num);
        RELEASE((Object*)jp_num);

        JSONNode* jp_active = new_JSON_String(p == page ? "active" : "");
        p_item->put(p_item, "is_active", (Object*)jp_active);
        RELEASE((Object*)jp_active);

        JSONNode* jp_st = new_JSON_String(search_type);
        p_item->put(p_item, "search_type_val", (Object*)jp_st);
        RELEASE((Object*)jp_st);

        JSONNode* jp_kw = new_JSON_String(keyword);
        p_item->put(p_item, "keyword", (Object*)jp_kw);
        RELEASE((Object*)jp_kw);

        j_pages->add(j_pages, (Object*)p_item);
        RELEASE((Object*)p_item);
    }
    resp->put(resp, "pages", (Object*)j_pages);
    RELEASE((Object*)j_pages);

    JSONNode* jt_pages = (JSONNode*)new_json_number((double)total_pages);
    resp->put(resp, "total_pages", (Object*)jt_pages);
    RELEASE((Object*)jt_pages);

    JSONNode* j_has_prev = (JSONNode*)new_json_number((double)(start_page > 1 ? 1 : 0));
    resp->put(resp, "has_prev", (Object*)j_has_prev);
    RELEASE((Object*)j_has_prev);

    JSONNode* j_prev_page = (JSONNode*)new_json_number((double)(start_page - 1));
    resp->put(resp, "prev_page", (Object*)j_prev_page);
    RELEASE((Object*)j_prev_page);

    JSONNode* j_has_next = (JSONNode*)new_json_number((double)(end_page < total_pages ? 1 : 0));
    resp->put(resp, "has_next", (Object*)j_has_next);
    RELEASE((Object*)j_has_next);

    JSONNode* j_next_page = (JSONNode*)new_json_number((double)(end_page + 1));
    resp->put(resp, "next_page", (Object*)j_next_page);
    RELEASE((Object*)j_next_page);

    JSONNode* j_search_type_val = new_JSON_String(search_type);
    resp->put(resp, "search_type_val", (Object*)j_search_type_val);
    RELEASE((Object*)j_search_type_val);

    JSONNode* j_sel = new_JSON_String("selected");
    JSONNode* j_emp = new_JSON_String("");

    resp->put(resp, "limit_5_selected",  (limit == 5)  ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "limit_10_selected", (limit == 10) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "limit_15_selected", (limit == 15) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "limit_20_selected", (limit == 20) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "limit_30_selected", (limit == 30) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "limit_40_selected", (limit == 40) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "limit_50_selected", (limit == 50) ? (Object*)j_sel : (Object*)j_emp);

    resp->put(resp, "search_title_selected", (strcmp(search_type, "title") == 0) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "search_content_selected", (strcmp(search_type, "content") == 0) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "search_title_content_selected", (strcmp(search_type, "title_content") == 0) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "search_writer_selected", (strcmp(search_type, "writer") == 0) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "skin_white_selected", (strcmp(self->skin, "white") == 0) ? (Object*)j_sel : (Object*)j_emp);
    resp->put(resp, "skin_dark_selected", (strcmp(self->skin, "dark") == 0) ? (Object*)j_sel : (Object*)j_emp);
    RELEASE((Object*)j_sel);
    RELEASE((Object*)j_emp);

    JSONNode* j_posts = new_JSON_Array();

    for (int i = 0; i < posts->getSize(posts); i++) {
        HashMap* row = (HashMap*)posts->get(posts, i);
        String* id = (String*)row->get(row, "id");
        String* title = (String*)row->get(row, "title");
        String* nickname = (String*)row->get(row, "nickname");
        String* view_count = (String*)row->get(row, "view_count");
        String* created_at = (String*)row->get(row, "created_at");
        char regdate[64] = {0};
        //microsends 단위는 자르기. 초까지만 나오도록 수정
        if (created_at && created_at->value) {
            const char *src = created_at->value;
            const char *dot = strchr(src, '.');
            size_t len = dot ? (size_t)(dot - src) : strlen(src);
            if (len >= sizeof(regdate))
                len = sizeof(regdate) - 1;
            memcpy(regdate, src, len);
            regdate[len] = '\0';
        }
        String* s_cmt_cnt = (String*)row->get(row, "comment_count");
        String* s_is_new = (String*)row->get(row, "is_new");
        String* s_has_att = (String*)row->get(row, "has_attachment");

        int p_id_val = 0, p_vc_val = 0, cmt_cnt_val = 0, is_new_val = 0, has_att_val = 0;
        if (!id || !parse_positive_int(id->value, &p_id_val) ||
            !view_count || !parse_nonnegative_int(view_count->value, &p_vc_val) ||
            !s_cmt_cnt || !parse_nonnegative_int(s_cmt_cnt->value, &cmt_cnt_val) ||
            !s_is_new || !parse_nonnegative_int(s_is_new->value, &is_new_val) ||
            !s_has_att || !parse_nonnegative_int(s_has_att->value, &has_att_val) ||
            (is_new_val != 0 && is_new_val != 1) ||
            (has_att_val != 0 && has_att_val != 1)) {

            RELEASE((Object*)posts);
            RELEASE((Object*)j_posts);
            RELEASE((Object*)resp);
            res->setStatus(res, 500);
            res->sendText(res, "Database corruption detected (invalid numeric data in list).");
            return;
        }

        JSONNode* item = new_JSON_Object();
        JSONNode* j_item_id = (JSONNode*)new_json_number((double)p_id_val);
        item->put(item, "id", (Object*)j_item_id);
        RELEASE((Object*)j_item_id);

        int virtual_no = total_count - (int)offset - i;
        JSONNode* j_item_no = (JSONNode*)new_json_number((double)virtual_no);
        item->put(item, "no", (Object*)j_item_no);
        RELEASE((Object*)j_item_no);

        JSONNode* j_item_title = new_JSON_String(title ? title->value : "");
        item->put(item, "title", (Object*)j_item_title);
        RELEASE((Object*)j_item_title);

        JSONNode* j_item_nick = new_JSON_String(nickname ? nickname->value : "익명");
        item->put(item, "nickname", (Object*)j_item_nick);
        RELEASE((Object*)j_item_nick);

        JSONNode* j_item_views = (JSONNode*)new_json_number((double)p_vc_val);
        item->put(item, "view_count", (Object*)j_item_views);
        RELEASE((Object*)j_item_views);

        JSONNode* j_item_date = new_JSON_String(regdate);
        item->put(item, "created_at", (Object*)j_item_date);
        RELEASE((Object*)j_item_date);

        char cmt_badge_buf[32] = {0};
        int cb_n = snprintf(cmt_badge_buf, sizeof(cmt_badge_buf), cmt_cnt_val <= 0 ? "[0]" : "[%d]", cmt_cnt_val);
        if (cb_n < 0 || (size_t)cb_n >= sizeof(cmt_badge_buf)) cmt_badge_buf[0] = '\0';

        JSONNode* j_cmt_badge = new_JSON_String(cmt_badge_buf);
        item->put(item, "comment_badge", (Object*)j_cmt_badge);
        RELEASE((Object*)j_cmt_badge);

        const char* new_badge_str = (is_new_val == 1) ? "NEW" : "";
        JSONNode* j_new_badge = new_JSON_String(new_badge_str);
        item->put(item, "new_badge", (Object*)j_new_badge);
        RELEASE((Object*)j_new_badge);

        const char* attach_icon_str = (has_att_val == 1) ? "📎" : "";
        JSONNode* j_attach_icon = new_JSON_String(attach_icon_str);
        item->put(item, "attach_icon", (Object*)j_attach_icon);
        RELEASE((Object*)j_attach_icon);

        j_posts->add(j_posts, (Object*)item);
        RELEASE((Object*)item);
    }
    RELEASE((Object*)posts);

    resp->put(resp, "posts", (Object*)j_posts);
    RELEASE((Object*)j_posts);

    TemplateEngine* engine = new_TemplateEngine();
    char* html_out = NULL;
    if (engine) {
        char skinDir[512];
        int n = snprintf(skinDir, sizeof(skinDir), "%s/index.html", self->skinDir);
        if (n >= 0 && (size_t)n < sizeof(skinDir)) {
            html_out = engine->renderFile(engine, skinDir, resp);
        }
        RELEASE((Object*)engine);
    }

    if (html_out) {
        res->setStatus(res, 200);
        res->setHeader(res, "Content-Type", "text/html; charset=utf-8");
        res->sendText(res, html_out);
        free(html_out);
    } else {
        res->setStatus(res, 500);
        res->sendText(res, "Template rendering failed.");
    }
    RELEASE((Object*)resp);
}

static void BoardHandler_detail(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    if (!BoardHandler_refresh_skin(self)) {
        res->setStatus(res, 500);
        res->sendText(res, "Skin configuration failed.");
        return;
    }

    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int post_id = 0;
    if (!parse_positive_int(id_str, &post_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid post ID.");
        return;
    }

    const char* mode = req->query ? hashmap_get_str(req->query, "mode") : NULL;
    const char* mode_class = (mode && strcmp(mode, "modify") == 0) ? "mode-modify" : "mode-read";
    const char* readonly_attr = (mode && strcmp(mode, "modify") == 0) ? "" : "readonly";

    ArrayList* p_list = NULL;
    ArrayList* att_list = NULL;
    ArrayList* cmt_list = NULL;
    JSONNode* resp = NULL;

    char sql_sel[512];
    int sn = snprintf(sql_sel, sizeof(sql_sel), "SELECT p.id, p.title, p.content, p.view_count, p.created_at, COALESCE(m.nickname, '익명') AS nickname FROM board_posts p LEFT JOIN board_members m ON p.member_id = m.id WHERE p.id = %d AND p.is_deleted = 0", post_id);
    if (sn < 0 || (size_t)sn >= sizeof(sql_sel)) {
        res->setStatus(res, 500);
        res->sendText(res, "Detail query buffer overflow.");
        return;
    }

    p_list = self->db->getRecordsFromQuery(self->db, sql_sel);
    if (!p_list) {
        res->setStatus(res, 500);
        res->sendText(res, "Detail query failed.");
        return;
    }
    if (p_list->getSize(p_list) == 0) {
        RELEASE((Object*)p_list); p_list = NULL;
        res->setStatus(res, 404);
        res->setHeader(res, "Content-Type", "text/html; charset=utf-8");
        res->sendText(res, "<script>alert('존재하지 않거나 삭제된 게시글입니다.'); location.href='/board/list';</script>");
        return;
    }
    HashMap* post = (HashMap*)p_list->get(p_list, 0);

    if (strcmp(mode_class, "mode-read") == 0) {
        char sql_upd[128];
        int un = snprintf(sql_upd, sizeof(sql_upd), "UPDATE board_posts SET view_count = view_count + 1, updated_at = updated_at WHERE id = %d", post_id);
        if (un < 0 || (size_t)un >= sizeof(sql_upd)) {
            RELEASE((Object*)p_list); p_list = NULL;
            res->setStatus(res, 500);
            res->sendText(res, "View update query buffer overflow.");
            return;
        }
        if (!self->db->sqlQuery(self->db, sql_upd)) {
            if (logger) {
                LOG_WARN(logger, "View count update failed: post_id=%d", post_id);
            }
        }
    }

    char sql_att[256];
    int an = snprintf(sql_att, sizeof(sql_att), "SELECT id, file_name, file_size FROM board_attachments WHERE post_id = %d AND is_deleted = 0", post_id);
    if (an < 0 || (size_t)an >= sizeof(sql_att)) {
        RELEASE((Object*)p_list); p_list = NULL;
        res->setStatus(res, 500);
        res->sendText(res, "Attachment query buffer overflow.");
        return;
    }

    att_list = self->db->getRecordsFromQuery(self->db, sql_att);
    if (!att_list) {
        RELEASE((Object*)p_list); p_list = NULL;
        res->setStatus(res, 500);
        res->sendText(res, "Attachment query failed.");
        return;
    }

    char sql_cmt[512];
    int c_n = snprintf(sql_cmt, sizeof(sql_cmt), "SELECT c.id, c.content, c.created_at, c.parent_id, COALESCE(m.nickname, '익명') AS nickname FROM board_comments c LEFT JOIN board_members m ON c.member_id = m.id WHERE c.post_id = %d AND c.is_deleted = 0 ORDER BY c.id ASC", post_id);
    if (c_n < 0 || (size_t)c_n >= sizeof(sql_cmt)) {
        RELEASE((Object*)att_list); att_list = NULL;
        RELEASE((Object*)p_list); p_list = NULL;
        res->setStatus(res, 500);
        res->sendText(res, "Comment query buffer overflow.");
        return;
    }

    cmt_list = self->db->getRecordsFromQuery(self->db, sql_cmt);
    if (!cmt_list) {
        RELEASE((Object*)att_list); att_list = NULL;
        RELEASE((Object*)p_list); p_list = NULL;
        res->setStatus(res, 500);
        res->sendText(res, "Comment query failed.");
        return;
    }

    resp = new_JSON_Object();

    char size_buf[64];
    double bytes = (double)self->max_upload_size;
    int sb_n = 0;
    if (bytes >= (1024.0 * 1024.0 * 1024.0)) {
        sb_n = snprintf(size_buf, sizeof(size_buf), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= (1024.0 * 1024.0)) {
        sb_n = snprintf(size_buf, sizeof(size_buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        sb_n = snprintf(size_buf, sizeof(size_buf), "%.1f KB", bytes / 1024.0);
    }

    if (sb_n < 0 || (size_t)sb_n >= sizeof(size_buf)) {
        memcpy(size_buf, "Unknown", sizeof("Unknown"));
    }

    JSONNode* j_max_size = new_JSON_String(size_buf);
    resp->put(resp, "max_upload_size_str", (Object*)j_max_size);
    RELEASE((Object*)j_max_size);

    JSONNode* j_max_bytes = (JSONNode*)new_json_number((double)self->max_upload_size);
    resp->put(resp, "max_upload_size_bytes", (Object*)j_max_bytes);
    RELEASE((Object*)j_max_bytes);

    JSONNode* j_mode_class = new_JSON_String(mode_class);
    resp->put(resp, "mode_class", (Object*)j_mode_class);
    RELEASE((Object*)j_mode_class);

    JSONNode* j_readonly = new_JSON_String(readonly_attr);
    resp->put(resp, "readonly_attr", (Object*)j_readonly);
    RELEASE((Object*)j_readonly);

    String* s_id = (String*)post->get(post, "id");
    String* s_title = (String*)post->get(post, "title");
    String* s_content = (String*)post->get(post, "content");
    String* s_nickname = (String*)post->get(post, "nickname");
    String* s_views = (String*)post->get(post, "view_count");
    String* s_created = (String*)post->get(post, "created_at");
    char regdate[64] = {0};
    //microsends 단위는 자르기. 초까지만 나오도록 수정
    if (s_created && s_created->value) {
        const char *src = s_created->value;
        const char *dot = strchr(src, '.');
        size_t len = dot ? (size_t)(dot - src) : strlen(src);
        if (len >= sizeof(regdate))
            len = sizeof(regdate) - 1;
        memcpy(regdate, src, len);
        regdate[len] = '\0';
    }
    String* s_dbtype = new_String(self->db->db_type);

    int p_id_val = 0, p_vc_val = 0;
    if (!s_id || !parse_positive_int(s_id->value, &p_id_val) ||
        !s_views || !parse_nonnegative_int(s_views->value, &p_vc_val)) {
        goto fail_corrupt;
    }

    JSONNode* jp_id = (JSONNode*)new_json_number((double)p_id_val);
    resp->put(resp, "id", (Object*)jp_id);
    RELEASE((Object*)jp_id);

    JSONNode* jp_title = new_JSON_String(s_title ? s_title->value : "");
    resp->put(resp, "title", (Object*)jp_title);
    RELEASE((Object*)jp_title);

    JSONNode* jp_content = new_JSON_String(s_content ? s_content->value : "");
    resp->put(resp, "content", (Object*)jp_content);
    RELEASE((Object*)jp_content);

    JSONNode* jp_nick = new_JSON_String(s_nickname ? s_nickname->value : "익명");
    resp->put(resp, "nickname", (Object*)jp_nick);
    RELEASE((Object*)jp_nick);

    JSONNode* db_type = new_JSON_String(s_dbtype->value);
    resp->put(resp, "db_type", (Object*)db_type);
    RELEASE((Object*)db_type);
    RELEASE((Object*)s_dbtype);

    JSONNode* jp_views = (JSONNode*)new_json_number((double)p_vc_val);
    resp->put(resp, "view_count", (Object*)jp_views);
    RELEASE((Object*)jp_views);

    JSONNode* jp_date = new_JSON_String(regdate);
    resp->put(resp, "created_at", (Object*)jp_date);
    RELEASE((Object*)jp_date);

    JSONNode* j_atts = new_JSON_Array();

    for (int i = 0; i < att_list->getSize(att_list); i++) {
        HashMap* att = (HashMap*)att_list->get(att_list, i);
        String* a_id = (String*)att->get(att, "id");
        String* a_name = (String*)att->get(att, "file_name");
        String* a_size = (String*)att->get(att, "file_size");

        int att_id_val = 0, att_sz_val = 0;
        if (!a_id || !parse_positive_int(a_id->value, &att_id_val) ||
            !a_size || !parse_nonnegative_int(a_size->value, &att_sz_val)) {
            RELEASE((Object*)j_atts);
            goto fail_corrupt;
        }

        JSONNode* j_att = new_JSON_Object();
        JSONNode* ja_id = (JSONNode*)new_json_number((double)att_id_val);
        j_att->put(j_att, "id", (Object*)ja_id);
        RELEASE((Object*)ja_id);

        JSONNode* ja_name = new_JSON_String(a_name ? a_name->value : "");
        j_att->put(j_att, "file_name", (Object*)ja_name);
        RELEASE((Object*)ja_name);

        JSONNode* ja_size = (JSONNode*)new_json_number((double)att_sz_val);
        j_att->put(j_att, "size", (Object*)ja_size);
        RELEASE((Object*)ja_size);

        j_atts->add(j_atts, (Object*)j_att);
        RELEASE((Object*)j_att);
    }
    resp->put(resp, "attachments", (Object*)j_atts);
    RELEASE((Object*)j_atts);

    int comment_count = cmt_list->getSize(cmt_list);
    JSONNode* j_comment_count = (JSONNode*)new_json_number((double)comment_count);
    resp->put(resp, "comment_count", (Object*)j_comment_count);
    RELEASE((Object*)j_comment_count);

    JSONNode* j_cmts = new_JSON_Array();

    for (int i = 0; i < cmt_list->getSize(cmt_list); i++) {
        HashMap* cmt = (HashMap*)cmt_list->get(cmt_list, i);
        String* c_id = (String*)cmt->get(cmt, "id");
        String* c_pid = (String*)cmt->get(cmt, "parent_id");
        String* c_nick = (String*)cmt->get(cmt, "nickname");
        String* c_cont = (String*)cmt->get(cmt, "content");
        String* c_date = (String*)cmt->get(cmt, "created_at");

        int c_id_val = 0;
        if (!c_id || !parse_positive_int(c_id->value, &c_id_val)) {
            RELEASE((Object*)j_cmts);
            goto fail_corrupt;
        }

        int c_pid_val = 0;
        if (c_pid && c_pid->value[0] != '\0' && !parse_positive_int(c_pid->value, &c_pid_val)) {
            RELEASE((Object*)j_cmts);
            goto fail_corrupt;
        }

        JSONNode* j_cmt = new_JSON_Object();
        JSONNode* jc_id = (JSONNode*)new_json_number((double)c_id_val);
        j_cmt->put(j_cmt, "id", (Object*)jc_id);
        RELEASE((Object*)jc_id);

        JSONNode* jc_class = new_JSON_String((c_pid && c_pid->value[0] != '\0') ? "reply" : "");
        j_cmt->put(j_cmt, "comment_class", (Object*)jc_class);
        RELEASE((Object*)jc_class);

        JSONNode* jc_prefix = new_JSON_String((c_pid && c_pid->value[0] != '\0') ? "↳ " : "");
        j_cmt->put(j_cmt, "reply_prefix", (Object*)jc_prefix);
        RELEASE((Object*)jc_prefix);

        JSONNode* jc_nick = new_JSON_String(c_nick ? c_nick->value : "익명");
        j_cmt->put(j_cmt, "nickname", (Object*)jc_nick);
        RELEASE((Object*)jc_nick);

        JSONNode* jc_cont = new_JSON_String(c_cont ? c_cont->value : "");
        j_cmt->put(j_cmt, "content", (Object*)jc_cont);
        RELEASE((Object*)jc_cont);

        JSONNode* jc_date = new_JSON_String(c_date ? c_date->value : "");
        j_cmt->put(j_cmt, "created_at", (Object*)jc_date);
        RELEASE((Object*)jc_date);

        j_cmts->add(j_cmts, (Object*)j_cmt);
        RELEASE((Object*)j_cmt);
    }
    resp->put(resp, "comments", (Object*)j_cmts);
    RELEASE((Object*)j_cmts);

    TemplateEngine* engine = new_TemplateEngine();
    char* html_out = NULL;
    if (engine) {
        char templatePath[512];
        int n = snprintf(templatePath, sizeof(templatePath), "%s/board_read.html", self->skinDir);
        if (n >= 0 && (size_t)n < sizeof(templatePath)) {
            html_out = engine->renderFile(engine, templatePath, resp);
        }
        RELEASE((Object*)engine);
    }

    if (html_out) {
        res->setStatus(res, 200);
        res->setHeader(res, "Content-Type", "text/html; charset=utf-8");
        res->sendText(res, html_out);
        free(html_out);
    } else {
        res->setStatus(res, 500);
        res->sendText(res, "Template rendering failed.");
    }

    if (att_list) { RELEASE((Object*)att_list); att_list = NULL; }
    if (cmt_list) { RELEASE((Object*)cmt_list); cmt_list = NULL; }
    if (p_list)   { RELEASE((Object*)p_list); p_list = NULL; }
    if (resp)     { RELEASE((Object*)resp); resp = NULL; }
    return;

fail_corrupt:
    if (att_list) { RELEASE((Object*)att_list); att_list = NULL; }
    if (cmt_list) { RELEASE((Object*)cmt_list); cmt_list = NULL; }
    if (p_list)   { RELEASE((Object*)p_list); p_list = NULL; }
    if (resp)     { RELEASE((Object*)resp); resp = NULL; }

    res->setStatus(res, 500);
    res->sendText(res, "Database corruption detected (invalid numeric data).");
}

static void BoardHandler_modify(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (req->method != HTTP_PUT) {
        res->sendStatus(res, 405);
        return;
    }
    if (!req->multipart) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request.");
        return;
    }
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int post_id = 0;
    if (!parse_positive_int(id_str, &post_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid ID.");
        return;
    }

    const char* title = MultipartResult_get_field(req->multipart, "title");
    const char* content = MultipartResult_get_field(req->multipart, "content");
    if (!title || !content || title[0] == '\0' || content[0] == '\0') {
        res->setStatus(res, 400);
        res->sendText(res, "Missing fields.");
        return;
    }

    HttpMultipartFile* attach = MultipartResult_get_file(req->multipart, "attach");
    char new_path[512] = {0};
    bool file_saved = false;

    if (attach && attach->data && attach->size > 0) {
        if (self->file_size_exceeded(self, attach)) {
            if (logger) LOG_WARN(logger, "File too large: %zu > %zu", attach->size, self->max_upload_size);
            res->setStatus(res, 413);
            res->sendText(res, "File too large. Max size exceeded.");
            return;
        }

        file_saved = self->file_save(self, attach, new_path, sizeof(new_path));
        if (!file_saved) {
            res->setStatus(res, 500);
            res->sendText(res, "File save failed.");
            return;
        }
    }

    if (!self->db->beginTransaction(self->db)) {
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "DB TX init failed.");
        return;
    }

    char sql_check[128];
    int sc_n = snprintf(sql_check, sizeof(sql_check), "SELECT id FROM board_posts WHERE id = %d AND is_deleted = 0", post_id);
    if (sc_n < 0 || (size_t)sc_n >= sizeof(sql_check)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "Check query buffer overflow.");
        return;
    }

    ArrayList* chk_list = self->db->getRecordsFromQuery(self->db, sql_check);
    if (!chk_list) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "Check query failed.");
        return;
    }
    if (chk_list->getSize(chk_list) == 0) {
        RELEASE((Object*)chk_list);
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 404);
        res->sendText(res, "Not found.");
        return;
    }
    RELEASE((Object*)chk_list);

    char old_path[512] = {0};
    bool has_old_row = false;
    char sql_old[256];
    int so_n = snprintf(sql_old, sizeof(sql_old), "SELECT saved_name FROM board_attachments WHERE post_id = %d AND is_deleted = 0 LIMIT 1", post_id);
    if (so_n < 0 || (size_t)so_n >= sizeof(sql_old)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "Old att query buffer overflow.");
        return;
    }

    ArrayList* old_att_list = self->db->getRecordsFromQuery(self->db, sql_old);
    if (!old_att_list) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "Old attachment query failed.");
        return;
    }

    if (old_att_list->getSize(old_att_list) > 0) {
        has_old_row = true;
        HashMap* old_att = (HashMap*)old_att_list->get(old_att_list, 0);
        String* saved = (String*)old_att->get(old_att, "saved_name");
        if (saved && saved->value[0] != '\0') {
            int n = snprintf(old_path, sizeof(old_path), "%s", saved->value);
            if (n < 0 || (size_t)n >= sizeof(old_path)) {
                RELEASE((Object*)old_att_list);
                self->db->rollback(self->db);
                if (file_saved) self->file_delete(self, new_path);
                res->setStatus(res, 500);
                res->sendText(res, "Attachment path too long.");
                return;
            }
        }
    }
    RELEASE((Object*)old_att_list);

    char pid_buf[64];
    int pb_n = snprintf(pid_buf, sizeof(pid_buf), "id=%d AND is_deleted=0", post_id);
    if (pb_n < 0 || (size_t)pb_n >= sizeof(pid_buf)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    String* s_title = new_String(title);
    String* s_content = new_String(content);
    HashMap* upd_data = new_HashMap(8);
    upd_data->put(upd_data, "title", (Object*)s_title);
    upd_data->put(upd_data, "content", (Object*)s_content);
    RELEASE((Object*)s_title);
    RELEASE((Object*)s_content);

    int update_ok = self->db->updateTable(self->db, "board_posts", upd_data, pid_buf);
    RELEASE((Object*)upd_data);

    int attach_ok = 1;
    if (update_ok && file_saved) {
        const char* original_filename = "untitled.bin";
        if (attach->filename && attach->filename->c_str) {
            original_filename = attach->filename->c_str(attach->filename);
        }
        char sz_buf[32];
        int sz_n = snprintf(sz_buf, sizeof(sz_buf), "%zu", attach->size);
        const char* mime_str = "application/octet-stream";
        if (attach->content_type && attach->content_type->c_str) {
            mime_str = attach->content_type->c_str(attach->content_type);
        }

        char raw_pid[32];
        int rp_n = snprintf(raw_pid, sizeof(raw_pid), "%d", post_id);
        char raw_mid[32];
        int rm_n = snprintf(raw_mid, sizeof(raw_mid), "1");

        if (sz_n < 0 || (size_t)sz_n >= sizeof(sz_buf) || rp_n < 0 || (size_t)rp_n >= sizeof(raw_pid) || rm_n < 0 || (size_t)rm_n >= sizeof(raw_mid)) {
            self->db->rollback(self->db);
            if (file_saved) self->file_delete(self, new_path);
            res->setStatus(res, 500);
            res->sendText(res, "Buffer error.");
            return;
        }

        String* sa_pid = new_String(raw_pid);
        String* sa_mid = new_String(raw_mid);
        String* sa_org = new_String(original_filename);
        String* sa_path = new_String(new_path);
        String* sa_size = new_String(sz_buf);
        String* sa_mime = new_String(mime_str);

        HashMap* att_data = new_HashMap(8);
        att_data->put(att_data, "post_id", (Object*)sa_pid);
        att_data->put(att_data, "member_id", (Object*)sa_mid);
        att_data->put(att_data, "file_name", (Object*)sa_org);
        att_data->put(att_data, "saved_name", (Object*)sa_path);
        att_data->put(att_data, "file_size", (Object*)sa_size);
        att_data->put(att_data, "mime_type", (Object*)sa_mime);

        if (has_old_row) {
            char upd_cond[64];
            int uc_n = snprintf(upd_cond, sizeof(upd_cond), "post_id=%d AND is_deleted=0", post_id);
            if (uc_n < 0 || (size_t)uc_n >= sizeof(upd_cond)) {
                attach_ok = 0;
            } else {
                attach_ok = self->db->updateTable(self->db, "board_attachments", att_data, upd_cond);
            }
        } else {
            attach_ok = self->db->insertTable(self->db, "board_attachments", att_data);
        }

        RELEASE((Object*)sa_pid);
        RELEASE((Object*)sa_mid);
        RELEASE((Object*)sa_org);
        RELEASE((Object*)sa_path);
        RELEASE((Object*)sa_size);
        RELEASE((Object*)sa_mime);
        RELEASE((Object*)att_data);
    }

    if (!update_ok || (file_saved && !attach_ok)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "DB query failed.");
        return;
    }

    if (!self->db->commit(self->db)) {
        self->db->rollback(self->db);
        if (file_saved) self->file_delete(self, new_path);
        res->setStatus(res, 500);
        res->sendText(res, "DB commit failed.");
        return;
    }

    if (file_saved && old_path[0] != '\0' && strcmp(old_path, new_path) != 0) {
        if (!self->file_delete(self, old_path) && logger) {
            LOG_WARN(logger, "Old attachment physical delete failed: post_id=%d path=%s", post_id, old_path);
        }
    }

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("ok");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    JSONNode* j_pid = (JSONNode*)new_json_number((double)post_id);
    resp->put(resp, "post_id", (Object*)j_pid);
    RELEASE((Object*)j_pid);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_remove(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int post_id = 0;
    if (!parse_positive_int(id_str, &post_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid ID.");
        return;
    }

    if (!self->db->beginTransaction(self->db)) {
        res->setStatus(res, 500);
        res->sendText(res, "DB TX failed.");
        return;
    }

    char sql_check[128];
    int sc_n = snprintf(sql_check, sizeof(sql_check), "SELECT id FROM board_posts WHERE id = %d AND is_deleted = 0", post_id);
    if (sc_n < 0 || (size_t)sc_n >= sizeof(sql_check)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* chk_list = self->db->getRecordsFromQuery(self->db, sql_check);
    if (!chk_list) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Check query failed.");
        return;
    }
    if (chk_list->getSize(chk_list) == 0) {
        RELEASE((Object*)chk_list);
        self->db->rollback(self->db);
        res->setStatus(res, 404);
        res->sendText(res, "Not found.");
        return;
    }
    RELEASE((Object*)chk_list);

    char sql_att[256];
    int sa_n = snprintf(sql_att, sizeof(sql_att), "SELECT saved_name FROM board_attachments WHERE post_id = %d", post_id);
    if (sa_n < 0 || (size_t)sa_n >= sizeof(sql_att)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* paths = self->db->getRecordsFromQuery(self->db, sql_att);
    if (!paths) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Att query failed.");
        return;
    }

    char sql_del[256];
    const char* now_expr = (strcmp(self->db->db_type, "SQLITE") == 0)
        ? "datetime('now', 'localtime')" : "NOW()";

    int sd_n = snprintf(sql_del, sizeof(sql_del),
        "UPDATE board_posts SET is_deleted = 1, deleted_at = %s WHERE id = %d",
        now_expr, post_id);

    if (sd_n < 0 || (size_t)sd_n >= sizeof(sql_del)) {
        self->db->rollback(self->db);
        RELEASE((Object*)paths);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    int post_ok = self->db->sqlQuery(self->db, sql_del);

    sd_n = snprintf(sql_del, sizeof(sql_del), "UPDATE board_attachments SET is_deleted = 1 WHERE post_id = %d", post_id);
    if (sd_n < 0 || (size_t)sd_n >= sizeof(sql_del)) {
        self->db->rollback(self->db);
        RELEASE((Object*)paths);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    int att_ok = self->db->sqlQuery(self->db, sql_del);

    if (!post_ok || !att_ok) {
        self->db->rollback(self->db);
        RELEASE((Object*)paths);
        res->setStatus(res, 500);
        res->sendText(res, "Delete failed.");
        return;
    }

    if (!self->db->commit(self->db)) {
        self->db->rollback(self->db);
        RELEASE((Object*)paths);
        res->setStatus(res, 500);
        res->sendText(res, "Commit failed.");
        return;
    }

    for (int i = 0; i < paths->getSize(paths); i++) {
        HashMap* row = (HashMap*)paths->get(paths, i);
        String* sv = (String*)row->get(row, "saved_name");
        if (sv && !self->file_delete(self, sv->value)) {
            if (logger) {
                LOG_WARN(logger, "Post attachment physical delete failed: post_id=%d path=%s", post_id, sv->value);
            }
        }
    }
    RELEASE((Object*)paths);

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    JSONNode* j_del_id = (JSONNode*)new_json_number((double)post_id);
    resp->put(resp, "deleted_id", (Object*)j_del_id);
    RELEASE((Object*)j_del_id);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_attach(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int attach_id = 0;
    if (!parse_positive_int(id_str, &attach_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid attach ID.");
        return;
    }

    char sql[256];
    int sn = snprintf(sql, sizeof(sql), "SELECT file_name, saved_name, mime_type, file_size FROM board_attachments WHERE id = %d AND is_deleted = 0", attach_id);
    if (sn < 0 || (size_t)sn >= sizeof(sql)) {
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* att_list = self->db->getRecordsFromQuery(self->db, sql);
    if (!att_list) {
        res->setStatus(res, 500);
        res->sendText(res, "File metadata query failed.");
        return;
    }
    if (att_list->getSize(att_list) == 0) {
        RELEASE((Object*)att_list);
        res->setStatus(res, 404);
        res->sendText(res, "File not found.");
        return;
    }

    HashMap* att = (HashMap*)att_list->get(att_list, 0);
    String* s_fname = (String*)att->get(att, "file_name");
    String* s_saved = (String*)att->get(att, "saved_name");
    String* s_mime  = (String*)att->get(att, "mime_type");

    if (!s_saved || !s_fname) {
        RELEASE((Object*)att_list);
        res->setStatus(res, 404);
        res->sendText(res, "File metadata corrupted.");
        return;
    }

    const char* mime = (s_mime && s_mime->value[0] != '\0') ? s_mime->value : "application/octet-stream";

    char safe_mime[128] = {0};
    const char* m_src = mime;
    size_t mp = 0;
    while (*m_src && mp < sizeof(safe_mime) - 1) {
        if (*m_src != '\r' && *m_src != '\n') {
            safe_mime[mp++] = *m_src;
        }
        m_src++;
    }
    safe_mime[mp] = '\0';

    /* 🚨 MIME 빈 값 폴백 처리 (안전장치 완성) */
    if (safe_mime[0] == '\0') {
        snprintf(safe_mime, sizeof(safe_mime), "application/octet-stream");
    }

    char safe_fname[256] = {0};
    const char* src = s_fname->value;
    size_t p = 0;
    while (*src && p < sizeof(safe_fname) - 1) {
        if (*src != '\r' && *src != '\n' && *src != '"') {
            safe_fname[p++] = *src;
        }
        src++;
    }
    safe_fname[p] = '\0';

    char disp[512];
    int dn = snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", safe_fname);
    if (dn < 0 || (size_t)dn >= sizeof(disp)) {
        RELEASE((Object*)att_list);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    char abs_path[1024];
    int n = snprintf(abs_path, sizeof(abs_path), "%s/%s", self->base_dir, s_saved->value);
    if (n < 0 || (size_t)n >= sizeof(abs_path)) {
        RELEASE((Object*)att_list);
        res->setStatus(res, 500);
        res->sendText(res, "Internal Path Error.");
        return;
    }

    char canonical[MAX_PATH_LEN + 1];
    if (!self->pv->validate(self->pv, abs_path, canonical, sizeof(canonical))) {
        RELEASE((Object*)att_list);
        res->setStatus(res, 403);
        res->sendText(res, "Invalid attachment path.");
        return;
    }

    struct stat fst;
    if (stat(canonical, &fst) != 0 || !S_ISREG(fst.st_mode)) {
        RELEASE((Object*)att_list);
        res->setStatus(res, 404);
        res->sendText(res, "File not found on disk.");
        return;
    }

    res->setHeader(res, "Content-Type", safe_mime);
    res->setHeader(res, "Content-Disposition", disp);

    res->setStatus(res, 200);
    res->sendFile(res, canonical);

    RELEASE((Object*)att_list);
}

static void BoardHandler_attach_remove(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (req->method != HTTP_DELETE) {
        res->sendStatus(res, 405);
        return;
    }

    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int attach_id = 0;
    if (!parse_positive_int(id_str, &attach_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid attach ID.");
        return;
    }

    if (!self->db->beginTransaction(self->db)) {
        res->setStatus(res, 500);
        res->sendText(res, "DB TX failed.");
        return;
    }

    char sql[256];
    int sn = snprintf(sql, sizeof(sql),
        "SELECT saved_name "
        "FROM board_attachments "
        "WHERE id = %d AND is_deleted = 0",
        attach_id);

    if (sn < 0 || (size_t)sn >= sizeof(sql)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* att_list = self->db->getRecordsFromQuery(self->db, sql);
    if (!att_list) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Attachment query failed.");
        return;
    }
    if (att_list->getSize(att_list) == 0) {
        RELEASE((Object*)att_list);
        self->db->rollback(self->db);
        res->setStatus(res, 404);
        res->sendText(res, "Attachment not found.");
        return;
    }

    HashMap* att = (HashMap*)att_list->get(att_list, 0);
    String* saved = (String*)att->get(att, "saved_name");
    char saved_path[512] = {0};

    if (!saved || saved->value[0] == '\0') {
        RELEASE((Object*)att_list);
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Invalid attachment metadata.");
        return;
    }

    int n = snprintf(saved_path, sizeof(saved_path), "%s", saved->value);
    RELEASE((Object*)att_list);

    if (n < 0 || (size_t)n >= sizeof(saved_path)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Attachment path too long.");
        return;
    }

    sn = snprintf(sql, sizeof(sql),
        "UPDATE board_attachments "
        "SET is_deleted = 1 "
        "WHERE id = %d AND is_deleted = 0",
        attach_id);

    if (sn < 0 || (size_t)sn >= sizeof(sql)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    if (!self->db->sqlQuery(self->db, sql)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Attachment delete failed.");
        return;
    }

    if (!self->db->commit(self->db)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Commit failed.");
        return;
    }

    if (!self->file_delete(self, saved_path)) {
        if (logger) {
            LOG_WARN(logger, "Attachment physical delete failed: id=%d", attach_id);
        }
    }

    res->setStatus(res, 204);
    res->sendText(res, "");
}

static void BoardHandler_comment_write(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int post_id = 0;
    if (!parse_positive_int(id_str, &post_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid post ID.");
        return;
    }

    const char* content = NULL;
    int parent_id = 0;
    if (req->json && req->json->isObject(req->json)) {
        content = req->json->getString(req->json, "content");
        parent_id = req->json->getInt(req->json, "parent_id");
        if (parent_id < 0) {
            res->setStatus(res, 400);
            res->sendText(res, "Invalid parent ID.");
            return;
        }
    } else if (req->form) {
        content = hashmap_get_str(req->form, "content");
        const char* p_str = hashmap_get_str(req->form, "parent_id");
        if (p_str && p_str[0] != '\0') {
            if (!parse_positive_int(p_str, &parent_id)) {
                res->setStatus(res, 400);
                res->sendText(res, "Invalid parent ID.");
                return;
            }
        }
    }

    if (!content || content[0] == '\0') {
        res->setStatus(res, 400);
        res->sendText(res, "No content.");
        return;
    }

    if (!self->db->beginTransaction(self->db)) {
        res->setStatus(res, 500);
        res->sendText(res, "DB TX failed.");
        return;
    }

    char sql_pchk[128];
    int sp_n = snprintf(sql_pchk, sizeof(sql_pchk), "SELECT id FROM board_posts WHERE id = %d AND is_deleted = 0", post_id);
    if (sp_n < 0 || (size_t)sp_n >= sizeof(sql_pchk)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* pchk_list = self->db->getRecordsFromQuery(self->db, sql_pchk);
    if (!pchk_list) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Post query failed.");
        return;
    }
    if (pchk_list->getSize(pchk_list) == 0) {
        RELEASE((Object*)pchk_list);
        self->db->rollback(self->db);
        res->setStatus(res, 404);
        res->sendText(res, "Post not found.");
        return;
    }
    RELEASE((Object*)pchk_list);

    int depth = 0;
    if (parent_id > 0) {
        char sql_chk[256];
        int sc_n = snprintf(sql_chk, sizeof(sql_chk),
            "SELECT depth FROM board_comments "
            "WHERE id = %d AND post_id = %d AND is_deleted = 0",
            parent_id, post_id);

        if (sc_n < 0 || (size_t)sc_n >= sizeof(sql_chk)) {
            self->db->rollback(self->db);
            res->setStatus(res, 500);
            res->sendText(res, "Buffer error.");
            return;
        }

        ArrayList* p_cmt_list = self->db->getRecordsFromQuery(self->db, sql_chk);
        if (!p_cmt_list) {
            self->db->rollback(self->db);
            res->setStatus(res, 500);
            res->sendText(res, "Parent comment query failed.");
            return;
        }
        if (p_cmt_list->getSize(p_cmt_list) == 0) {
            RELEASE((Object*)p_cmt_list);
            self->db->rollback(self->db);
            res->setStatus(res, 404);
            res->sendText(res, "Parent comment missing or invalid.");
            return;
        }

        HashMap* p_cmt = (HashMap*)p_cmt_list->get(p_cmt_list, 0);
        int parent_depth = 0;
        String* d_str = (String*)p_cmt->get(p_cmt, "depth");

        if (!d_str || !parse_nonnegative_int(d_str->value, &parent_depth)) {
            RELEASE((Object*)p_cmt_list);
            self->db->rollback(self->db);
            res->setStatus(res, 500);
            res->sendText(res, "Invalid parent comment metadata.");
            return;
        }

        if (parent_depth != 0) {
            RELEASE((Object*)p_cmt_list);
            self->db->rollback(self->db);
            res->setStatus(res, 400);
            res->sendText(res, "Reply depth limit exceeded.");
            return;
        }
        depth = 1;
        RELEASE((Object*)p_cmt_list);
    }

    long member_id = 1;
    char pid_buf[32]; char mid_buf[32]; char depth_buf[32]; char parent_buf[32];

    int n1 = snprintf(pid_buf, sizeof(pid_buf), "%d", post_id);
    int n2 = snprintf(mid_buf, sizeof(mid_buf), "%ld", member_id);
    int n3 = snprintf(depth_buf, sizeof(depth_buf), "%d", depth);
    int n4 = 0;
    if (parent_id > 0) n4 = snprintf(parent_buf, sizeof(parent_buf), "%d", parent_id);

    if (n1 < 0 || (size_t)n1 >= sizeof(pid_buf) || n2 < 0 || (size_t)n2 >= sizeof(mid_buf) || n3 < 0 || (size_t)n3 >= sizeof(depth_buf) || (parent_id > 0 && (n4 < 0 || (size_t)n4 >= sizeof(parent_buf)))) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    String* s_pid = new_String(pid_buf);
    String* s_mid = new_String(mid_buf);
    String* s_content = new_String(content);
    String* s_depth = new_String(depth_buf);
    String* s_parent = parent_id > 0 ? new_String(parent_buf) : NULL;

    HashMap* cmt_data = new_HashMap(8);
    cmt_data->put(cmt_data, "post_id", (Object*)s_pid);
    cmt_data->put(cmt_data, "member_id", (Object*)s_mid);
    cmt_data->put(cmt_data, "content", (Object*)s_content);
    cmt_data->put(cmt_data, "depth", (Object*)s_depth);
    if (s_parent) cmt_data->put(cmt_data, "parent_id", (Object*)s_parent);

    RELEASE((Object*)s_pid); RELEASE((Object*)s_mid); RELEASE((Object*)s_content); RELEASE((Object*)s_depth);
    if (s_parent) RELEASE((Object*)s_parent);

    int ok = self->db->insertTable(self->db, "board_comments", cmt_data);
    long long new_cmt_id = self->db->last_insert_id;
    RELEASE((Object*)cmt_data);

    if (!ok || new_cmt_id <= 0 || new_cmt_id > INT_MAX) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Invalid inserted comment ID.");
        return;
    }

    if (!self->db->commit(self->db)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Commit failed.");
        return;
    }

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    JSONNode* j_cid = (JSONNode*)new_json_number((double)new_cmt_id);
    resp->put(resp, "comment_id", (Object*)j_cid);
    RELEASE((Object*)j_cid);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 201);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_comment_modify(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* cid_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int comment_id = 0;
    if (!parse_positive_int(cid_str, &comment_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid comment ID.");
        return;
    }

    if (!self->db->beginTransaction(self->db)) {
        res->setStatus(res, 500);
        res->sendText(res, "DB TX failed.");
        return;
    }

    char sql_cchk[128];
    int sc_n = snprintf(sql_cchk, sizeof(sql_cchk), "SELECT id FROM board_comments WHERE id = %d AND is_deleted = 0", comment_id);
    if (sc_n < 0 || (size_t)sc_n >= sizeof(sql_cchk)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* cchk_list = self->db->getRecordsFromQuery(self->db, sql_cchk);
    if (!cchk_list) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Comment check query failed.");
        return;
    }
    if (cchk_list->getSize(cchk_list) == 0) {
        RELEASE((Object*)cchk_list);
        self->db->rollback(self->db);
        res->setStatus(res, 404);
        res->sendText(res, "Comment not found.");
        return;
    }
    RELEASE((Object*)cchk_list);

    const char* content = NULL;
    if (req->json && req->json->isObject(req->json)) {
        content = req->json->getString(req->json, "content");
    } else if (req->form) {
        content = hashmap_get_str(req->form, "content");
    }
    if (!content || content[0] == '\0') {
        self->db->rollback(self->db);
        res->setStatus(res, 400);
        res->sendText(res, "No content.");
        return;
    }

    char cid_buf[64];
    int cb_n = snprintf(cid_buf, sizeof(cid_buf), "id=%d AND is_deleted=0", comment_id);
    if (cb_n < 0 || (size_t)cb_n >= sizeof(cid_buf)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    String* s_content = new_String(content);
    HashMap* upd_data = new_HashMap(4);
    upd_data->put(upd_data, "content", (Object*)s_content);
    RELEASE((Object*)s_content);
    int ok = self->db->updateTable(self->db, "board_comments", upd_data, cid_buf);
    RELEASE((Object*)upd_data);

    if (!ok || !self->db->commit(self->db)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Update failed.");
        return;
    }

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    JSONNode* j_cid = (JSONNode*)new_json_number((double)comment_id);
    resp->put(resp, "comment_id", (Object*)j_cid);
    RELEASE((Object*)j_cid);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_comment_remove(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (!self->db || self->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "DB unavailable.");
        return;
    }

    const char* cid_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int comment_id = 0;
    if (!parse_positive_int(cid_str, &comment_id)) {
        res->setStatus(res, 400);
        res->sendText(res, "Invalid comment ID.");
        return;
    }

    if (!self->db->beginTransaction(self->db)) {
        res->setStatus(res, 500);
        res->sendText(res, "DB TX failed.");
        return;
    }

    char sql_chk[128];
    int sc_n = snprintf(sql_chk, sizeof(sql_chk), "SELECT depth FROM board_comments WHERE id = %d AND is_deleted = 0", comment_id);
    if (sc_n < 0 || (size_t)sc_n >= sizeof(sql_chk)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    ArrayList* cmt_list = self->db->getRecordsFromQuery(self->db, sql_chk);
    if (!cmt_list) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Comment query failed.");
        return;
    }
    if (cmt_list->getSize(cmt_list) == 0) {
        RELEASE((Object*)cmt_list);
        self->db->rollback(self->db);
        res->setStatus(res, 404);
        res->sendText(res, "Not found.");
        return;
    }

    HashMap* cmt = (HashMap*)cmt_list->get(cmt_list, 0);
    int depth = 0;
    String* d = (String*)cmt->get(cmt, "depth");

    if (!d || !parse_nonnegative_int(d->value, &depth)) {
        RELEASE((Object*)cmt_list);
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Invalid comment metadata.");
        return;
    }
    RELEASE((Object*)cmt_list);

    char sql_del[128];
    int sd_n = 0;
    if (depth == 0) {
        sd_n = snprintf(sql_del, sizeof(sql_del), "UPDATE board_comments SET is_deleted = 1 WHERE id = %d OR parent_id = %d", comment_id, comment_id);
    } else {
        sd_n = snprintf(sql_del, sizeof(sql_del), "UPDATE board_comments SET is_deleted = 1 WHERE id = %d", comment_id);
    }

    if (sd_n < 0 || (size_t)sd_n >= sizeof(sql_del)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Buffer error.");
        return;
    }

    int del_ok = self->db->sqlQuery(self->db, sql_del);
    if (!del_ok) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Delete failed.");
        return;
    }
    if (!self->db->commit(self->db)) {
        self->db->rollback(self->db);
        res->setStatus(res, 500);
        res->sendText(res, "Commit failed.");
        return;
    }
    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    JSONNode* j_del_cid = (JSONNode*)new_json_number((double)comment_id);
    resp->put(resp, "deleted_comment_id", (Object*)j_del_cid);
    RELEASE((Object*)j_del_cid);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_skin_update(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (req->method != HTTP_POST) {
        res->sendStatus(res, 405);
        return;
    }
    const char* new_skin = NULL;
    if (req->json && req->json->isObject(req->json)) {
        new_skin = req->json->getString(req->json, "skin");
    } else if (req->form) {
        new_skin = hashmap_get_str(req->form, "skin");
    }

    if (!new_skin || (strcmp(new_skin, "white") != 0 && strcmp(new_skin, "dark") != 0)) {
        res->setStatus(res, 400);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Invalid skin name.\"}");
        return;
    }

    char check_path[1024];
    int n = snprintf(check_path, sizeof(check_path), "%s/%s/skin/%s", self->base_dir, self->tpl_dir, new_skin);
    if (n < 0 || (size_t)n >= sizeof(check_path)) {
        res->setStatus(res, 500);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Skin path too long.\"}");
        return;
    }
    struct stat st;
    if (stat(check_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        res->setStatus(res, 400);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Skin directory not found.\"}");
        return;
    }

    if (!board_config_upsert(self->db, "skin", new_skin)) {
        res->setStatus(res, 500);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"DB update failed.\"}");
        return;
    }

    if (!BoardHandler_refresh_skin(self)) {
        res->setStatus(res, 500);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Skin config failed.\"}");
        return;
    }

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);
    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_limit_update(BoardHandler* self, HttpRequest* req, HttpResponse* res) {
    if (req->method != HTTP_POST) {
        res->sendStatus(res, 405);
        return;
    }

    const char* new_limit = NULL;
    if (req->json && req->json->isObject(req->json)) {
        new_limit = req->json->getString(req->json, "limit");
    } else if (req->form) {
        new_limit = hashmap_get_str(req->form, "limit");
    }

    if (!new_limit || new_limit[0] == '\0') {
        res->setStatus(res, 400);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Limit is missing.\"}");
        return;
    }

    int req_limit = 0;
    if (!parse_positive_int(new_limit, &req_limit)) {
        res->setStatus(res, 400);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Invalid limit value format.\"}");
        return;
    }

    int safe_limit = 20;
    switch(req_limit) {
        case 5: case 10: case 15: case 20: case 30: case 40: case 50:
            safe_limit = req_limit;
            break;
        default:
            res->setStatus(res, 400);
            res->sendText(res, "{\"status\":\"error\",\"message\":\"Invalid limit value.\"}");
            return;
    }

    char limit_str[32];
    int ls_n = snprintf(limit_str, sizeof(limit_str), "%d", safe_limit);
    if (ls_n < 0 || (size_t)ls_n >= sizeof(limit_str)) {
        res->setStatus(res, 500);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"Buffer overflow.\"}");
        return;
    }

    if (!board_config_upsert(self->db, "board_list", limit_str)) {
        res->setStatus(res, 500);
        res->sendText(res, "{\"status\":\"error\",\"message\":\"DB limit update failed.\"}");
        return;
    }

    JSONNode* resp = new_JSON_Object();
    JSONNode* j_status = new_JSON_String("success");
    resp->put(resp, "status", (Object*)j_status);
    RELEASE((Object*)j_status);

    char* json_out = resp->toString(resp);
    res->setStatus(res, 200);
    res->setHeader(res, "Content-Type", "application/json; charset=utf-8");
    res->sendText(res, json_out);
    free(json_out);
    RELEASE((Object*)resp);
}

static void BoardHandler_finalize(Object* obj) {
    (void)obj;
}

static const Class BoardHandler_Class = {
    .name = "BoardHandler",
    .size = sizeof(BoardHandler),
    .finalize = BoardHandler_finalize
};

BoardHandler* new_BoardHandler(DBClient* db, PathValidator* pv, const char* base_dir, const char* upload_dir, const char* tpl_dir, size_t max_upload_size) {
    if (!db || !pv || !base_dir || !upload_dir || !tpl_dir) return NULL;
    BoardHandler* self = (BoardHandler*)calloc(1, sizeof(BoardHandler));
    if (!self) return NULL;

    Object_Init((Object*)self, &BoardHandler_Class);
    self->db = db;
    self->pv = pv;

    self->max_upload_size = max_upload_size > 0 ? max_upload_size : 10485760;

    int n;
    n = snprintf(self->base_dir, sizeof(self->base_dir), "%s", base_dir);
    if (n < 0 || (size_t)n >= sizeof(self->base_dir)) {
        free(self);
        return NULL;
    }
    n = snprintf(self->upload_dir, sizeof(self->upload_dir), "%s", upload_dir);
    if (n < 0 || (size_t)n >= sizeof(self->upload_dir)) {
        free(self);
        return NULL;
    }
    n = snprintf(self->tpl_dir, sizeof(self->tpl_dir), "%s", tpl_dir);
    if (n < 0 || (size_t)n >= sizeof(self->tpl_dir)) {
        free(self);
        return NULL;
    }
    n = snprintf(self->skin, sizeof(self->skin), "white");
    if (n < 0 || (size_t)n >= sizeof(self->skin)) {
        free(self);
        return NULL;
    }

    if (!BoardHandler_refresh_skin(self)) {
        printf("[ERROR] BoardHandler_refresh_skin failed. Base skin directory missing?\n");
        free(self);
        return NULL;
    }

    self->read_write = BoardHandler_read_write;
    self->write = BoardHandler_write;
    self->list = BoardHandler_list;
    self->detail = BoardHandler_detail;
    self->modify = BoardHandler_modify;
    self->remove = BoardHandler_remove;
    self->attach = BoardHandler_attach;
    self->attach_remove = BoardHandler_attach_remove;
    self->comment_write = BoardHandler_comment_write;
    self->comment_modify = BoardHandler_comment_modify;
    self->comment_remove = BoardHandler_comment_remove;
    self->file_size_exceeded = BoardHandler_file_size_exceeded;
    self->file_save = BoardHandler_file_save;
    self->file_delete = BoardHandler_file_delete;
    self->file_sanitize = BoardHandler_file_sanitize;
    self->skin_update = BoardHandler_skin_update;
    self->limit_update = BoardHandler_limit_update;

    return self;
}

void board_read_write_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->read_write((BoardHandler*)ctx, req, res);
}
void board_write_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->write((BoardHandler*)ctx, req, res);
}
void board_list_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->list((BoardHandler*)ctx, req, res);
}
void board_detail_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->detail((BoardHandler*)ctx, req, res);
}
void board_modify_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->modify((BoardHandler*)ctx, req, res);
}
void board_remove_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->remove((BoardHandler*)ctx, req, res);
}
void board_attach_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->attach((BoardHandler*)ctx, req, res);
}
void board_attach_remove_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->attach_remove((BoardHandler*)ctx, req, res);
}
void board_comment_write_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->comment_write((BoardHandler*)ctx, req, res);
}
void board_comment_modify_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->comment_modify((BoardHandler*)ctx, req, res);
}
void board_comment_remove_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->comment_remove((BoardHandler*)ctx, req, res);
}
void board_skin_update_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->skin_update((BoardHandler*)ctx, req, res);
}
void board_limit_update_cb(HttpRequest* req, HttpResponse* res, void* ctx) {
    ((BoardHandler*)ctx)->limit_update((BoardHandler*)ctx, req, res);
}