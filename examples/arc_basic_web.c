/*
 * File: examples/arc_web_basics.c
 * libcore 웹서버 기초 예제 — GET / POST / 동적 :id / query / form / JSON
 * (강의 Vol.19 "libcore를 이용해서 내 손으로 웹서버 구축하기" 실습용)
 *
 * 실행:  ./examples/arc_web_basics            → 브라우저로 http://127.0.0.1:8080/
 *
 * 라우트
 *   GET  /                    → /signup 으로 redirect
 *   GET  /signup              → 가입 폼 (text, number, select, radio, checkbox, textarea)
 *   POST /signup              → 폼 처리 → 저장 → /members/:id 로 redirect (PRG 패턴)
 *   GET  /members             → 목록 (?dept=dev 처럼 query 필터)
 *   GET  /members/:id         → 상세 (동적 파라미터)
 *   GET  /api/members/:id     → 같은 데이터를 JSON 으로
 *
 * 핵심 포인트
 *   1. user_ctx  : 전역 변수 대신 MemberStore 를 Router 에 주입한다.
 *   2. :id       : req->params 는 항상 문자열 → 숫자 검증 후 사용.
 *   3. form 값   : 현재 req->form 은 URL 디코드를 하지 않는다 (백로그 FORM-1).
 *                  → form_get() 에서 직접 디코드. FORM-1 수정 후엔 디코드 단계만 빼면 된다.
 *   4. checkbox  : 같은 name 이 여러 번 오면 HashMap 은 마지막 값만 남긴다.
 *                  → 그룹 checkbox 는 req->body 원문에서 form_get_all() 로 모두 모은다.
 *   5. 출력      : 사용자 입력을 HTML 에 넣을 때는 반드시 html_escape (XSS 방지).
 */

#include "libcore.h"
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_MEMBERS 64

/* =========================================================
 * [1] 데이터 — 메모리 저장소 (user_ctx 로 주입)
 * ========================================================= */
typedef struct {
    int  id;
    char name[64];
    char email[96];
    int  age;
    char dept[16];
    char level[16];
    char interests[96];   /* "c, web" 처럼 쉼표로 이어 붙임 */
    char memo[256];
    bool notify;
} Member;

typedef struct {
    Member items[MAX_MEMBERS];
    int    count;
} MemberStore;

static EventLoop* g_loop = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_loop) event_loop_stop(g_loop);   /* running = 0 만 세운다 */
}

/* =========================================================
 * [2] 작은 도구들 — 출력 버퍼, HTML 이스케이프, 숫자 검증
 * ========================================================= */
typedef struct {
    char   data[16384];
    size_t len;
    bool   overflow;
} Page;

static void page_printf(Page* p, const char* fmt, ...) {
    if (p->overflow) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(p->data + p->len, sizeof(p->data) - p->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(p->data) - p->len) {
        p->overflow = true;
        return;
    }
    p->len += (size_t)n;
}

/* 사용자 입력을 HTML 에 넣기 전에 반드시 거친다 */
static void page_escape(Page* p, const char* s) {
    for (; s && *s; s++) {
        switch (*s) {
            case '&':  page_printf(p, "&amp;");  break;
            case '<':  page_printf(p, "&lt;");   break;
            case '>':  page_printf(p, "&gt;");   break;
            case '"':  page_printf(p, "&quot;"); break;
            case '\'': page_printf(p, "&#39;");  break;
            default:   page_printf(p, "%c", *s); break;
        }
    }
}

static void send_page(HttpResponse* res, int status, Page* p) {
    if (p->overflow) {
        res->setStatus(res, 500);
        res->sendText(res, "page too large");
        return;
    }
    res->setStatus(res, status);
    res->setHeader(res, "Content-Type", "text/html; charset=utf-8");
    res->sendText(res, p->data);
}

