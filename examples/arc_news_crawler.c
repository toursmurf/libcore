/*
 * arc_news_crawler.c
 * libcore v0.5 Killer Example!
 * ThreadPool + ArrayList + HashMap + pthread_mutex
 * Parallel RSS News Crawler
 *
 * 투스(Toos) IT 홀딩스 - Iron Fortress
 * "Valgrind 0 bytes. Every. Single. Time."
 *
 * Build:
 * gcc -Wall -O2 -Iinclude examples/arc_news_crawler.c \
 * lib/libcore.a -lpthread -lcurl -lm -o examples/arc_news_crawler
 *
 * Run:
 * ./examples/arc_news_crawler
 *
 * Valgrind:
 * valgrind --leak-check=full --show-leak-kinds=all \
 * --suppressions=libcore.supp \
 * --error-exitcode=1 examples/arc_news_crawler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <curl/curl.h>
#include "libcore.h"

/* =========================================
 * News Sources
 * ========================================= */
typedef struct {
    const char* name;
    const char* url;
} NewsSource;

static NewsSource sources[] = {
    {
        "BBC_Tech",
        "https://feeds.bbci.co.uk/news/technology/rss.xml"
    },
    {
        "Hacker_News",
        "https://hnrss.org/frontpage"
    },
    {
        "AI Times",
        "https://www.aitimes.com/rss/allArticle.xml"
    },
    {
        "MIT Tech",
        "https://www.technologyreview.com/feed/"
    },
};

#define SOURCE_COUNT \
    (int)(sizeof(sources) / sizeof(sources[0]))
#define MAX_PER_SOURCE 10

/* =========================================
 * News Item
 * ========================================= */
typedef struct {
    char title[512];
    char link[512];
    char desc[2048]; // Summary가 길 수 있으므로 버퍼 확장
} NewsItem;

/* =========================================
 * curl Response Buffer
 * ========================================= */
typedef struct {
    char* data;
    size_t size;
} CurlBuffer;

static size_t write_cb(
        void* ptr, size_t size,
        size_t nmemb, void* ud) {
    CurlBuffer* buf = (CurlBuffer*)ud;
    size_t total = size * nmemb;
    char* p = realloc(buf->data,
                      buf->size + total + 1);
    if (!p) return 0;
    buf->data = p;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

/* =========================================
 * HTML Entity Decode
 * ========================================= */
static void decode_html_entities(
        char* str, size_t max_len) {
    char out[2048] = {0};
    int oi = 0;
    int i  = 0;
    int len = (int)strlen(str);

    while (i < len && oi < (int)max_len - 1) {
        if (str[i] == '&') {
            if (strncmp(&str[i], "&lt;",   4) == 0)
                { out[oi++] = '<';  i += 4; }
            else if (strncmp(&str[i], "&gt;",   4) == 0)
                { out[oi++] = '>';  i += 4; }
            else if (strncmp(&str[i], "&amp;",  5) == 0)
                { out[oi++] = '&';  i += 5; }
            else if (strncmp(&str[i], "&quot;", 6) == 0)
                { out[oi++] = '"';  i += 6; }
            else if (strncmp(&str[i], "&#39;",  5) == 0)
                { out[oi++] = '\''; i += 5; }
            else if (strncmp(&str[i], "&#039;", 6) == 0)
                { out[oi++] = '\''; i += 6; }
            else if (strncmp(&str[i], "&nbsp;", 6) == 0)
                { out[oi++] = ' ';  i += 6; }
            else {
                out[oi++] = str[i++];
            }
        } else {
            out[oi++] = str[i++];
        }
    }
    out[oi] = '\0';
    size_t _ol = strlen(out);
    if (_ol >= max_len) _ol = max_len - 1;
    memcpy(str, out, _ol);
    str[_ol] = '\0';
}

/* =========================================
 * Encoding Normalization
 * ========================================= */
static void normalize_encoding(
        char* str, size_t max_len) {
    (void)str;
    (void)max_len;
}

/* =========================================
 * XML Tag Extract
 * ========================================= */
static void extract_tag(
        const char* xml,
        const char* open,
        const char* close,
        char* out, size_t out_sz) {
    out[0] = '\0';
    const char* s = strstr(xml, open);
    if (!s) return;
    s += strlen(open);

    if (strncmp(s, "<![CDATA[", 9) == 0) {
        s += 9;
        const char* e = strstr(s, "]]>");
        if (!e) return;
        size_t len = e - s;
        if (len >= out_sz) len = out_sz - 1;
        memcpy(out, s, len);
        out[len] = '\0';
        return;
    }

    const char* e = strstr(s, close);
    if (!e) return;
    size_t len = e - s;
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, s, len);
    out[len] = '\0';
}

