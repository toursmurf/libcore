/*
 * arc_board_handler_test.c
 * BoardHandler 회귀 테스트 — 400 Bad Request & untitled.bin 방어 검증 (v1.7.1)
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


/* =========================================================
 * 🛠️ 1. Mock DBClient (트랜잭션 껍데기)
 * ========================================================= */
typedef struct {
    DBClient base;
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
static int mock_commit(DBClient* self) { (void)self; return 1; }

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
    MockSocket* spy_sock = (MockSocket*)mock_sock;

    const char* test_dir = "/tmp/toostalk_test_uploads";
    BoardHandler* bh = new_BoardHandler(db, pv, test_dir);
    assert(bh != NULL);

    /* ── [테스트 1] file_sanitize 정책 검증 (PathValidator 거부 시 기본값 반환) ── */
    char clean_name[256] = {0};
    bh->file_sanitize(bh, "../../../etc/passwd", clean_name, sizeof(clean_name));
    assert(strcmp(clean_name, "untitled.bin") == 0); /* 🚨 PathValidator가 .. 차단 시 반환 */
    printf("✅ [Test 1] file_sanitize (PathValidator 공격 차단 및 untitled.bin 반환) 검증 완료!\n");

    /* ── [테스트 2] file_delete Containment 경계 방어 ── */
    assert(bh->file_delete(bh, "/etc/passwd") == false);
    assert(bh->file_delete(bh, "/tmp/toostalk_test_uploads2/file.txt") == false);
    printf("✅ [Test 2] file_delete (Containment 경계 이탈) 원천 차단 완료!\n");

    /* ── [테스트 3] Router params 연동 검증 (detail API) ── */
    HttpRequest* req_detail = new_HttpRequest();
    req_detail->method = HTTP_GET;

    String* s_id = new_String("123");
    req_detail->params->put(req_detail->params, "id", (Object*)s_id);
    RELEASE((Object*)s_id);

    HttpResponse* res_detail = new_HttpResponse(mock_sock, NULL);
    bh->detail(bh, req_detail, res_detail);

    assert(res_detail->status_code == 200);
    assert(strstr(spy_sock->captured, "\"post_id\":123") != NULL);
    printf("✅ [Test 3] detail API (req->params 연동 및 응답 캡처) 검증 완료!\n");

    /* ── [테스트 4] Multipart 누락 시 조기 차단 (write API 400 검증) ── */
    reset_mock_socket(mock_sock);

    HttpRequest* req_write = new_HttpRequest();
    req_write->method = HTTP_POST;
    req_write->multipart = NULL; /* 의도적으로 multipart 파싱 결과를 누락 */

    HttpResponse* res_write = new_HttpResponse(mock_sock, NULL);

    bh->write(bh, req_write, res_write);

    assert(res_write->status_code == 400);
    assert(strstr(spy_sock->captured, "Bad Request: Multipart payload expected") != NULL);
    printf("✅ [Test 4] write API (multipart 누락 시 400 Bad Request 즉각 차단) 검증 완료!\n");

    /* TODO: DB Commit 실패 시 Rollback 500 에러 검증은
     * 추후 Raw Multipart Body 스트링을 조립하여 Multipart_parse()를 호출하는 통합 테스트로 보강 예정 */

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