static bool parse_int_range(const char* s, int lo, int hi, int* out) {
    if (!s || !*s) return false;
    long v = 0;
    for (const char* c = s; *c; c++) {
        if (*c < '0' || *c > '9') return false;
        v = v * 10 + (*c - '0');
        if (v > hi) return false;
    }
    if (v < lo) return false;
    *out = (int)v;
    return true;
}

/* =========================================================
 * [3] form 읽기 — 디코드 + 다중 값
 * ========================================================= */
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* application/x-www-form-urlencoded 디코드: '+' → ' ', %XX → 바이트 */
static bool url_decode(const char* src, size_t src_len, char* dst, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= src_len) return false;
            int hi = hexval(src[i + 1]);
            int lo = hexval(src[i + 2]);
            if (hi < 0 || lo < 0) return false;
            c = (char)(hi * 16 + lo);
            if (c == '\0') return false;
            i += 2;
        }
        if (o + 1 >= cap) return false;
        dst[o++] = c;
    }
    dst[o] = '\0';
    return true;
}

/* 단일 값: req->form 에서 꺼내 디코드 (없으면 false) */
static bool form_get(HttpRequest* req, const char* key, char* out, size_t cap) {
    const char* raw = req->form ? hashmap_get_str(req->form, key) : NULL;
    if (!raw) return false;
    return url_decode(raw, strlen(raw), out, cap);
}

/* 다중 값: 같은 name 의 값을 원문 body 에서 전부 모아 ", " 로 이어 붙인다 */
static bool form_get_all(HttpRequest* req, const char* key, char* out, size_t cap) {
    out[0] = '\0';
    const char* p = (const char*)req->body;
    if (!p) return true;

    size_t used = 0;
    while (*p) {
        const char* amp = strchr(p, '&');
        size_t tok_len = amp ? (size_t)(amp - p) : strlen(p);
        const char* eq = memchr(p, '=', tok_len);

        if (eq) {
            char k[64];
            char v[64];
            if (url_decode(p, (size_t)(eq - p), k, sizeof(k)) && strcmp(k, key) == 0) {
                if (!url_decode(eq + 1, tok_len - (size_t)(eq - p) - 1, v, sizeof(v))) return false;
                int n = snprintf(out + used, cap - used, "%s%s", used ? ", " : "", v);
                if (n < 0 || (size_t)n >= cap - used) return false;
                used += (size_t)n;
            }
        }
        if (!amp) break;
        p = amp + 1;
    }
    return true;
}

static bool in_list(const char* v, const char* const* list) {
    for (; *list; list++) {
        if (strcmp(v, *list) == 0) return true;
    }
    return false;
}

static const char* const DEPTS[]     = { "dev", "ops", "sales", NULL };
static const char* const LEVELS[]    = { "junior", "senior", NULL };
static const char* const INTERESTS[] = { "c", "network", "web", NULL };

/* =========================================================
 * [4] 핸들러 — GET
 * ========================================================= */
static void handle_root(HttpRequest* req, HttpResponse* res, void* ctx) {
    (void)req; (void)ctx;
    res->redirect(res, "/signup");
}