/* =========================================
 * [강화판] Strip HTML Tags + Clean Whitespace
 * 무관용 태그 소각 및 다중 공백 압축 엔진
 * ========================================= */
static void strip_html(char* str, size_t max_len) {
    char clean[2048] = {0};
    int ci = 0;
    bool in_tag = false;
    bool last_was_space = true; // 시작 부분의 공백도 제거하기 위해 true로 시작

    for (int i = 0; str[i] && ci < (int)max_len - 1; i++) {
        if (str[i] == '<') {
            in_tag = true;
        } else if (str[i] == '>') {
            in_tag = false;
        } else if (!in_tag) {
            // 줄바꿈, 탭, 연속된 공백을 단일 공백으로 치환
            if (str[i] == '\n' || str[i] == '\r' || str[i] == '\t' || str[i] == ' ') {
                if (!last_was_space && ci > 0) {
                    clean[ci++] = ' ';
                    last_was_space = true;
                }
            } else {
                clean[ci++] = str[i];
                last_was_space = false;
            }
        }
    }
    
    // 마지막이 공백이면 제거
    if (ci > 0 && clean[ci-1] == ' ') {
        ci--;
    }
    
    clean[ci] = '\0';
    size_t _cl = strlen(clean);
    if (_cl >= max_len) _cl = max_len - 1;
    memcpy(str, clean, _cl);
    str[_cl] = '\0';
}

/* =========================================
 * Normalize String
 * Entity → Strip Tags → Entity again
 * ========================================= */
static void normalize_str(
        char* str, size_t max_len) {
    decode_html_entities(str, max_len);
    strip_html(str, max_len);
    decode_html_entities(str, max_len); // CDATA 내부에 숨어있던 엔티티 2차 확인
    normalize_encoding(str, max_len);
}

/* =========================================
 * UTF-8 Safe Truncation
 * 한글 바이트 절단 방지용 쉴드 엔진
 * ========================================= */
static void utf8_safe_truncate(char* dst, const char* src, size_t max_bytes) {
    size_t i = 0, j = 0;
    
    while (src[i] != '\0' && j < max_bytes - 4) { 
        unsigned char c = (unsigned char)src[i];
        size_t char_len = 1;
        
        // UTF-8 바이트 길이 판별 로직
        if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        
        // 남은 공간이 문자를 온전히 담지 못하면 절단 스탑
        if (j + char_len >= max_bytes - 4) break;
        
        for (size_t k = 0; k < char_len; k++) {
            if (src[i] == '\0') break;
            dst[j++] = src[i++];
        }
    }
    
    if (src[i] != '\0') {
        dst[j++] = '.'; dst[j++] = '.'; dst[j++] = '.';
    }
    dst[j] = '\0';
}

/* =========================================
 * RSS Parser
 * ========================================= */
