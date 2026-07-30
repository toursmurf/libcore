/**
 * @file arc_naver_news.c
 * @brief libcore HttpClient 기반 네이버 뉴스 병렬 크롤러
 *
 * @note
 *   HttpClient는 현재 동기 블로킹 TCP/HTTPS 경로를 사용한다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <ctype.h>

#include "libcore.h"

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char* name;
    const char* url;
} NaverSection;

static const NaverSection sections[] = {
    { "정치",     "https://news.naver.com/section/100" },
    { "경제",     "https://news.naver.com/section/101" },
    { "사회",     "https://news.naver.com/section/102" },
    { "생활문화", "https://news.naver.com/section/103" },
    { "세계",     "https://news.naver.com/section/104" },
    { "IT",       "https://news.naver.com/section/105" }
};

#define SECTION_COUNT \
    ((int)(sizeof(sections) / sizeof(sections[0])))

#define MAX_PER_SECTION   15
#define TITLE_MIN_BYTES    5
#define TITLE_MAX_BYTES  450

#define NAVER_TIMEOUT_MS 30000

#define CHROME_UA                                                     \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "                      \
    "AppleWebKit/537.36 (KHTML, like Gecko) "                         \
    "Chrome/131.0.0.0 Safari/537.36"

/* -------------------------------------------------------------------------- */
/* Domain                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    char title[512];
    char link[512];
} NaverNewsItem;

typedef enum {
    FETCH_PENDING = 0,
    FETCH_OK,
    FETCH_CLIENT_CREATE_FAILED,
    FETCH_REQUEST_FAILED,
    FETCH_HTTP_ERROR,
    FETCH_EMPTY_BODY,
    FETCH_PARSE_EMPTY,
    FETCH_RESULT_CREATE_FAILED
} FetchStatus;

typedef struct {
    const char*      name;
    const char*      url;
    ArrayList*       all_list;
    HashMap*         map;
    pthread_mutex_t* lock;

    double           elapsed_ms;
    FetchStatus      status;
    int              http_status;
    int              item_count;
} FetchTask;

typedef struct {
    int    fastest_index;
    int    slowest_index;
    int    success_count;
    int    failure_count;

    double fastest_ms;
    double slowest_ms;
    double average_ms;
    double sequential_ms;
} FetchStatistics;

/* -------------------------------------------------------------------------- */
/* Time and status utility                                                    */
/* -------------------------------------------------------------------------- */

static double now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }

    return (double)ts.tv_sec * 1000.0 +
           (double)ts.tv_nsec / 1000000.0;
}

static const char* fetch_status_string(FetchStatus status)
{
    switch (status) {
        case FETCH_PENDING:
            return "작업 대기 또는 미완료";

        case FETCH_OK:
            return "정상";

        case FETCH_CLIENT_CREATE_FAILED:
            return "HttpClient 생성 실패";

        case FETCH_REQUEST_FAILED:
            return "HTTP 요청 실패";

        case FETCH_HTTP_ERROR:
            return "HTTP 비정상 상태 코드";

        case FETCH_EMPTY_BODY:
            return "응답 본문 없음";

        case FETCH_PARSE_EMPTY:
            return "뉴스 파싱 결과 없음";

        case FETCH_RESULT_CREATE_FAILED:
            return "결과 컬렉션 생성 실패";

        default:
            return "알 수 없는 오류";
    }
}

/* -------------------------------------------------------------------------- */
/* UTF-8                                                                      */
/* -------------------------------------------------------------------------- */

static bool utf8_is_continuation(unsigned char c)
{
    return (c & 0xC0U) == 0x80U;
}