static void handle_signup_form(HttpRequest* req, HttpResponse* res, void* ctx) {
    (void)req; (void)ctx;
    Page p = {0};
    page_printf(&p,
        "<!DOCTYPE html><html lang=\"ko\"><head><meta charset=\"utf-8\"><title>가입</title></head>"
        "<body style=\"font-family:sans-serif;max-width:560px;margin:40px auto\">"
        "<h1>회원 가입</h1>"
        "<form method=\"POST\" action=\"/signup\">"
        "<p>이름 (text, 필수)<br><input name=\"name\" maxlength=\"30\" required></p>"
        "<p>이메일 (text)<br><input name=\"email\" maxlength=\"90\"></p>"
        "<p>나이 (number)<br><input name=\"age\" type=\"number\" min=\"1\" max=\"150\" required></p>"
        "<p>부서 (select)<br><select name=\"dept\">"
        "<option value=\"dev\">개발</option><option value=\"ops\">운영</option>"
        "<option value=\"sales\">영업</option></select></p>"
        "<p>직급 (radio)<br>"
        "<label><input type=\"radio\" name=\"level\" value=\"junior\" checked> 주니어</label> "
        "<label><input type=\"radio\" name=\"level\" value=\"senior\"> 시니어</label></p>"
        "<p>관심 분야 (checkbox 그룹 — 같은 name)<br>"
        "<label><input type=\"checkbox\" name=\"interest\" value=\"c\"> C</label> "
        "<label><input type=\"checkbox\" name=\"interest\" value=\"network\"> 네트워크</label> "
        "<label><input type=\"checkbox\" name=\"interest\" value=\"web\"> 웹</label></p>"
        "<p>메모 (textarea)<br><textarea name=\"memo\" rows=\"3\" cols=\"50\"></textarea></p>"
        "<p><label><input type=\"checkbox\" name=\"notify\" value=\"on\"> 소식 메일 받기 (단일 checkbox)</label></p>"
        "<p><button type=\"submit\">가입</button> <a href=\"/members\">회원 목록</a></p>"
        "</form></body></html>");
    send_page(res, 200, &p);
}

static void handle_member_list(HttpRequest* req, HttpResponse* res, void* ctx) {
    MemberStore* store = (MemberStore*)ctx;

    /* query: ?dept=dev — 서버가 이미 디코드해 둔다 */
    const char* dept = req->query ? hashmap_get_str(req->query, "dept") : NULL;

    Page p = {0};
    page_printf(&p,
        "<!DOCTYPE html><html lang=\"ko\"><head><meta charset=\"utf-8\"><title>회원 목록</title></head>"
        "<body style=\"font-family:sans-serif;max-width:720px;margin:40px auto\">"
        "<h1>회원 목록</h1><p>필터: <a href=\"/members\">전체</a> · "
        "<a href=\"/members?dept=dev\">dev</a> · <a href=\"/members?dept=ops\">ops</a> · "
        "<a href=\"/members?dept=sales\">sales</a></p>"
        "<table border=\"1\" cellpadding=\"6\"><tr><th>ID</th><th>이름</th><th>부서</th><th>직급</th></tr>");

    for (int i = 0; i < store->count; i++) {
        Member* m = &store->items[i];
        if (dept && strcmp(dept, m->dept) != 0) continue;
        page_printf(&p, "<tr><td><a href=\"/members/%d\">%d</a></td><td>", m->id, m->id);
        page_escape(&p, m->name);
        page_printf(&p, "</td><td>%s</td><td>%s</td></tr>", m->dept, m->level);
    }
    page_printf(&p, "</table><p><a href=\"/signup\">+ 가입</a></p></body></html>");
    send_page(res, 200, &p);
}

/* :id → Member* (실패 시 응답까지 보내고 NULL) */
static Member* find_member_or_reply(HttpRequest* req, HttpResponse* res, MemberStore* store) {
    const char* id_str = req->params ? hashmap_get_str(req->params, "id") : NULL;
    int id = 0;
    if (!parse_int_range(id_str, 1, MAX_MEMBERS, &id)) {
        res->setStatus(res, 400);
        res->sendText(res, "invalid id");
        return NULL;
    }
    if (id > store->count) {
        res->setStatus(res, 404);
        res->sendText(res, "member not found");
        return NULL;
    }
    return &store->items[id - 1];
}

