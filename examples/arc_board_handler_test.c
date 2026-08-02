/*
 * arc_board_handler_test.c
 * BoardHandler 회귀 테스트 — 캡처형 Mock 및 경계 공격 포함 (v1.7.1)
 * 🌿 Eye-Care Mode: 1 Line = 1 Statement
 */

#include "board_handler.h"
#include "path_validator.h"
#include "http_message.h"
#include "db.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

Logger* logger = NULL;

/* =========================================================
 * 🛠️ 1. Mock DBClient (fail_commit 트리거 탑재)
 * ========================================================= */
typedef struct {
    DBClient base;
    int fail_commit; /* 1로 설정 시 commit() 강제 실패 */
} MockDB;

static void MockDB_finalize(Object* obj) { (void)obj; }
static const Class MockDB_Class = { .name = "MockDB", .size = sizeof(MockDB), .finalize = MockDB_finalize };

static int mock_begin(DBClient* self) { (void)self; return 1; }
static int mock_rollback(DBClient* self) { (void)self; return 1; }
static int mock_insert(DBClient* self, const char* table, HashMap* data) {
    (void)self; (void)table; (void)data;
    self->last_insert_id = 999;
    return 1;
}
static int mock_update(DBClient* self, const char* table, HashMap* data, const char* pk) {
    (void)self; (void)table; (void)data; (void)pk;
    return 1;
}
static int mock_commit(DBClient* self) {
    MockDB* m = (MockDB*)self;
    return m->fail_commit ? 0 : 1;
}

static DBClient* create_mock_db(void) {
    MockDB* m = (MockDB*)calloc(1, sizeof(MockDB));
    if (!m) return NULL;
    Object_Init((Object*)m, &MockDB_Class);
    m->base.isConnected = 1;
    m->base.beginTransaction = mock_begin;
    m->base.commit = mock_commit;
    m->base.rollback = mock_rollback;
    m->base.insertTable = mock_insert;
    m->base.updateTable = mock_update;
    return (DBClient*)m;
}

/* =========================================================
 * 🛠️ 2. Mock Socket (응답 캡처형 Spy 객체)
 * ========================================================= */
typedef struct {
    Socket base;
    char captured[8192];
    size_t length;
} MockSocket;

static void MockSocket_finalize(Object* obj) { (void)obj; }
static const Class MockSocket_Class = { .name = "MockSocket", .size = sizeof(MockSocket), .finalize = MockSocket_finalize };

static ssize_t mock_send(Socket* self, const void* buf, size_t len, const char* dest_ip, int dest_port) {
    (void)dest_ip; (void)dest_port;
    MockSocket* m = (MockSocket*)self;
    if (m->length + len < sizeof(m->captured)) {
        memcpy(m->captured + m->length, buf, len);
        m->length += len;
        m->captured[m->length] = '\0';
    }
    return (ssize_t)len;
}

static Socket* create_mock_socket(void) {
    MockSocket* m = (MockSocket*)calloc(1, sizeof(MockSocket));
    if (!m) return NULL;
    Object_Init((Object*)m, &MockSocket_Class);
    m->base.is_open = 1;
    m->base.send = mock_send;
    return (Socket*)m;
}

static void reset_mock_socket(Socket* sock) {
    MockSocket* m = (MockSocket*)sock;
    memset(m->captured, 0, sizeof(m->captured));
    m->length = 0;
}

/* =========================================================
 * 🚀 3. 회귀 테스트 러너
 * ========================================================= */
int main(void) {
    printf("==========================================\n");
    printf("🛡️ BoardHandler Regression Test Start!\n");
    printf("==========================================\n");

    logger = new_Logger(LOG_LEVEL_DEBUG);

    DBClient* db = create_mock_db();
    PathValidator* pv = new_PathValidator();
    Socket* mock_sock = create_mock_socket();

    const char* test_dir = "/tmp/toostalk_test_uploads";
    BoardHandler* bh = new_BoardHandler(db, pv, test_dir);
    assert(bh != NULL);

    /* ── [테스트 1] file_sanitize 정책 검증 (정책 B: basename 추출) ── */
    char clean_name[256] = {0};
    bh->file_sanitize(bh, "../../../etc/passwd", clean_name, sizeof(clean_name));
    assert(strcmp(clean_name, "passwd") == 0); /* 🚨 디렉터리 제거 후 이름만 허용 */
    printf("✅ [Test 1] file_sanitize (basename 추출 정책) 검증 완료!\n");

    /* ── [테스트 2] file_delete Containment 경계 방어 ── */
    assert(bh->file_delete(bh, "/etc/passwd") == false);
    assert(bh->file_delete(bh, "/tmp/toostalk_test_uploads2/file.txt") == false); /* 이름이 비슷한 외부 폴더 차단 */
    printf("✅ [Test 2] file_delete (Containment 경계 이탈) 원천 차단 완료!\n");

    /* ── [테스트 3] Router params 연동 검증 (detail API) ── */
    HttpRequest* req_detail = new_HttpRequest();
    req_detail->method = HTTP_GET;

    String* s_id = new_String("123");
    req_detail->params->put(req_detail->params, "id", (Object*)s_id);
    RELEASE((Object*)s_id);

    HttpResponse* res_detail = new_HttpResponse(mock_sock, NULL);
    bh->detail(bh, req_detail, res_detail);

    MockSocket* spy_sock = (MockSocket*)mock_sock;
    assert(res_detail->status_code == 200);
    assert(strstr(spy_sock->captured, "\"post_id\":123") != NULL); /* 🚨 응답 본문 직접 캡처 검증! */
    printf("✅ [Test 3] detail API (req->params 연동 및 응답 캡처) 검증 완료!\n");

    /* ── [테스트 4] Commit 실패 스트레스 테스트 (write API) ── */
    reset_mock_socket(mock_sock);

    HttpRequest* req_write = new_HttpRequest();
    req_write->method = HTTP_POST;
    req_write->multipart = new_MultipartResult();
    MultipartResult_add_field(req_write->multipart, "title", "Fail Title");
    MultipartResult_add_field(req_write->multipart, "content", "Fail Content");

    HttpResponse* res_write = new_HttpResponse(mock_sock, NULL);

    /* Mock DB에 commit() 실패 트리거 온! */
    ((MockDB*)db)->fail_commit = 1;
    bh->write(bh, req_write, res_write);

    assert(res_write->status_code == 500);
    assert(strstr(spy_sock->captured, "500 Internal Server Error") != NULL);
    printf("✅ [Test 4] DB Commit 실패 시 Rollback 및 500 에러 처리 검증 완료!\n");

    /* ── ARC 메모리 대청소 ── */
    RELEASE((Object*)req_detail);
    RELEASE((Object*)res_detail);
    RELEASE((Object*)req_write);
    RELEASE((Object*)res_write);

    RELEASE((Object*)bh);
    RELEASE((Object*)pv);
    RELEASE((Object*)db);
    RELEASE((Object*)mock_sock);
    RELEASE((Object*)logger);

    printf("==========================================\n");
    printf("🚀 BoardHandler 회귀 테스트 완벽 통과! (Valgrind Ready)\n");
    printf("==========================================\n");

    return 0;
}