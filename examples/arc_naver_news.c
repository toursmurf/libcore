/**
 * @file arc_naver_news.c
 * @brief libcore HttpClient (동기 블로킹 TCP / HTTPS) 기반 네이버 뉴스 크롤러
 * @note  io_uring 링은 HttpClient 내부에 소유되나, impl_execute 경로는
 *        HttpTransport 블로킹 TCP/SSL을 사용 → io_uring 혜택 없음 실증 확인.
 *        (epoll 10.258s vs io_uring 10.331s — 차이 0.07초)
 *        실제 io_uring I/O 혜택은 HttpTransport 비동기 재설계 후 가능 (v1.7 예정)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include "libcore.h"

typedef struct {
    const char* name;
    const char* url;
} NaverSection;

static NaverSection sections[] = {
    { "정치",    "https://news.naver.com/section/100" },
    { "경제",    "https://news.naver.com/section/101" },
    { "사회",    "https://news.naver.com/section/102" },
    { "생활문화","https://news.naver.com/section/103" },
    { "세계",    "https://news.naver.com/section/104" },
    { "IT",      "https://news.naver.com/section/105" },
};

#define SECTION_COUNT \
    (int)(sizeof(sections) / sizeof(sections[0]))
#define MAX_PER_SECTION  15
#define TITLE_MIN_LEN     5
#define TITLE_MAX_LEN   150

#define CHROME_UA \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) " \
    "AppleWebKit/537.36 (KHTML, like Gecko) "     \
    "Chrome/131.0.0.0 Safari/537.36"

#define NAVER_TIMEOUT_MS 30000

typedef struct {
    char title[512];
    char link[512];
} NaverNewsItem;

static void decode_html_entities(char* str, size_t max_len) {
    char out[2048] = {0};
    int  oi = 0, i = 0, len = (int)strlen(str);
    while (i < len && oi < (int)max_len - 4) {
        if (str[i] != '&') { out[oi++] = str[i++]; continue; }
        if      (strncmp(&str[i], "&lt;",   4) == 0) { out[oi++] = '<';  i += 4; }
        else if (strncmp(&str[i], "&gt;",   4) == 0) { out[oi++] = '>';  i += 4; }
        else if (strncmp(&str[i], "&amp;",  5) == 0) { out[oi++] = '&';  i += 5; }
        else if (strncmp(&str[i], "&quot;", 6) == 0) { out[oi++] = '"';  i += 6; }
        else if (strncmp(&str[i], "&nbsp;", 6) == 0) { out[oi++] = ' ';  i += 6; }
        else if (strncmp(&str[i], "&#x27;", 6) == 0) { out[oi++] = '\''; i += 6; }
        else if (strncmp(&str[i], "&#x3D;", 6) == 0) { out[oi++] = '=';  i += 6; }
        else if (strncmp(&str[i], "&#x60;", 6) == 0) { out[oi++] = '`';  i += 6; } /* 🚀 [패치] 백틱 기호 추가 */
        else if (strncmp(&str[i], "&#39;",  5) == 0) { out[oi++] = '\''; i += 5; }
        else if (strncmp(&str[i], "&#039;", 6) == 0) { out[oi++] = '\''; i += 6; }
        else if (strncmp(&str[i], "&#8216;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\x98", 3); oi += 3; i += 7; }
        else if (strncmp(&str[i], "&#8217;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\x99", 3); oi += 3; i += 7; }
        else if (strncmp(&str[i], "&#8220;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\x9c", 3); oi += 3; i += 7; }
        else if (strncmp(&str[i], "&#8221;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\x9d", 3); oi += 3; i += 7; }
        else if (strncmp(&str[i], "&#8211;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\x93", 3); oi += 3; i += 7; }
        else if (strncmp(&str[i], "&#8212;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\x94", 3); oi += 3; i += 7; }
        else if (strncmp(&str[i], "&#8230;", 7) == 0) {
            memcpy(&out[oi], "\xe2\x80\xa6", 3); oi += 3; i += 7; }
        else { out[oi++] = str[i++]; }
    }
    out[oi] = '\0';
    size_t ol = strlen(out);
    if (ol >= max_len) ol = max_len - 1;
    memcpy(str, out, ol);
    str[ol] = '\0';
}

static void strip_html(char* str, size_t max_len) {
    char clean[2048] = {0};
    int  ci = 0;
    bool in_tag = false, last_was_space = true;

    for (int i = 0; str[i] && ci < (int)max_len - 1; i++) {
        if      (str[i] == '<') { in_tag = true;  continue; }
        else if (str[i] == '>') { in_tag = false; continue; }
        if (in_tag) continue;
        if (str[i] == '\n' || str[i] == '\r' ||
            str[i] == '\t' || str[i] == ' ') {
            if (!last_was_space && ci > 0) {
                clean[ci++] = ' '; last_was_space = true;
            }
        } else { clean[ci++] = str[i]; last_was_space = false; }
    }
    if (ci > 0 && clean[ci - 1] == ' ') ci--;
    clean[ci] = '\0';
    size_t cl = strlen(clean);
    if (cl >= max_len) cl = max_len - 1;
    memcpy(str, clean, cl);
    str[cl] = '\0';
}