static void handle_member_detail(HttpRequest* req, HttpResponse* res, void* ctx) {
    Member* m = find_member_or_reply(req, res, (MemberStore*)ctx);
    if (!m) return;   /* 이미 응답함 — 반드시 return */

    Page p = {0};
    page_printf(&p,
        "<!DOCTYPE html><html lang=\"ko\"><head><meta charset=\"utf-8\"><title>회원 %d</title></head>"
        "<body style=\"font-family:sans-serif;max-width:560px;margin:40px auto\">"
        "<h1>회원 #%d</h1><table border=\"1\" cellpadding=\"6\">", m->id, m->id);
    page_printf(&p, "<tr><th>이름</th><td>");     page_escape(&p, m->name);
    page_printf(&p, "</td></tr><tr><th>이메일</th><td>"); page_escape(&p, m->email);
    page_printf(&p, "</td></tr><tr><th>나이</th><td>%d</td></tr>", m->age);
    page_printf(&p, "<tr><th>부서</th><td>%s</td></tr><tr><th>직급</th><td>%s</td></tr>", m->dept, m->level);
    page_printf(&p, "<tr><th>관심 분야</th><td>%s</td></tr>", m->interests[0] ? m->interests : "-");
    page_printf(&p, "<tr><th>메모</th><td>");     page_escape(&p, m->memo);
    page_printf(&p, "</td></tr><tr><th>소식 메일</th><td>%s</td></tr></table>", m->notify ? "받음" : "안 받음");
    page_printf(&p, "<p><a href=\"/api/members/%d\">JSON 보기</a> · <a href=\"/members\">목록</a></p></body></html>", m->id);
    send_page(res, 200, &p);
}

static void json_put_str(JSONNode* o, const char* key, const char* val) {
    JsonValue* v = new_json_string(val);
    if (!v) return;
    o->put(o, key, (Object*)v);
    RELEASE(v);   /* put 이 RETAIN 했음 */
}

static void json_put_num(JSONNode* o, const char* key, double val) {
    JsonValue* v = new_json_number(val);
    if (!v) return;
    o->put(o, key, (Object*)v);
    RELEASE(v);
}

static void handle_member_json(HttpRequest* req, HttpResponse* res, void* ctx) {
    Member* m = find_member_or_reply(req, res, (MemberStore*)ctx);
    if (!m) return;

    JSONNode* o = new_JSON_Object();
    if (!o) { res->sendStatus(res, 500); return; }

    json_put_num(o, "id", m->id);
    json_put_str(o, "name", m->name);
    json_put_str(o, "email", m->email);
    json_put_num(o, "age", m->age);
    json_put_str(o, "dept", m->dept);
    json_put_str(o, "level", m->level);
    json_put_str(o, "interests", m->interests);
    json_put_str(o, "memo", m->memo);

    JsonValue* notify = new_json_bool(m->notify);
    if (notify) { o->put(o, "notify", (Object*)notify); RELEASE(notify); }

    res->sendJson(res, o);
    RELEASE(o);
}

/* =========================================================
 * [5] 핸들러 — POST (폼 처리)
 * ========================================================= */
static void reply_form_error(HttpResponse* res, const char* msg) {
    Page p = {0};
    page_printf(&p,
        "<!DOCTYPE html><html lang=\"ko\"><head><meta charset=\"utf-8\"><title>입력 오류</title></head>"
        "<body style=\"font-family:sans-serif;max-width:560px;margin:40px auto\">"
        "<h1>입력 오류</h1><p>%s</p><p><a href=\"/signup\">다시 입력</a></p></body></html>", msg);
    send_page(res, 400, &p);
}