static size_t utf8_sequence_length(
    const char* src,
    size_t      remaining)
{
    unsigned char c;

    if (src == NULL || remaining == 0) {
        return 0;
    }

    c = (unsigned char)src[0];

    if (c < 0x80U) {
        return 1;
    }

    if ((c & 0xE0U) == 0xC0U) {
        if (remaining >= 2 &&
            utf8_is_continuation((unsigned char)src[1])) {
            return 2;
        }

        return 1;
    }

    if ((c & 0xF0U) == 0xE0U) {
        if (remaining >= 3 &&
            utf8_is_continuation((unsigned char)src[1]) &&
            utf8_is_continuation((unsigned char)src[2])) {
            return 3;
        }

        return 1;
    }

    if ((c & 0xF8U) == 0xF0U) {
        if (remaining >= 4 &&
            utf8_is_continuation((unsigned char)src[1]) &&
            utf8_is_continuation((unsigned char)src[2]) &&
            utf8_is_continuation((unsigned char)src[3])) {
            return 4;
        }

        return 1;
    }

    return 1;
}

static size_t utf8_encode_codepoint(
    uint32_t codepoint,
    char     output[4])
{
    if (output == NULL) {
        return 0;
    }

    if (codepoint <= 0x7FU) {
        output[0] = (char)codepoint;
        return 1;
    }

    if (codepoint <= 0x7FFU) {
        output[0] = (char)(0xC0U | (codepoint >> 6));
        output[1] = (char)(0x80U | (codepoint & 0x3FU));
        return 2;
    }

    /*
     * UTF-16 surrogate 범위는 올바른 Unicode scalar value가 아니다.
     */
    if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) {
        return 0;
    }

    if (codepoint <= 0xFFFFU) {
        output[0] = (char)(0xE0U | (codepoint >> 12));
        output[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[2] = (char)(0x80U | (codepoint & 0x3FU));
        return 3;
    }

    if (codepoint <= 0x10FFFFU) {
        output[0] = (char)(0xF0U | (codepoint >> 18));
        output[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
        output[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        output[3] = (char)(0x80U | (codepoint & 0x3FU));
        return 4;
    }

    return 0;
}

static void utf8_safe_truncate(
    char*       dst,
    size_t      dst_capacity,
    const char* src,
    size_t      max_output_bytes)
{
    size_t src_len;
    size_t read_pos  = 0;
    size_t write_pos = 0;
    size_t limit;
    bool   truncated = false;

    if (dst == NULL || dst_capacity == 0) {
        return;
    }

    dst[0] = '\0';

    if (src == NULL) {
        return;
    }

    limit = dst_capacity - 1;

    if (max_output_bytes < limit) {
        limit = max_output_bytes;
    }

    if (limit == 0) {
        return;
    }

    src_len = strlen(src);

    while (read_pos < src_len) {
        size_t sequence_len =
            utf8_sequence_length(
                src + read_pos,
                src_len - read_pos);

        size_t reserve =
            (read_pos + sequence_len < src_len && limit >= 3)
                ? 3
                : 0;

        if (sequence_len == 0 ||
            write_pos + sequence_len + reserve > limit) {
            truncated = true;
            break;
        }

        memcpy(
            dst + write_pos,
            src + read_pos,
            sequence_len);

        write_pos += sequence_len;
        read_pos  += sequence_len;
    }

    if (truncated && write_pos + 3 <= limit) {
        dst[write_pos++] = '.';
        dst[write_pos++] = '.';
        dst[write_pos++] = '.';
    }

    dst[write_pos] = '\0';
}

/* -------------------------------------------------------------------------- */
/* HTML entity decoding                                                       */
/* -------------------------------------------------------------------------- */

static bool parse_unsigned_entity(
    const char* text,
    size_t      length,
    unsigned    base,
    uint32_t*   value_out)
{
    uint32_t value = 0;

    if (text == NULL ||
        value_out == NULL ||
        length == 0) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        unsigned digit;
        unsigned char c = (unsigned char)text[i];

        if (c >= '0' && c <= '9') {
            digit = (unsigned)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = 10U + (unsigned)(c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = 10U + (unsigned)(c - 'A');
        } else {
            return false;
        }

        if (digit >= base) {
            return false;
        }

        if (value > (0x10FFFFU - digit) / base) {
            return false;
        }

        value = value * base + digit;
    }

    *value_out = value;
    return true;
}

static bool decode_named_entity(
    const char* name,
    size_t      name_len,
    char        output[4],
    size_t*     output_len)
{
    if (name == NULL ||
        output == NULL ||
        output_len == NULL) {
        return false;
    }

#define ENTITY_EQUALS(literal)                                               \
    (name_len == sizeof(literal) - 1 &&                                      \
     memcmp(name, literal, sizeof(literal) - 1) == 0)

    if (ENTITY_EQUALS("lt")) {
        output[0] = '<';
        *output_len = 1;
        return true;
    }

    if (ENTITY_EQUALS("gt")) {
        output[0] = '>';
        *output_len = 1;
        return true;
    }

    if (ENTITY_EQUALS("amp")) {
        output[0] = '&';
        *output_len = 1;
        return true;
    }

    if (ENTITY_EQUALS("quot")) {
        output[0] = '"';
        *output_len = 1;
        return true;
    }

    if (ENTITY_EQUALS("apos")) {
        output[0] = '\'';
        *output_len = 1;
        return true;
    }

    if (ENTITY_EQUALS("nbsp")) {
        output[0] = ' ';
        *output_len = 1;
        return true;
    }

#undef ENTITY_EQUALS

    return false;
}

static bool decode_numeric_entity(
    const char* body,
    size_t      body_len,
    char        output[4],
    size_t*     output_len)
{
    uint32_t codepoint;
    unsigned base = 10;
    size_t digit_start = 1;

    if (body == NULL ||
        output == NULL ||
        output_len == NULL ||
        body_len < 2 ||
        body[0] != '#') {
        return false;
    }

    if (body_len >= 3 &&
        (body[1] == 'x' || body[1] == 'X')) {
        base = 16;
        digit_start = 2;
    }

    if (digit_start >= body_len) {
        return false;
    }

    if (!parse_unsigned_entity(
            body + digit_start,
            body_len - digit_start,
            base,
            &codepoint)) {
        return false;
    }

    /*
     * HTML 숫자 엔티티의 NUL 문자는 문자열에 기록하지 않는다.
     */
    if (codepoint == 0) {
        return false;
    }

    *output_len =
        utf8_encode_codepoint(
            codepoint,
            output);

    return *output_len > 0;
}

/**
 * HTML 엔티티를 동일한 버퍼 안에서 제자리 디코딩한다.
 *
 * 엔티티 표현은 디코딩된 UTF-8 결과보다 항상 길거나 같으므로
 * write_pos는 read_pos를 추월하지 않는다.
 */
static void decode_html_entities(
    char*  str,
    size_t capacity)
{
    size_t length;
    size_t read_pos  = 0;
    size_t write_pos = 0;

    if (str == NULL || capacity == 0) {
        return;
    }

    length = strnlen(str, capacity);

    /*
     * 입력 문자열이 NUL로 종료되지 않았다면 마지막 바이트에서 종료한다.
     */
    if (length == capacity) {
        str[capacity - 1] = '\0';
        length = capacity - 1;
    }

    while (read_pos < length &&
           write_pos < capacity - 1) {
        if (str[read_pos] != '&') {
            str[write_pos++] = str[read_pos++];
            continue;
        }

        /*
         * 비정상적으로 긴 엔티티 탐색을 막기 위해 최대 길이를 제한한다.
         */
        size_t semicolon_pos = read_pos + 1;
        size_t search_end = read_pos + 16;

        if (search_end > length) {
            search_end = length;
        }

        while (semicolon_pos < search_end &&
               str[semicolon_pos] != ';' &&
               str[semicolon_pos] != '&' &&
               str[semicolon_pos] != '<' &&
               str[semicolon_pos] != '>') {
            semicolon_pos++;
        }

        if (semicolon_pos >= length ||
            str[semicolon_pos] != ';') {
            str[write_pos++] = str[read_pos++];
            continue;
        }

        const char* entity_body =
            str + read_pos + 1;

        size_t entity_body_len =
            semicolon_pos - (read_pos + 1);

        char decoded[4];
        size_t decoded_len = 0;
        bool decoded_ok;

        if (entity_body_len > 0 &&
            entity_body[0] == '#') {
            decoded_ok =
                decode_numeric_entity(
                    entity_body,
                    entity_body_len,
                    decoded,
                    &decoded_len);
        } else {
            decoded_ok =
                decode_named_entity(
                    entity_body,
                    entity_body_len,
                    decoded,
                    &decoded_len);
        }

        if (!decoded_ok ||
            write_pos + decoded_len >= capacity) {
            str[write_pos++] = str[read_pos++];
            continue;
        }

        memcpy(
            str + write_pos,
            decoded,
            decoded_len);

        write_pos += decoded_len;
        read_pos   = semicolon_pos + 1;
    }

    str[write_pos] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Lightweight HTML cleanup                                                   */
/* -------------------------------------------------------------------------- */

/**
 * 태그를 제거하고 연속된 공백을 한 칸으로 정규화한다.
 *
 * 정식 HTML parser가 아니라 뉴스 제목 내부의 단순 태그 제거용이다.
 */
static void strip_html(
    char*  str,
    size_t capacity)
{
    size_t length;
    size_t read_pos  = 0;
    size_t write_pos = 0;

    bool in_tag = false;
    bool last_was_space = true;

    if (str == NULL || capacity == 0) {
        return;
    }

    length = strnlen(str, capacity);

    if (length == capacity) {
        str[capacity - 1] = '\0';
        length = capacity - 1;
    }

    while (read_pos < length &&
           write_pos < capacity - 1) {
        unsigned char c =
            (unsigned char)str[read_pos++];

        if (c == '<') {
            in_tag = true;
            continue;
        }

        if (c == '>') {
            in_tag = false;
            continue;
        }

        if (in_tag) {
            continue;
        }

        if (isspace(c)) {
            if (!last_was_space && write_pos > 0) {
                str[write_pos++] = ' ';
                last_was_space = true;
            }

            continue;
        }

        str[write_pos++] = (char)c;
        last_was_space = false;
    }

    if (write_pos > 0 &&
        str[write_pos - 1] == ' ') {
        write_pos--;
    }

    str[write_pos] = '\0';
}

static bool is_unwanted_title(const char* title)
{
    if (title == NULL || title[0] == '\0') {
        return true;
    }

    if (strcmp(title, "동영상뉴스") == 0) {
        return true;
    }

    /*
     * HTML 구조 변경으로 CSS 속성이 제목에 섞이는 경우를 방어한다.
     */
    if (strstr(title, "style=") != NULL ||
        strstr(title, "display:") != NULL) {
        return true;
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* Naver news parser                                                          */
/* -------------------------------------------------------------------------- */

static int parse_naver_news(
    const char*    html,
    NaverNewsItem* items,
    int            max_items)
{
    int count = 0;
    const char* ptr;

    if (html == NULL ||
        items == NULL ||
        max_items <= 0) {
        return 0;
    }

    ptr = html;

    while (ptr != NULL &&
           count < max_items) {
        const char* href = strstr(
            ptr,
            "href=\"https://n.news.naver.com/");

        if (href == NULL) {
            break;
        }

        const char* url_start =
            href + strlen("href=\"");

        const char* url_end =
            strchr(url_start, '"');

        if (url_end == NULL) {
            ptr = href + 1;
            continue;
        }

        size_t url_len =
            (size_t)(url_end - url_start);

        if (url_len == 0 ||
            url_len >= sizeof(items[0].link)) {
            ptr = href + 1;
            continue;
        }

        char url[sizeof(items[0].link)];

        memcpy(
            url,
            url_start,
            url_len);

        url[url_len] = '\0';

        /*
         * URL query string 내부의 &amp;, &#x3D; 등을 복원한다.
         */
        decode_html_entities(
            url,
            sizeof(url));

        url_len = strlen(url);

        if (strstr(url, "/article/") == NULL) {
            ptr = href + 1;
            continue;
        }

        bool duplicate = false;

        for (int i = 0; i < count; i++) {
            if (strcmp(items[i].link, url) == 0) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            ptr = url_end + 1;
            continue;
        }

        const char* tag_end =
            strchr(url_end, '>');

        if (tag_end == NULL) {
            ptr = href + 1;
            continue;
        }

        const char* anchor_close =
            strstr(tag_end + 1, "</a>");

        if (anchor_close == NULL) {
            ptr = href + 1;
            continue;
        }

        size_t title_len =
            (size_t)(anchor_close - (tag_end + 1));

        if (title_len == 0 ||
            title_len >= sizeof(items[0].title)) {
            ptr = anchor_close + strlen("</a>");
            continue;
        }

        char title_raw[sizeof(items[0].title)];

        memcpy(
            title_raw,
            tag_end + 1,
            title_len);

        title_raw[title_len] = '\0';

        strip_html(
            title_raw,
            sizeof(title_raw));

        decode_html_entities(
            title_raw,
            sizeof(title_raw));

        title_len = strlen(title_raw);

        if (title_len < TITLE_MIN_BYTES ||
            title_len > TITLE_MAX_BYTES ||
            is_unwanted_title(title_raw)) {
            ptr = anchor_close + strlen("</a>");
            continue;
        }

        memcpy(
            items[count].title,
            title_raw,
            title_len + 1);

        memcpy(
            items[count].link,
            url,
            url_len + 1);

        count++;
        ptr = anchor_close + strlen("</a>");
    }

    return count;
}

/* -------------------------------------------------------------------------- */
/* Worker                                                                     */
/* -------------------------------------------------------------------------- */

static void* fetch_news(void* arg)
{
    FetchTask* task = (FetchTask*)arg;

    HttpClient* client = NULL;
    HttpClientResponse* response = NULL;
    ArrayList* section_list = NULL;

    double started_at;

    if (task == NULL) {
        return NULL;
    }

    task->status      = FETCH_PENDING;
    task->http_status = 0;
    task->item_count  = 0;
    task->elapsed_ms  = 0.0;

    started_at = now_ms();

    client = new_HttpClient(NULL);

    if (client == NULL) {
        task->status =
            FETCH_CLIENT_CREATE_FAILED;

        goto cleanup;
    }

    client->options.timeout_ms =
        NAVER_TIMEOUT_MS;

    client->setHeader(
        client,
        "User-Agent",
        CHROME_UA);

    client->setHeader(
        client,
        "Accept",
        "text/html,application/xhtml+xml,"
        "application/xml;q=0.9,*/*;q=0.8");

    client->setHeader(
        client,
        "Accept-Language",
        "ko-KR,ko;q=0.9,en-US;q=0.8");

    client->setHeader(
        client,
        "Referer",
        "https://www.naver.com/");

    response =
        client->GET(
            client,
            task->url,
            NULL);

    if (response == NULL) {
        task->status =
            FETCH_REQUEST_FAILED;

        goto cleanup;
    }

    task->http_status =
        response->status_code;

    if (response->status_code < 200 ||
        response->status_code >= 300) {
        task->status =
            FETCH_HTTP_ERROR;

        goto cleanup;
    }

    if (response->body == NULL ||
        response->body_len == 0) {
        task->status =
            FETCH_EMPTY_BODY;

        goto cleanup;
    }

    NaverNewsItem items[MAX_PER_SECTION];

    int item_count =
        parse_naver_news(
            response->body,
            items,
            MAX_PER_SECTION);

    if (item_count <= 0) {
        task->status =
            FETCH_PARSE_EMPTY;

        goto cleanup;
    }

    section_list =
        new_ArrayList(item_count);

    if (section_list == NULL) {
        task->status =
            FETCH_RESULT_CREATE_FAILED;

        goto cleanup;
    }

    for (int i = 0; i < item_count; i++) {
        char formatted[1024];
        char safe_title[400];
        char safe_link[512];

        utf8_safe_truncate(
            safe_title,
            sizeof(safe_title),
            items[i].title,
            380);

        utf8_safe_truncate(
            safe_link,
            sizeof(safe_link),
            items[i].link,
            500);

        int written =
            snprintf(
                formatted,
                sizeof(formatted),
                "제목: %s\n  링크: %s",
                safe_title,
                safe_link);

        if (written < 0) {
            continue;
        }

        String* news_object =
            new_String(formatted);

        if (news_object == NULL) {
            continue;
        }

        /*
         * ArrayList::add()가 Object를 RETAIN하는 libcore ARC 규약.
         */
        section_list->add(
            section_list,
            (Object*)news_object);

        RELEASE((Object*)news_object);
    }

    if (section_list->size <= 0) {
        task->status =
            FETCH_RESULT_CREATE_FAILED;

        goto cleanup;
    }

    pthread_mutex_lock(task->lock);

    for (int i = 0;
         i < section_list->size;
         i++) {
        String* item =
            (String*)section_list->get(
                section_list,
                i);

        if (item != NULL) {
            task->all_list->add(
                task->all_list,
                (Object*)item);
        }
    }

    /*
     * HashMap::put()이 value를 RETAIN하는 libcore ARC 규약.
     */
    task->map->put(
        task->map,
        task->name,
        (Object*)section_list);

    pthread_mutex_unlock(task->lock);

    task->item_count =
        section_list->size;

    task->status =
        FETCH_OK;

cleanup:
    task->elapsed_ms =
        now_ms() - started_at;

    if (task->elapsed_ms < 0.0) {
        task->elapsed_ms = 0.0;
    }

    if (section_list != NULL) {
        RELEASE((Object*)section_list);
    }

    if (response != NULL) {
        RELEASE((Object*)response);
    }

    if (client != NULL) {
        RELEASE((Object*)client);
    }

    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Statistics                                                                 */
/* -------------------------------------------------------------------------- */

static FetchStatistics calculate_fetch_statistics(
    const FetchTask* tasks,
    int              task_count)
{
    FetchStatistics stats;

    stats.fastest_index = -1;
    stats.slowest_index = -1;
    stats.success_count = 0;
    stats.failure_count = 0;

    stats.fastest_ms    = 0.0;
    stats.slowest_ms    = 0.0;
    stats.average_ms    = 0.0;
    stats.sequential_ms = 0.0;

    if (tasks == NULL || task_count <= 0) {
        return stats;
    }

    double total_ms = 0.0;
    int measured_count = 0;

    for (int i = 0; i < task_count; i++) {
        double elapsed_ms = tasks[i].elapsed_ms;

        if (elapsed_ms < 0.0) {
            elapsed_ms = 0.0;
        }

        stats.sequential_ms += elapsed_ms;
        total_ms            += elapsed_ms;
        measured_count++;

        if (tasks[i].status == FETCH_OK) {
            stats.success_count++;
        } else {
            stats.failure_count++;
        }

        if (stats.fastest_index < 0 ||
            elapsed_ms < stats.fastest_ms) {
            stats.fastest_index = i;
            stats.fastest_ms = elapsed_ms;
        }

        if (stats.slowest_index < 0 ||
            elapsed_ms > stats.slowest_ms) {
            stats.slowest_index = i;
            stats.slowest_ms = elapsed_ms;
        }
    }

    if (measured_count > 0) {
        stats.average_ms =
            total_ms / (double)measured_count;
    }

    return stats;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    ArrayList* all = NULL;
    HashMap* map = NULL;
    ThreadPool* pool = NULL;

    pthread_mutex_t lock;
    bool lock_initialized = false;

    FetchTask tasks[SECTION_COUNT];

    int exit_code = EXIT_FAILURE;

    double total_started_at = 0.0;
    double total_elapsed_ms = 0.0;

#ifdef SIGPIPE
    /*
     * 상대 서버가 TLS write 도중 연결을 닫더라도
     * 프로세스 전체가 SIGPIPE로 종료되지 않게 한다.
     */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        fprintf(
            stderr,
            "[WARN] SIGPIPE 무시 설정에 실패했습니다.\n");
    }
#endif

    memset(
        tasks,
        0,
        sizeof(tasks));

    printf("\n");
    printf("========================================================\n");
    printf("  libcore - 네이버 뉴스 병렬 크롤러\n");
    printf("  HttpClient(동기 블로킹 TCP / HTTPS) + Chrome UA\n");
    printf("  ThreadPool + ArrayList + HashMap\n");
    printf("  Toos IT Holdings\n");
    printf("========================================================\n\n");

    all =
        new_ArrayList(
            SECTION_COUNT * MAX_PER_SECTION);

    if (all == NULL) {
        fprintf(
            stderr,
            "[ERROR] 전체 뉴스 ArrayList 생성 실패\n");

        goto cleanup;
    }

    map = new_HashMap(16);

    if (map == NULL) {
        fprintf(
            stderr,
            "[ERROR] 섹션 HashMap 생성 실패\n");

        goto cleanup;
    }

    if (pthread_mutex_init(&lock, NULL) != 0) {
        fprintf(
            stderr,
            "[ERROR] 결과 병합 mutex 초기화 실패\n");

        goto cleanup;
    }

    lock_initialized = true;

    pool =
        new_ThreadPool(
            SECTION_COUNT,
            SECTION_COUNT * 2);

    if (pool == NULL) {
        fprintf(
            stderr,
            "[ERROR] ThreadPool 생성 실패\n");

        goto cleanup;
    }

    total_started_at = now_ms();

    printf(
        "📡 수집 시작! "
        "(%d개 섹션 병렬 · ThreadPool 블로킹 TCP)\n\n",
        SECTION_COUNT);

    for (int i = 0; i < SECTION_COUNT; i++) {
        tasks[i].name         = sections[i].name;
        tasks[i].url          = sections[i].url;
        tasks[i].all_list     = all;
        tasks[i].map          = map;
        tasks[i].lock         = &lock;
        tasks[i].elapsed_ms   = 0.0;
        tasks[i].status       = FETCH_PENDING;
        tasks[i].http_status  = 0;
        tasks[i].item_count   = 0;

        pool->submit(
            pool,
            fetch_news,
            &tasks[i]);

        printf(
            "  → [%s] %s\n",
            sections[i].name,
            sections[i].url);
    }

    printf("\n⏳ 병렬 수집 중...\n\n");

    /*
     * 제출된 모든 작업이 종료될 때까지 기다린다.
     */
    pool->shutdown(pool);

    total_elapsed_ms =
        now_ms() - total_started_at;

    if (total_elapsed_ms < 0.0) {
        total_elapsed_ms = 0.0;
    }

    printf("============================================\n");
    printf("  📰 섹션별 뉴스\n");
    printf("============================================\n");

    for (int i = 0; i < SECTION_COUNT; i++) {
        ArrayList* section_list =
            (ArrayList*)map->get(
                map,
                sections[i].name);

        if (tasks[i].status != FETCH_OK ||
            section_list == NULL) {
            printf(
                "\n🔹 [%s] 수집 실패 | %.0fms\n",
                sections[i].name,
                tasks[i].elapsed_ms);

            printf(
                "     원인: %s",
                fetch_status_string(tasks[i].status));

            if (tasks[i].status == FETCH_HTTP_ERROR) {
                printf(
                    " (HTTP %d)",
                    tasks[i].http_status);
            }

            printf("\n");
            continue;
        }

        printf(
            "\n🔹 [%s] %d건 | %.0fms\n",
            sections[i].name,
            section_list->size,
            tasks[i].elapsed_ms);

        printf(
            "  ──────────────────────────────────────\n");

        for (int j = 0;
             j < section_list->size;
             j++) {
            String* item =
                (String*)section_list->get(
                    section_list,
                    j);

            if (item == NULL ||
                item->value == NULL) {
                continue;
            }

            printf(
                "\n  [%02d] %s\n",
                j + 1,
                item->value);
        }

        printf(
            "  ──────────────────────────────────────\n");
    }

    FetchStatistics stats =
        calculate_fetch_statistics(
            tasks,
            SECTION_COUNT);

    double time_saved_ms =
        stats.sequential_ms - total_elapsed_ms;

    if (time_saved_ms < 0.0) {
        time_saved_ms = 0.0;
    }

    double speedup =
        total_elapsed_ms > 0.0
            ? stats.sequential_ms / total_elapsed_ms
            : 0.0;

    double efficiency =
        SECTION_COUNT > 0
            ? speedup / (double)SECTION_COUNT * 100.0
            : 0.0;

    double work_spread_ms =
        stats.slowest_ms - stats.fastest_ms;

    if (work_spread_ms < 0.0) {
        work_spread_ms = 0.0;
    }

    double slowest_share =
        total_elapsed_ms > 0.0
            ? stats.slowest_ms / total_elapsed_ms * 100.0
            : 0.0;

    printf("\n");
    printf("========================================================\n");
    printf("  ✅ 수집 완료!\n");
    printf("  총 뉴스: %d건\n", all->size);
    printf(
        "  성공 섹션: %d / %d",
        stats.success_count,
        SECTION_COUNT);

    if (stats.failure_count > 0) {
        printf(
            " · 실패: %d",
            stats.failure_count);
    }

    printf("\n");

    printf("  ────────────────────────────────────────────────────────\n");
    printf(
        "  Sequential (Est): %.0fms\n",
        stats.sequential_ms);

    printf(
        "  Parallel   (Act): %.0fms\n",
        total_elapsed_ms);

    printf(
        "  Time Saved:       %.0fms\n",
        time_saved_ms);

    printf(
        "  Speedup:          %.2fx\n",
        speedup);

    printf(
        "  Efficiency:       %.1f%% (%d workers)\n",
        efficiency,
        SECTION_COUNT);

    printf("  ────────────────────────────────────────────────────────\n");

    if (stats.fastest_index >= 0) {
        printf(
            "  Fastest Job:      %s (%.0fms)\n",
            tasks[stats.fastest_index].name,
            stats.fastest_ms);
    }

    printf(
        "  Average Job:      %.0fms\n",
        stats.average_ms);

    if (stats.slowest_index >= 0) {
        printf(
            "  Slowest Job:      %s (%.0fms)\n",
            tasks[stats.slowest_index].name,
            stats.slowest_ms);
    }

    printf(
        "  Work Spread:      %.0fms\n",
        work_spread_ms);

    printf(
        "  Slowest Share:    %.1f%% of parallel runtime\n",
        slowest_share);

    printf("  ────────────────────────────────────────────────────────\n");
    printf("  HttpClient(동기 블로킹 TCP / HTTPS) · Chrome UA\n");
    printf("  ThreadPool · ArrayList · HashMap\n");
    printf("========================================================\n\n");

    exit_code = EXIT_SUCCESS;

cleanup:
    /*
     * ThreadPool은 shutdown 완료 후 마지막으로 소유권을 해제한다.
     */
    if (pool != NULL) {
        RELEASE((Object*)pool);
    }

    if (all != NULL) {
        RELEASE((Object*)all);
    }

    if (map != NULL) {
        RELEASE((Object*)map);
    }

    if (lock_initialized) {
        pthread_mutex_destroy(&lock);
    }

    if (exit_code == EXIT_SUCCESS) {
        printf(
            "[TOOS-IT] "
            "Naver news crawler shutdown complete.\n");
    } else {
        fprintf(
            stderr,
            "[TOOS-IT] "
            "Naver news crawler terminated with error.\n");
    }

    return exit_code;
}