static void utf8_safe_truncate(
        char* dst, const char* src, size_t max_bytes) {
    if (max_bytes < 8) { dst[0] = '\0'; return; }
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j < max_bytes - 4) {
        unsigned char c = (unsigned char)src[i];
        size_t cl = 1;
        if      ((c & 0xE0) == 0xC0) cl = 2;
        else if ((c & 0xF0) == 0xE0) cl = 3;
        else if ((c & 0xF8) == 0xF0) cl = 4;
        if (j + cl >= max_bytes - 4) break;
        for (size_t k = 0; k < cl; k++) {
            if (src[i] == '\0') break;
            dst[j++] = src[i++];
        }
    }
    if (src[i] != '\0') {
        dst[j++] = '.'; dst[j++] = '.'; dst[j++] = '.';
    }
    dst[j] = '\0';
}

static int parse_naver_news(
        const char* html, NaverNewsItem* items, int max) {
    int count = 0;
    const char* ptr = html;

    while (ptr && count < max) {
        const char* href = strstr(ptr,
            "href=\"https://n.news.naver.com/");
        if (!href) break;

        const char* url_s = href + 6;
        const char* url_e = strchr(url_s, '"');
        if (!url_e) { ptr = href + 1; continue; }

        size_t url_len = (size_t)(url_e - url_s);
        if (url_len == 0 || url_len >= sizeof(items[0].link)) {
            ptr = href + 1; continue;
        }

        char url[512];
        memcpy(url, url_s, url_len);
        url[url_len] = '\0';

        /* 🚀 [패치] URL 내의 HTML 엔티티(&amp;, &#x3D; 등)를 파싱 직후 즉각 디코딩! */
        decode_html_entities(url, sizeof(url));
        url_len = strlen(url); /* 디코딩 후 줄어든 문자열 길이에 맞춰 재계산 */

        if (!strstr(url, "/article/")) { ptr = href + 1; continue; }

        bool dup = false;
        for (int d = 0; d < count; d++) {
            if (strcmp(items[d].link, url) == 0) { dup = true; break; }
        }
        if (dup) { ptr = url_e + 1; continue; }

        const char* tag_end = strchr(url_e, '>');
        if (!tag_end) { ptr = href + 1; continue; }

        const char* a_close = strstr(tag_end + 1, "</a>");
        if (!a_close) { ptr = href + 1; continue; }

        size_t text_len = (size_t)(a_close - (tag_end + 1));
        if (text_len == 0 || text_len >= sizeof(items[0].title)) {
            ptr = a_close + 4; continue;
        }

        char title_raw[512];
        memcpy(title_raw, tag_end + 1, text_len);
        title_raw[text_len] = '\0';

        strip_html(title_raw, sizeof(title_raw));
        decode_html_entities(title_raw, sizeof(title_raw));

        size_t tl = strlen(title_raw);
        if (tl < TITLE_MIN_LEN || tl > TITLE_MAX_LEN) {
            ptr = a_close + 4; continue;
        }
        // 🚀 [패치] CSS 스타일 잔재 필터링
        if (strstr(title_raw, "style=") || strstr(title_raw, "display:")) {
            ptr = a_close + 4; continue;
        }
        memcpy(items[count].title, title_raw, tl + 1);
        memcpy(items[count].link,  url,       url_len + 1);
        count++;
        ptr = a_close + 4;
    }
    return count;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

typedef struct {
    const char*      name;
    const char*      url;
    ArrayList*       all_list;
    HashMap*         map;
    pthread_mutex_t* lock;
    double           elapsed;
} FetchTask;

static void* fetch_news(void* arg) {
    FetchTask* task = (FetchTask*)arg;
    double t0 = now_ms();

    HttpClient* client = new_HttpClient(NULL);
    if (!client) { task->elapsed = 0; return NULL; }

    client->options.timeout_ms = NAVER_TIMEOUT_MS;

    client->setHeader(client, "User-Agent",      CHROME_UA);
    client->setHeader(client, "Accept",
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    client->setHeader(client, "Accept-Language", "ko-KR,ko;q=0.9,en-US;q=0.8");
    client->setHeader(client, "Referer",         "https://www.naver.com/");

    HttpClientResponse* resp = client->GET(client, task->url, NULL);
    task->elapsed = now_ms() - t0;

    if (resp != NULL
        && resp->status_code >= 200
        && resp->status_code <  300
        && resp->body        != NULL
        && resp->body_len     > 0) {

        NaverNewsItem items[MAX_PER_SECTION];
        int count = parse_naver_news(resp->body, items, MAX_PER_SECTION);

        if (count > 0) {
            ArrayList* src = new_ArrayList(count);

            for (int i = 0; i < count; i++) {
                char str[1024];
                char safe_title[400];
                char safe_link[512];

                utf8_safe_truncate(safe_title, items[i].title, 380);
                utf8_safe_truncate(safe_link,  items[i].link,  500);

                snprintf(str, sizeof(str),
                    "제목: %s\n  링크: %s",
                    safe_title, safe_link);

                String* news_obj = new_String(str);
                src->add(src, (Object*)news_obj);
                RELEASE((Object*)news_obj);
            }

            pthread_mutex_lock(task->lock);
            for (int i = 0; i < src->size; i++) {
                String* s = (String*)src->get(src, i);
                task->all_list->add(task->all_list, (Object*)s);
            }
            task->map->put(task->map, task->name, (Object*)src);
            pthread_mutex_unlock(task->lock);
            RELEASE((Object*)src);
        }
    }

    if (resp) RELEASE((Object*)resp);
    RELEASE((Object*)client);
    return NULL;
}

int main(void) {
    printf("\n");
    printf("========================================================\n");
    printf("  libcore  - 네이버 뉴스 크롤러 (Cookie/Redirect/Multipart 완벽 지원)\n");
    printf("  HttpClient(동기 블로킹 TCP / HTTPS) + Chrome UA\n");
    printf("  ThreadPool + ArrayList + HashMap\n");
    printf("  Toos IT Holdings\n");
    printf("========================================================\n\n");

    double total_start = now_ms();

    ArrayList*      all  = new_ArrayList(SECTION_COUNT * MAX_PER_SECTION);
    HashMap*        map  = new_HashMap(16);
    pthread_mutex_t lock;
    pthread_mutex_init(&lock, NULL);

    ThreadPool* pool = new_ThreadPool(SECTION_COUNT, SECTION_COUNT * 2);
    FetchTask tasks[SECTION_COUNT];

    printf("📡 수집 시작! (%d개 섹션 병렬 · ThreadPool 블로킹 TCP)\n\n",
        SECTION_COUNT);

    for (int i = 0; i < SECTION_COUNT; i++) {
        tasks[i].name     = sections[i].name;
        tasks[i].url      = sections[i].url;
        tasks[i].all_list = all;
        tasks[i].map      = map;
        tasks[i].lock     = &lock;
        tasks[i].elapsed  = 0;
        pool->submit(pool, fetch_news, &tasks[i]);
        printf("  → [%s] %s\n", sections[i].name, sections[i].url);
    }

    printf("\n⏳ 병렬 수집 중...\n\n");
    pool->shutdown(pool);

    double total_elapsed = now_ms() - total_start;

    printf("============================================\n");
    printf("  📰 섹션별 뉴스\n");
    printf("============================================\n");

    double seq_total = 0;
    for (int i = 0; i < SECTION_COUNT; i++) {
        seq_total += tasks[i].elapsed;

        ArrayList* src = (ArrayList*)map->get(map, sections[i].name);
        if (!src) {
            printf("\n🔹 [%s] 수집 실패 (%.0fms)\n",
                sections[i].name, tasks[i].elapsed);
            printf("     → HTML 구조 변경·타임아웃 가능성\n");
            continue;
        }

        printf("\n🔹 [%s] %d건 | %.0fms\n",
            sections[i].name, src->size, tasks[i].elapsed);
        printf("  ──────────────────────────────────────\n");
        for (int j = 0; j < src->size; j++) {
            String* s = (String*)src->get(src, j);
            printf("\n  [%02d] %s\n", j + 1, s->value);
        }
        printf("  ──────────────────────────────────────\n");
    }

    printf("\n========================================================\n");
    printf("  ✅ 수집 완료!\n");
    printf("  총 뉴스: %d건\n", all->size);
    printf("  ────────────────────────────────────────────────────────\n");
    printf("  Sequential (Est): %.0fms\n", seq_total);
    printf("  Parallel   (Act): %.0fms\n", total_elapsed);
    printf("  Time Saved:       %.0fms\n",
        seq_total - total_elapsed);
    printf("  ────────────────────────────────────────────────────────\n");
    printf("  HttpClient(동기 블로킹 TCP / HTTPS) · Chrome UA\n");
    printf("  ThreadPool · ArrayList · HashMap\n");
    printf("========================================================\n\n");

    RELEASE((Object*)pool);
    RELEASE((Object*)all);
    RELEASE((Object*)map);
    pthread_mutex_destroy(&lock);

    printf("[TOOS-IT] Naver news crawler shutdown complete.\n");
    return 0;
}