static void handle_signup_submit(HttpRequest* req, HttpResponse* res, void* ctx) {
    MemberStore* store = (MemberStore*)ctx;

    if (store->count >= MAX_MEMBERS) {
        reply_form_error(res, "정원이 찼습니다.");
        return;
    }

    Member m = {0};
    char age_str[8] = {0};
    char notify[8]  = {0};

    /* text: 필수 */
    if (!form_get(req, "name", m.name, sizeof(m.name)) || m.name[0] == '\0') {
        reply_form_error(res, "이름은 필수입니다 (30자 이내).");
        return;
    }
    /* text: 선택 — 없어도 빈 문자열 */
    if (req->form && hashmap_get_str(req->form, "email")) {
        if (!form_get(req, "email", m.email, sizeof(m.email))) {
            reply_form_error(res, "이메일 형식이 너무 깁니다.");
            return;
        }
    }
    /* number: 문자열로 오니 직접 검증 */
    if (!form_get(req, "age", age_str, sizeof(age_str)) || !parse_int_range(age_str, 1, 150, &m.age)) {
        reply_form_error(res, "나이는 1~150 숫자여야 합니다.");
        return;
    }
    /* select / radio: 브라우저를 믿지 말고 허용 목록으로 확인 */
    if (!form_get(req, "dept", m.dept, sizeof(m.dept)) || !in_list(m.dept, DEPTS)) {
        reply_form_error(res, "부서 값이 올바르지 않습니다.");
        return;
    }
    if (!form_get(req, "level", m.level, sizeof(m.level)) || !in_list(m.level, LEVELS)) {
        reply_form_error(res, "직급 값이 올바르지 않습니다.");
        return;
    }
    /* checkbox 그룹: 같은 name 여러 개 → 원문에서 전부 모은 뒤 하나씩 검증 */
    if (!form_get_all(req, "interest", m.interests, sizeof(m.interests))) {
        reply_form_error(res, "관심 분야 값이 너무 깁니다.");
        return;
    }
    {
        char tmp[sizeof(m.interests)];
        snprintf(tmp, sizeof(tmp), "%s", m.interests);
        char* save = NULL;
        for (char* t = strtok_r(tmp, ", ", &save); t; t = strtok_r(NULL, ", ", &save)) {
            if (!in_list(t, INTERESTS)) {
                reply_form_error(res, "관심 분야 값이 올바르지 않습니다.");
                return;
            }
        }
    }
    /* textarea: 선택 */
    if (req->form && hashmap_get_str(req->form, "memo")) {
        if (!form_get(req, "memo", m.memo, sizeof(m.memo))) {
            reply_form_error(res, "메모가 너무 깁니다.");
            return;
        }
    }
    /* 단일 checkbox: 체크 안 하면 키 자체가 안 온다 */
    m.notify = form_get(req, "notify", notify, sizeof(notify)) && strcmp(notify, "on") == 0;

    m.id = store->count + 1;
    store->items[store->count] = m;
    store->count++;

    /* PRG: POST 결과를 GET 페이지로 넘긴다 → 새로고침해도 중복 가입 없음 */
    char location[32];
    snprintf(location, sizeof(location), "/members/%d", m.id);
    res->redirect(res, location);
}

/* =========================================================
 * [6] main — 조립
 * ========================================================= */
int main(void) {
    signal(SIGPIPE, SIG_IGN);

    static MemberStore store;                /* 0 으로 초기화됨 */

    Router* router = new_Router(&store);     /* 모든 핸들러의 ctx 로 전달 */
    if (!router) return 1;

    router->GET (router, "/",                handle_root);
    router->GET (router, "/signup",          handle_signup_form);
    router->POST(router, "/signup",          handle_signup_submit);
    router->GET (router, "/members",         handle_member_list);
    router->GET (router, "/members/:id",     handle_member_detail);
    router->GET (router, "/api/members/:id", handle_member_json);

    EventLoop* loop = event_loop_create();
    if (!loop) { RELEASE(router); return 1; }

    HttpServer* server = new_HttpServer(loop, router);
    RELEASE(router);                         /* server 가 RETAIN 했음 */
    if (!server) { event_loop_destroy(loop); return 1; }

    if (server->listen(server, 8080) != 0) {
        fprintf(stderr, "listen 8080 failed\n");
        RELEASE(server);
        event_loop_destroy(loop);
        return 1;
    }

    g_loop = loop;
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    printf("http://127.0.0.1:8080/  (Ctrl+C 종료)\n");
    event_loop_run(loop);

    RELEASE(server);
    event_loop_destroy(loop);
    return 0;
}