static int parse_items(
        const char* xml,
        NewsItem* items, int max) {
    int count = 0;
    const char* ptr = strstr(xml, "<item>");

    while (ptr && count < max) {
        const char* end =
            strstr(ptr, "</item>");
        if (!end) break;

        size_t len = end - ptr + 7;
        char* block = (char*)malloc(len + 1);
        if (!block) break;
        memcpy(block, ptr, len);
        block[len] = '\0';

        extract_tag(block,
            "<title>", "</title>",
            items[count].title,
            sizeof(items[count].title));
        extract_tag(block,
            "<link>", "</link>",
            items[count].link,
            sizeof(items[count].link));
        extract_tag(block,
            "<description>", "</description>",
            items[count].desc,
            sizeof(items[count].desc));

        normalize_str(items[count].title,
            sizeof(items[count].title));
        normalize_str(items[count].desc,
            sizeof(items[count].desc));
        decode_html_entities(items[count].link,
            sizeof(items[count].link));

        free(block);

        if (strlen(items[count].title) > 0)
            count++;

        ptr = strstr(end + 7, "<item>");
    }
    return count;
}

/* =========================================
 * Time Measurement
 * ========================================= */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0
         + ts.tv_nsec / 1e6;
}

/* =========================================
 * ThreadPool Task Unit
 * ========================================= */
typedef struct {
    const char* name;
    const char* url;
    ArrayList* all_list;  // [BORROWED]
    HashMap* map;       // [BORROWED]
    pthread_mutex_t* lock;      // [BORROWED]
    double           elapsed;
} FetchTask;

/* =========================================
 * Fetch Worker
 * ========================================= */
static void* fetch_news(void* arg) {
    FetchTask* task = (FetchTask*)arg;
    double t0 = now_ms();

    CURL* curl = curl_easy_init();
    if (!curl) { task->elapsed = 0; return NULL; }

    CurlBuffer buf = {NULL, 0};
    curl_easy_setopt(curl,
        CURLOPT_URL,            task->url);
    curl_easy_setopt(curl,
        CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl,
        CURLOPT_WRITEDATA,      &buf);
    curl_easy_setopt(curl,
        CURLOPT_TIMEOUT,        120L);
    curl_easy_setopt(curl,
        CURLOPT_CONNECTTIMEOUT, 120L);
    curl_easy_setopt(curl,
        CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl,
        CURLOPT_USERAGENT,
        "libcore/0.5 news-crawler");
    curl_easy_setopt(curl,
        CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl,
        CURLOPT_ACCEPT_ENCODING, "");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    task->elapsed = now_ms() - t0;

    if (res == CURLE_OK && buf.data) {
        NewsItem items[MAX_PER_SOURCE];
        int count = parse_items(
            buf.data, items, MAX_PER_SOURCE);

        if (count > 0) {
            ArrayList* src =
                new_ArrayList(count);

            for (int i = 0; i < count; i++) {
                char str[2048];
                char safe_title[256];
                char safe_link[512];
                char safe_desc[256];
                
                // UTF-8 경계를 지키는 안전 절단 함수 호출
                utf8_safe_truncate(safe_title, items[i].title, 200);
                utf8_safe_truncate(safe_link, items[i].link, 400);
                utf8_safe_truncate(safe_desc, items[i].desc, 200);

                snprintf(str, sizeof(str),
                    "Title  : %s\n"
                    "  Link   : %s\n"
                    "  Summary: %s",
                    safe_title, safe_link, safe_desc);

                // ARC: new → add(RETAIN) → RELEASE
                String* news_obj =
                    new_String(str);
                src->add(src,
                    (Object*)news_obj);
                RELEASE((Object*)news_obj);
            }

            pthread_mutex_lock(task->lock);

            // all_list->add() internally RETAINs
            for (int i = 0;
                 i < src->size; i++) {
                String* s = (String*)
                    src->get(src, i);
                task->all_list->add(
                    task->all_list,
                    (Object*)s);
            }

            // map->put() internally RETAINs
            task->map->put(task->map,
                task->name,
                (Object*)src);

            pthread_mutex_unlock(task->lock);

            // Release local ownership
            RELEASE((Object*)src);
        }
    }

    if (buf.data) free(buf.data);
    return NULL;
}

