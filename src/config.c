/*
 * board_write_handler.c
 * 게시판 파일 업로드 핸들러
 * BoardContext 직접 주입 방식 (Config + DB 직접 연결)
 */

#include "http_server.h"
#include "multipart_parser.h"
#include "path_validator.h"
#include "json.h"
#include "logger.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdatomic.h>

extern Logger* logger;

/* =========================================================
 * BoardContext — 핸들러에 직접 주입되는 의존성 묶음
 * ========================================================= */
typedef struct {
    DBClient*      db;          /* [BORROWED] main에서 소유 */
    PathValidator* pv;          /* [BORROWED] main에서 소유 (싱글턴) */
    const char*    upload_dir;  /* [BORROWED] config에서 빌려온 문자열 */
} BoardContext;

/* =========================================================
 * 디렉토리 재귀 생성 (0700 보안 권한)
 * ========================================================= */
static int mkdir_p(const char* path, mode_t mode) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

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

/* =========================================================
 * PathValidator 기반 파일명 세탁기
 * ========================================================= */
static void sanitize_filename(PathValidator* pv,
                               const char*    dirty,
                               char*          clean_out,
                               size_t         max_len) {
    if (max_len == 0 || !dirty || !pv) {
        return;
    }

    /* basename 추출 */
    const char* base = dirty;
    const char* p    = dirty;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
        p++;
    }

    /* PathValidator 검증 */
    char test_path[MAX_PATH_LEN + 2];
    int  n = snprintf(test_path, sizeof(test_path), "/%s", base);
    if (n < 0 || (size_t)n >= sizeof(test_path)) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }

    char canonical[MAX_PATH_LEN + 1];
    if (!pv->validate(pv, test_path, canonical, sizeof(canonical))) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }

    /* canonical에서 파일명 추출 */
    char* last_slash  = strrchr(canonical, '/');
    const char* final = last_slash ? last_slash + 1 : canonical;

    if (*final == '\0' ||
        strcmp(final, ".")  == 0 ||
        strcmp(final, "..") == 0) {
        strncpy(clean_out, "untitled.bin", max_len - 1);
        clean_out[max_len - 1] = '\0';
        return;
    }

    strncpy(clean_out, final, max_len - 1);
    clean_out[max_len - 1] = '\0';
}

/* =========================================================
 * POST /board/write 핸들러
 * ========================================================= */
void board_write_handler(HttpRequest*  req,
                          HttpResponse* res,
                          void*         user_ctx) {
    BoardContext* ctx = (BoardContext*)user_ctx;

    /* ── 방어막 ── */
    if (req->method != HTTP_POST) {
        res->sendStatus(res, 405);
        return;
    }
    if (!req->multipart) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Multipart payload expected.");
        return;
    }
    if (!ctx || !ctx->db || ctx->db->isConnected == 0) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB unavailable.");
        return;
    }
    if (!ctx->pv) {
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: PathValidator unavailable.");
        return;
    }

    /* ── 텍스트 필드 추출 ── */
    const char* title   = MultipartResult_get_field(req->multipart, "title");
    const char* content = MultipartResult_get_field(req->multipart, "content");
    long        member_id = 1; /* TODO: 세션 연동 */

    if (!title || !content) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Missing 'title' or 'content'.");
        return;
    }
    if (strlen(content) > 65535) {
        res->setStatus(res, 400);
        res->sendText(res, "Bad Request: Content too long (max 65535 bytes).");
        return;
    }

    /* ── 파일 추출 ── */
    HttpMultipartFile* attach = MultipartResult_get_file(req->multipart, "attach");
    char storage_path[512]    = {0};
    bool file_saved           = false;

    const char* original_filename = "untitled.bin";
    if (attach && attach->filename && attach->filename->c_str) {
        original_filename = attach->filename->c_str(attach->filename);
    }

    /* ── 1. 디스크 저장 ── */
    if (attach && attach->data && attach->size > 0) {
        /* 파일명 세탁 */
        char safe_name[256] = {0};
        sanitize_filename(ctx->pv, original_filename,
                          safe_name, sizeof(safe_name));

        /* 충돌 방지: tv_usec + atomic 카운터 */
        struct timeval          tv;
        gettimeofday(&tv, NULL);
        static _Atomic unsigned int seq = 0;
        unsigned int s = atomic_fetch_add(&seq, 1);

        const char* udir = ctx->upload_dir
                         ? ctx->upload_dir
                         : "/var/toostalk/uploads";

        snprintf(storage_path, sizeof(storage_path),
                 "%s/%ld_%06ld_%04u_%s",
                 udir,
                 (long)tv.tv_sec,
                 (long)tv.tv_usec,
                 s % 10000,
                 safe_name);

        /* 디렉토리 생성 */
        if (mkdir_p(udir, 0700) == -1) {
            if (logger) {
                LOG_ERROR(logger,
                    "mkdir_p failed: %s (errno: %d)", udir, errno);
            }
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: Storage init failed.");
            return;
        }

        /* 파일 쓰기 */
        FILE* fp = fopen(storage_path, "wb");
        if (!fp) {
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: Cannot open file.");
            return;
        }

        size_t written = fwrite(attach->data, 1, attach->size, fp);
        fflush(fp);
        fsync(fileno(fp));
        fclose(fp);

        if (written != attach->size) {
            remove(storage_path);
            res->setStatus(res, 500);
            res->sendText(res, "Internal Server Error: Disk write failed.");
            return;
        }

        file_saved = true;
    }

    /* ── 2. DB 트랜잭션 ── */
    ctx->db->beginTransaction(ctx->db);

    /* board_posts INSERT */
    char mid_buf[32];
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

    int       post_ok     = ctx->db->insertTable(ctx->db, "board_posts", post_data);
    long long new_post_id = ctx->db->last_insert_id;
    RELEASE((Object*)post_data);

    /* attachments INSERT */
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

        String*  sa_pid  = new_String(pid_buf);
        String*  sa_mid  = new_String(mid_buf);
        String*  sa_org  = new_String(original_filename);
        String*  sa_path = new_String(storage_path);
        String*  sa_size = new_String(sz_buf);
        String*  sa_mime = new_String(mime_str);
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

        attach_ok = ctx->db->insertTable(ctx->db, "attachments", att_data);
        RELEASE((Object*)att_data);
    }

    /* 실패 시 롤백 + 고아 파일 소각 */
    if (!post_ok || (file_saved && !attach_ok)) {
        ctx->db->rollback(ctx->db);
        if (file_saved && storage_path[0] != '\0') {
            remove(storage_path);
        }
        res->setStatus(res, 500);
        res->sendText(res, "Internal Server Error: DB transaction failed.");
        return;
    }

    ctx->db->commit(ctx->db);

    /* ── 3. 응답 ── */
    JSONNode* resp = new_JSON_Object();

    JSONNode* j_status = new_JSON_String("success");
    JSONNode* j_pid    = (JSONNode*)new_json_number((double)new_post_id);
    JSONNode* j_msg    = new_JSON_String("게시글 및 첨부파일 영속화 완료!");

    if (j_status) { resp->put(resp, "status",   (Object*)j_status); RELEASE((Object*)j_status); }
    if (j_pid)    { resp->put(resp, "post_id",  (Object*)j_pid);    RELEASE((Object*)j_pid);    }
    if (j_msg)    { resp->put(resp, "message",  (Object*)j_msg);    RELEASE((Object*)j_msg);    }

    res->sendJson(res, resp);
    RELEASE((Object*)resp);
}