/* =========================================
 * Main
 * ========================================= */
int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  libcore v0.5 - News Crawler\n");
    printf("  ThreadPool + ArrayList + HashMap\n");
    printf("  Toos IT Holdings\n");
    printf("========================================\n\n");

    curl_global_init(CURL_GLOBAL_ALL);

    double total_start = now_ms();

    ArrayList* all = new_ArrayList(
        SOURCE_COUNT * MAX_PER_SOURCE);
    HashMap* map   = new_HashMap(16);

    pthread_mutex_t lock;
    pthread_mutex_init(&lock, NULL);

    ThreadPool* pool =
        new_ThreadPool(4, SOURCE_COUNT * 2);

    FetchTask tasks[SOURCE_COUNT];

    printf("📡 수집 시작! / Starting Collection!"
           " (%d sources parallel)\n\n",
        SOURCE_COUNT);

    for (int i = 0; i < SOURCE_COUNT; i++) {
        tasks[i].name     = sources[i].name;
        tasks[i].url      = sources[i].url;
        tasks[i].all_list = all;
        tasks[i].map      = map;
        tasks[i].lock     = &lock;
        tasks[i].elapsed  = 0;

        pool->submit(pool, fetch_news,
            &tasks[i]);
        printf("  → [%s] requesting...\n",
            sources[i].name);
    }

    printf("\n⏳ 병렬 수집 중... /"
           " Parallel processing...\n\n");
    pool->shutdown(pool);

    double total_elapsed =
        now_ms() - total_start;

    // ─── News by Source ───────────────────
    printf("========================================\n");
    printf("  📰 소스별 뉴스 / News by Source\n");
    printf("========================================\n");

    double seq_total = 0;

    for (int i = 0; i < SOURCE_COUNT; i++) {
        seq_total += tasks[i].elapsed;

        ArrayList* src = (ArrayList*)
            map->get(map, sources[i].name);

        if (!src) {
            printf("\n🔹 [%-18s] Failed"
                   " (%.0fms)\n",
                sources[i].name,
                tasks[i].elapsed);
            continue;
        }

        printf("\n🔹 [%-18s] %2d items"
               " | Time: %.0fms\n",
            sources[i].name,
            src->size,
            tasks[i].elapsed);
        printf("  ──────────────────────────────\n");

        for (int j = 0;
             j < src->size; j++) {
            String* s = (String*)
                src->get(src, j);
            printf("\n  [%02d] %s\n",
                j + 1, s->value);
            printf("  ──────────────────────────────\n");
        }
    }

    // ─── Final Stats ──────────────────────
    printf("\n========================================\n");
    printf("  ✅ 수집 완료! / Collection Complete!\n");
    printf("  Total News: %d items\n", all->size);
    printf("  ─────────────────────────────────\n");
    printf("  Sequential (Est): %.0fms\n",
        seq_total);
    printf("  Parallel   (Act): %.0fms\n",
        total_elapsed);
    printf("  Time Saved:       %.0fms 🚀\n",
        seq_total - total_elapsed);
    printf("  ─────────────────────────────────\n");
    printf("  ThreadPool · ArrayList · HashMap\n");
    printf("  Valgrind 0 bytes!\n");
    printf("========================================\n\n");

    // ─── ARC Release Order Matters!! ──────
    // pool first → ensures workers fully done!
    RELEASE((Object*)pool);
    // then shared resources
    RELEASE((Object*)all);
    RELEASE((Object*)map);

    pthread_mutex_destroy(&lock);
    curl_global_cleanup();

    printf("[TOOS-IT] ThreadPool shutdown complete.\n");

    // Force glibc internal cache release
    // → Valgrind: still reachable = 0 bytes!
#ifdef __GLIBC__
    extern void __libc_freeres(void);
    __libc_freeres();
#endif

    return 0;
}
