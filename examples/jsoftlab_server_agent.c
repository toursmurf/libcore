/*
 * jsoftlab_server_agent.c
 * NMS 원격 서버 감시 에이전트 [Official v1.7.0 - Full API Build]
 * 투스IT 홀딩스 / JSoftLab 2026.07.15
 */

#include "http_server.h"
#include "event_loop.h"
#include "router.h"
#include "json.h"
#include "string_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>
#include "crypto.h"

#define AGENT_PORT 25580
#define MAX_PROCS 1024

static EventLoop* g_loop = NULL;
static HttpServer* g_server = NULL;

static unsigned long long g_last_rx = 0;
static unsigned long long g_last_tx = 0;
static double g_last_net_time = 0.0;

typedef struct {
    int pid;
    char user[64];
    double cpu;
    double mem;
    char status[16];
    char name[256];
    char cmd[1024];
} ProcInfo;

typedef struct {
    char name[256];
    int count;
} PGroup;

/* 전방 선언 */
static void get_current_timestamp(char* buf, size_t size);
static void format_time_str(double elapsed_ms, char* buf, size_t size);

/* ✨ 타임스탬프 공통 유틸리티 함수 추가 ✨ */
static void get_current_timestamp(char* buf, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%S+09:00", tm_info);
}

static int compare_pgroups(const void* a, const void* b) {
    PGroup* p1 = (PGroup*)a;
    PGroup* p2 = (PGroup*)b;
    return p2->count - p1->count;
}

/* 🚨 [패치] strncpy -> snprintf 로 교체하여 -Wstringop-truncation 경고 원천 차단 */
static void clean_process_name(char* name, const char* cmd_full) {
    if (cmd_full[0] == '[') {
        snprintf(name, 256, "%s", cmd_full);
    } else {
        char* base = strrchr(cmd_full, '/');
        char* token = base ? base + 1 : (char*)cmd_full;
        char* sp = strchr(token, ' ');
        if (sp) *sp = '\0';
        snprintf(name, 256, "%s", token);
    }
}


/* =========================================================
 * 설정 파일 구조
 * /etc/jsoftlab-agent/agent.conf
 *
 * [agent]
 * agent_id = 6d7b-xxxx-xxxx-xxxx
 * server   = https://nms.toos.it
 *
 * [auth]
 * api_key  = ENC(AES256:base64...)
 *
 * [logs]
 * allowed_dirs = httpd,mariadb,postgres,nginx
 *
 * [server]
 * port = 25580
 * ========================================================= */

#define DEFAULT_CONF_PATH "/etc/jsoftlab-agent/agent.conf"
#define DEFAULT_KEY_PATH  "/etc/jsoftlab-agent/.key"

static char g_agent_id[128] = "";
static char g_api_key[512]  = "";
static char g_allowed_dirs[512] = "httpd,nginx,mariadb,postgres,php-fpm";

/* 설정 파일 로드 */
static int load_config(const char* conf_path, const char* key_path) {
    (void)key_path; /* 추후 AES 복호화 시 사용 */

    FILE* fp = fopen(conf_path, "r");
    if (!fp) {
        printf("⚠️  설정 파일 없음: %s (기본값 사용)\n", conf_path);
        return 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char* hash = strchr(line, '#'); if (hash) *hash = '\0';
        if (strlen(line) == 0) continue;

        char key[128], val[384];
        /* 섹션 헤더 [agent] [auth] 등 건너뜀 */
        if (line[0] == '[') continue;

        if (sscanf(line, " %127[^=] = %383[^\n]", key, val) == 2) {
            /* 앞뒤 공백 제거 */
            char* k = key; while (*k == ' ') k++;
            char* v = val; while (*v == ' ') v++;
            /* 값 뒤 공백 제거 */
            char* ve = v + strlen(v) - 1;
            while (ve > v && (*ve == ' ' || *ve == '\t')) *ve-- = '\0';

            if (strcmp(k, "agent_id") == 0)
                snprintf(g_agent_id, sizeof(g_agent_id), "%.*s", (int)sizeof(g_agent_id)-1, v);
            else if (strcmp(k, "api_key") == 0)
                snprintf(g_api_key, sizeof(g_api_key), "%.*s", (int)sizeof(g_api_key)-1, v);
            /* port는 고정값 AGENT_PORT 사용 — 설정 파일에서 변경 불가 */
            else if (strcmp(k, "allowed_dirs") == 0)
                snprintf(g_allowed_dirs, sizeof(g_allowed_dirs), "%s", v);
        }
    }
    fclose(fp);



    printf("✅ 설정 로드: %s\n", conf_path);
    if (strlen(g_agent_id) > 0)
        printf("   Agent ID: %s\n", g_agent_id);
    return 1;
}


/* 전방 선언 */
static int key_store_exists(void);
static int verify_key_hash(const char* api_key);

/* ── Bearer 토큰 검증 ───────────────────────────────── */
/* 반환값: 1=인증OK, 0=토큰불일치, -1=키미설정 */
static int check_bearer(HttpRequest* req) {
    /* .key_store 없으면 키 미설정 */
    if (!key_store_exists()) return -1;
    if (!req->headers) return 0;
    const char* auth = hashmap_get_str(req->headers, "authorization");
    if (!auth || strncmp(auth, "Bearer ", 7) != 0) return 0;
    /* SHA-256 해시 비교 */
    return verify_key_hash(auth + 7);
}

/* 인증 체크 매크로 */
#define AUTH_CHECK(req, res) \
    do { \
        int _r = check_bearer(req); \
        if (_r == -1) { \
            Router_sendError(res, 401, "No_API_Key", \
                "API 키가 설정되지 않았습니다. POST /register 로 키를 등록하세요."); \
            return; \
        } \
        if (_r == 0) { \
            Router_sendError(res, 401, "Unauthorized", \
                "유효한 Bearer 토큰이 필요합니다."); \
            return; \
        } \
    } while(0)

static void handle_signal(int sig) {
    printf("\n🛑 [SIG %d] Received termination signal. Initiating graceful shutdown...\n", sig);

    if (g_server) {
        RELEASE((Object*)g_server);
        g_server = NULL;
    }
    if (g_loop) {
        RELEASE((Object*)g_loop);
        g_loop = NULL;
    }

    printf("✅ Memory cleanup complete. Agent terminated safely.\n");
    exit(0);
}

static void format_time_str(double elapsed_ms, char* buf, size_t size) {
    double sec = elapsed_ms / 1000.0;

    if (sec < 60.0) {
        snprintf(buf, size, "%.2f sec", sec);
    } else if (sec < 3600.0) {
        snprintf(buf, size, "%.2f min", sec / 60.0);
    } else {
        snprintf(buf, size, "%.2f hr", sec / 3600.0);
    }
}

static int collect_processes(ProcInfo* procs, int max, const char* filter) {
    FILE* fp = popen("ps aux --no-headers ", "r");
    if (!fp) return 0;

    int count = 0;
    char line[2048];
    while (fgets(line, sizeof(line), fp) && count < max) {
        ProcInfo p = {0};
        char cmd_full[2048] = {0};

        if (sscanf(line, "%63s %d %lf %lf %*s %*s %*s %15s %*s %*s %1023[^\n]",
                   p.user, &p.pid, &p.cpu, &p.mem, p.status, cmd_full) < 5) {
            continue;
        }

        clean_process_name(p.name, cmd_full);

        /* 🚨 [패치] gcc 경고 지점: snprintf로 교체 */
        snprintf(p.cmd, sizeof(p.cmd), "%s", cmd_full);

        if (filter && *filter) {
            if (!strstr(p.name, filter) && !strstr(p.cmd, filter)) {
                continue;
            }
        }
        procs[count++] = p;
    }
    pclose(fp);
    return count;
}

static char* build_process_json(ProcInfo* procs, int count, struct timeval* start_t) {
    JSONNode* root = new_JSON_Object();
    if (!root) return NULL;

    char hostname[128] = {0};
    gethostname(hostname, sizeof(hostname));
    JSONNode* jv = new_JSON_String(hostname);
    if (jv) {
        root->put(root, "hostname", (Object*)jv);
        RELEASE((Object*)jv);
    }

    char ts[32];
    get_current_timestamp(ts, sizeof(ts));

    jv = new_JSON_String(ts);
    if (jv) {
        root->put(root, "timestamp", (Object*)jv);
        RELEASE((Object*)jv);
    }

    JSONNode* jcount = (JSONNode*)new_json_number(count);
    if (jcount) {
        root->put(root, "count", (Object*)jcount);
        RELEASE((Object*)jcount);
    }

    PGroup pg[MAX_PROCS];
    int pg_count = 0;
    for (int i = 0; i < count; i++) {
        int found = 0;
        for (int j = 0; j < pg_count; j++) {
            if (strcmp(pg[j].name, procs[i].name) == 0) {
                pg[j].count++;
                found = 1;
                break;
            }
        }
        if (!found && pg_count < MAX_PROCS) {
            /* 🚨 [패치] strncpy -> snprintf 로 교체 */
            snprintf(pg[pg_count].name, sizeof(pg[pg_count].name), "%s", procs[i].name);
            pg[pg_count].count = 1;
            pg_count++;
        }
    }
    qsort(pg, pg_count, sizeof(PGroup), compare_pgroups);

    JSONNode* group_arr = new_JSON_Array();
    if (group_arr) {
        for (int i = 0; i < pg_count; i++) {
            JSONNode* item = new_JSON_Object();
            JSONNode* jname = new_JSON_String(pg[i].name);
            char cnt_buf[16];
            snprintf(cnt_buf, sizeof(cnt_buf), "%d", pg[i].count);
            JSONNode* jcnt = new_JSON_String(cnt_buf);

            if (item && jname && jcnt) {
                item->put(item, "name", (Object*)jname);
                item->put(item, "count", (Object*)jcnt);
                group_arr->add(group_arr, (Object*)item);
            }
            if (jname) RELEASE((Object*)jname);
            if (jcnt) RELEASE((Object*)jcnt);
            if (item) RELEASE((Object*)item);
        }
        root->put(root, "group_counts", (Object*)group_arr);
        RELEASE((Object*)group_arr);
    }

    JSONNode* arr = new_JSON_Array();
    if (!arr) {
        RELEASE((Object*)root);
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        JSONNode* obj = new_JSON_Object();
        if (!obj) continue;

        char pid_str[32]; snprintf(pid_str, sizeof(pid_str), "%d", procs[i].pid);
        char cpu_str[16]; snprintf(cpu_str, sizeof(cpu_str), "%.1f", procs[i].cpu);
        char mem_str[16]; snprintf(mem_str, sizeof(mem_str), "%.1f", procs[i].mem);

        JSONNode* jpid = new_JSON_String(pid_str);
        JSONNode* jcpu = new_JSON_String(cpu_str);
        JSONNode* jmem = new_JSON_String(mem_str);
        JSONNode* jstatus = new_JSON_String(procs[i].status);
        JSONNode* juser = new_JSON_String(procs[i].user);
        JSONNode* jname = new_JSON_String(procs[i].name);

        if (jpid) { obj->put(obj, "pid", (Object*)jpid); RELEASE((Object*)jpid); }
        if (juser) { obj->put(obj, "user", (Object*)juser); RELEASE((Object*)juser); }
        if (jcpu) { obj->put(obj, "cpu", (Object*)jcpu); RELEASE((Object*)jcpu); }
        if (jmem) { obj->put(obj, "mem", (Object*)jmem); RELEASE((Object*)jmem); }
        if (jstatus) { obj->put(obj, "status", (Object*)jstatus); RELEASE((Object*)jstatus); }
        if (jname) { obj->put(obj, "name", (Object*)jname); RELEASE((Object*)jname); }

        arr->add(arr, (Object*)obj);
        RELEASE((Object*)obj);
    }
    root->put(root, "processes", (Object*)arr);
    RELEASE((Object*)arr);

    struct timeval end_t;
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t->tv_sec) * 1000.0 + (end_t.tv_usec - start_t->tv_usec) / 1000.0;
    char elapsed_str[32];
    format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));
    JSONNode* jexec = new_JSON_String(elapsed_str);

    if (jexec) {
        root->put(root, "exec_time", (Object*)jexec);
        RELEASE((Object*)jexec);
    }

    char* json_str = root->toString(root);
    RELEASE((Object*)root);
    return json_str;
}

/* ── HTTP 핸들러 ─────────────────────────── */
static void handler_status(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;

    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char hostname[128] = {0};
    gethostname(hostname, sizeof(hostname));

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    FILE* lf = fopen("/proc/loadavg", "r");

    if (lf) {
        if (fscanf(lf, "%lf %lf %lf", &load1, &load5, &load15) != 3) {
            load1 = 0.0;
            load5 = 0.0;
            load15 = 0.0;
        }
        fclose(lf);
    }

    long total_mb = 0, available_mb = 0, swap_total_mb = 0, swap_free_mb = 0, swap_used_mb = 0;
    FILE* mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[128];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %ld kB", &total_mb)) total_mb /= 1024;
            else if (sscanf(line, "MemAvailable: %ld kB", &available_mb)) available_mb /= 1024;
            else if (sscanf(line, "SwapTotal: %ld kB", &swap_total_mb)) swap_total_mb /= 1024;
            else if (sscanf(line, "SwapFree: %ld kB", &swap_free_mb)) swap_free_mb /= 1024;
        }
        fclose(mf);
    }
    long used_mb = total_mb - available_mb;
    swap_used_mb = swap_total_mb - swap_free_mb;
    double mem_percent = (total_mb > 0) ? ((double)used_mb * 100.0 / total_mb) : 0.0;

    struct statvfs st;
    statvfs("/", &st);
    double disk_total_gb = (double)st.f_blocks * st.f_frsize / 1024.0 / 1024.0 / 1024.0;
    double disk_available_gb = (double)st.f_bavail * st.f_frsize / 1024.0 / 1024.0 / 1024.0;
    double disk_used_gb = disk_total_gb - disk_available_gb;
    double disk_percent = (disk_total_gb > 0) ? (disk_used_gb * 100.0 / disk_total_gb) : 0.0;

    long core_count = sysconf(_SC_NPROCESSORS_ONLN);

    double cpu_total = 0.0;
    int proc_count = 0;
    int zombie_count = 0;

    FILE* fp = popen("ps aux --no-headers | awk '{cpu+=$3; count++; if($8 ~ /^Z/) z++} END {print cpu, count, z+0}'", "r");
    if (fp) {
        if (fscanf(fp, "%lf %d %d", &cpu_total, &proc_count, &zombie_count) != 3) {
            cpu_total = 0.0;
            proc_count = 0;
            zombie_count = 0;
        }
        pclose(fp);
    }

    char cpu_model[128] = "Unknown";
    FILE* cf = fopen("/proc/cpuinfo", "r");
    if (cf) {
        char cline[256];
        while (fgets(cline, sizeof(cline), cf)) {
            if (strncmp(cline, "model name", 10) == 0 || strncmp(cline, "Hardware", 8) == 0) {
                char* colon = strchr(cline, ':');
                if (colon) {
                    char* p = colon + 1;
                    while (*p == ' ' || *p == '\t') p++;
                    char* nl = strchr(p, '\n');
                    if (nl) *nl = '\0';
                    /* 🚨 [패치] strncpy -> snprintf 교체 */
                    snprintf(cpu_model, sizeof(cpu_model), "%s", p);
                    break;
                }
            }
        }
        fclose(cf);
    }

    unsigned long long total_rx = 0, total_tx = 0;
    FILE* net_fp = fopen("/proc/net/dev", "r");
    if (net_fp) {
        char nline[256];
        if (fgets(nline, sizeof(nline), net_fp)) { /* skip 헤더 1 */ }
        if (fgets(nline, sizeof(nline), net_fp)) { /* skip 헤더 2 */ }

        while (fgets(nline, sizeof(nline), net_fp)) {
            char iface[32];
            unsigned long long rx, tx;
            char* colon = strchr(nline, ':');
            if (colon) {
                *colon = ' ';
                if (sscanf(nline, "%31s %llu %*u %*u %*u %*u %*u %*u %*u %llu", iface, &rx, &tx) == 3) {
                    if (strcmp(iface, "lo") != 0) {
                        total_rx += rx;
                        total_tx += tx;
                    }
                }
            }
        }
        fclose(net_fp);
    }

    double current_time = start_t.tv_sec + start_t.tv_usec / 1000000.0;
    long long rx_bps = 0, tx_bps = 0;
    if (g_last_net_time > 0.0) {
        double delta_t = current_time - g_last_net_time;
        if (delta_t > 0.0) {
            rx_bps = (long long)(((total_rx - g_last_rx) * 8.0) / delta_t);
            tx_bps = (long long)(((total_tx - g_last_tx) * 8.0) / delta_t);
        }
    }
    g_last_rx = total_rx;
    g_last_tx = total_tx;
    g_last_net_time = current_time;


    int health_score = 100;
    char alerts_json[1024];
    int alert_offset = snprintf(alerts_json, sizeof(alerts_json), "[");
    int alert_count = 0;

    double cpu_avg = (core_count > 0) ? (cpu_total / core_count) : 0.0;
    if (cpu_avg > 90.0) {
        health_score -= 10;
        if (alert_count > 0) alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, ",");
        alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "{\"type\":\"CPU_CRITICAL\",\"msg\":\"CPU usage > 90%%\"}");
        alert_count++;
    }
    if (mem_percent > 90.0) {
        health_score -= 10;
        if (alert_count > 0) alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, ",");
        alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "{\"type\":\"MEM_CRITICAL\",\"msg\":\"Memory usage > 90%%\"}");
        alert_count++;
    }
    if (disk_percent > 95.0) {
        health_score -= 15;
        if (alert_count > 0) alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, ",");
        alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "{\"type\":\"DISK_CRITICAL\",\"msg\":\"Disk usage > 95%%\"}");
        alert_count++;
    }
    if (load1 > core_count) {
        health_score -= 5;
        if (alert_count > 0) alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, ",");
        alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "{\"type\":\"LOAD_WARN\",\"msg\":\"Load1 exceeds core count\"}");
        alert_count++;
    }
    double swap_pct = (swap_total_mb > 0) ? ((double)swap_used_mb * 100.0 / swap_total_mb) : 0.0;
    if (swap_pct > 80.0) {
        health_score -= 5;
        if (alert_count > 0) alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, ",");
        alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "{\"type\":\"SWAP_WARN\",\"msg\":\"Swap usage > 80%%\"}");
        alert_count++;
    }
    if (zombie_count > 0) {
        health_score -= 5;
        if (alert_count > 0) alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, ",");
        alert_offset += snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "{\"type\":\"ZOMBIE_WARN\",\"msg\":\"Zombie process detected (%d)\"}", zombie_count);
        alert_count++;
    }

    snprintf(alerts_json + alert_offset, sizeof(alerts_json) - alert_offset, "]");

    if (health_score < 0) {
        health_score = 0;
    }
    if (health_score > 100) {
        health_score = 100;
    }

    char ts[32];
    get_current_timestamp(ts, sizeof(ts));

    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32];
    format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    char resp[2560];
    snprintf(resp, sizeof(resp),
             "{\"hostname\":\"%s\",\"health_score\":%d,\"alerts\":%s,\"timestamp\":\"%s\",\"exec_time\":\"%s\",\"load_avg\":{\"1m\":%.2f,\"5m\":%.2f,\"15m\":%.2f},\"cpu\":{\"model\":\"%s\",\"total\":%.1f,\"core_count\":%ld},\"memory\":{\"total_mb\":%ld,\"used_mb\":%ld,\"available_mb\":%ld,\"swap_total_mb\":%ld,\"swap_used_mb\":%ld,\"percent\":%.1f},\"disk\":{\"total_gb\":%.0f,\"used_gb\":%.0f,\"available_gb\":%.0f,\"percent\":%.1f},\"network\":{\"rx_bps\":%lld,\"tx_bps\":%lld},\"proc_count\":%d,\"zombie_count\":%d}",
             hostname, health_score, alerts_json, ts, elapsed_str, load1, load5, load15, cpu_model, cpu_total, core_count, total_mb, used_mb, available_mb, swap_total_mb, swap_used_mb, mem_percent, disk_total_gb, disk_used_gb, disk_available_gb, disk_percent, rx_bps, tx_bps, proc_count, zombie_count);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, resp);
}

static void handler_traffic(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;

    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[4096];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"interfaces\":[");
    int count = 0;

    FILE* fp = fopen("/proc/net/dev", "r");
    if (fp) {
        char line[512];

        if (fgets(line, sizeof(line), fp)) { /* skip header 1 */ }
        if (fgets(line, sizeof(line), fp)) { /* skip header 2 */ }

        while (fgets(line, sizeof(line), fp)) {
            char iface[32];
            unsigned long long rx_bytes, rx_packets, tx_bytes, tx_packets;
            char* colon = strchr(line, ':');
            if (colon) {
                *colon = ' ';
                if (sscanf(line, "%31s %llu %llu %*u %*u %*u %*u %*u %*u %llu %llu",
                           iface, &rx_bytes, &rx_packets, &tx_bytes, &tx_packets) >= 5) {
                    if (count > 0) {
                        offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                    }
                    offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                                       "{\"name\":\"%s\",\"rx_bytes\":%llu,\"rx_packets\":%llu,\"tx_bytes\":%llu,\"tx_packets\":%llu}",
                                       iface, rx_bytes, rx_packets, tx_bytes, tx_packets);
                    count++;
                }
            }
        }
        fclose(fp);
    }

    /* ✨ 공통 유틸리티를 활용하여 타임스탬프 획득 ✨ */
    char ts[32];
    get_current_timestamp(ts, sizeof(ts));

    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32];
    format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset, "],\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

static void handler_processes(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;

    struct timeval start_t;
    gettimeofday(&start_t, NULL);

    const char* filter = req->query ? hashmap_get_str(req->query, "name") : NULL;
    ProcInfo* procs = calloc(MAX_PROCS, sizeof(ProcInfo));
    if (!procs) {
        Router_sendError(res, 500, "Internal_Server_Error", "프로세스 조회를 위한 시스템 메모리 할당에 실패했습니다.");
        return;
    }

    int count = collect_processes(procs, MAX_PROCS, filter);
    char* json = build_process_json(procs, count, &start_t);
    free(procs);

    if (!json) {
        Router_sendError(res, 500, "Internal_Server_Error", "프로세스 목록 JSON 데이터 생성에 실패했습니다.");
        return;
    }

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json);
    free(json);
}

static void handler_process_by_pid(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;

    struct timeval start_t;
    gettimeofday(&start_t, NULL);

    const char* path = req->path ? req->path->c_str(req->path) : "/";
    int pid = 0;
    if (sscanf(path, "/processes/%d", &pid) != 1) {
        Router_sendError(res, 404, "Invalid_PID_Format", "요청하신 프로세스 ID가 유효한 숫자 형식이 아닙니다.");
        return;
    }

    char cmd[1024];
    memset(cmd, 0x00, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ps aux --no-headers | grep -w %d | grep -v grep 2>/dev/null", pid);

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        Router_sendError(res, 500, "Command_Execution_Failed", "시스템 내부 명령어(popen) 실행에 실패했습니다.");
        return;
    }

    ProcInfo p = {0};
    char line[2048] = {0};
    char found = 0;

    if (fgets(line, sizeof(line), fp)) {
        char cmd_full[1024] = {0};
        if (sscanf(line, "%63s %d %lf %lf %*s %*s %*s %15s %*s %*s %1023[^\n]",
                   p.user, &p.pid, &p.cpu, &p.mem, p.status, cmd_full) >= 5) {
            clean_process_name(p.name, cmd_full);

            /* 🚨 [패치] gcc 경고 지점: snprintf로 교체 */
            snprintf(p.cmd, sizeof(p.cmd), "%s", cmd_full);
            found = 1;
        }
    }
    pclose(fp);

    if (!found) {
        Router_sendError(res, 404, "Process_Not_Found", "요청하신 ID의 프로세스를 시스템에서 찾을 수 없거나 이미 종료되었습니다.");
        return;
    }

    char* json = build_process_json(&p, 1, &start_t);
    if (!json) {
        Router_sendError(res, 500, "Internal_Server_Error", "프로세스 상세 정보 JSON 데이터 생성에 실패했습니다.");
        return;
    }

    res->setHeader(res, "Content-Type", "application/json");
    res->sendText(res, json);
    free(json);
}

/* ── /metrics: CPU·메모리만 분리 ────────────────── */
static void handler_metrics(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    long total_mb = 0, available_mb = 0, swap_total_mb = 0, swap_free_mb = 0;
    FILE* mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[128];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %ld kB", &total_mb)) total_mb /= 1024;
            else if (sscanf(line, "MemAvailable: %ld kB", &available_mb)) available_mb /= 1024;
            else if (sscanf(line, "SwapTotal: %ld kB", &swap_total_mb)) swap_total_mb /= 1024;
            else if (sscanf(line, "SwapFree: %ld kB", &swap_free_mb)) swap_free_mb /= 1024;
        }
        fclose(mf);
    }
    long used_mb = total_mb - available_mb;
    long swap_used_mb = swap_total_mb - swap_free_mb;
    double mem_percent = (total_mb > 0) ? ((double)used_mb * 100.0 / total_mb) : 0.0;

    long core_count = sysconf(_SC_NPROCESSORS_ONLN);
    double cpu_total = 0.0;
    int proc_count = 0, zombie_count = 0;
    FILE* fp = popen("ps aux --no-headers | awk '{cpu+=$3; count++; if($8 ~ /^Z/) z++} END {print cpu, count, z+0}'", "r");
    if (fp) {
        if (fscanf(fp, "%lf %d %d", &cpu_total, &proc_count, &zombie_count) != 3) {
            cpu_total = 0.0; proc_count = 0; zombie_count = 0;
        }
        pclose(fp);
    }
    double cpu_avg = (core_count > 0) ? (cpu_total / core_count) : 0.0;

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    char resp[512];
    snprintf(resp, sizeof(resp),
        "{\"timestamp\":\"%s\",\"exec_time\":\"%s\","
        "\"cpu\":{\"total\":%.1f,\"avg\":%.1f,\"core_count\":%ld},"
        "\"memory\":{\"total_mb\":%ld,\"used_mb\":%ld,\"available_mb\":%ld,"
        "\"swap_total_mb\":%ld,\"swap_used_mb\":%ld,\"percent\":%.1f},"
        "\"proc_count\":%d,\"zombie_count\":%d}",
        ts, elapsed_str, cpu_total, cpu_avg, core_count,
        total_mb, used_mb, available_mb, swap_total_mb, swap_used_mb, mem_percent,
        proc_count, zombie_count);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, resp);
}

/* ── /disk: 디스크 마운트 포인트 목록 ──────────────── */
static void handler_disk(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[4096];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"disks\":[");
    int count = 0;

    FILE* fp = popen("df -BG --output=source,target,size,used,avail,pcent 2>/dev/null | tail -n +2", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            char src[128], mnt[128], size[32], used[32], avail[32], pct[16];
            if (sscanf(line, "%127s %127s %31s %31s %31s %15s", src, mnt, size, used, avail, pct) == 6) {
                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"source\":\"%s\",\"mount\":\"%s\",\"size\":\"%s\","
                    "\"used\":\"%s\",\"avail\":\"%s\",\"percent\":\"%s\"}",
                    src, mnt, size, used, avail, pct);
                count++;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /ports: 열린 포트 목록 ────────────────────────── */
static void handler_ports(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[8192];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"ports\":[");
    int count = 0;

    FILE* fp = popen("ss -tlnp 2>/dev/null | tail -n +2", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            char state[32], local[128], peer[128], proc[256];
            proc[0] = '\0';
            if (sscanf(line, "%31s %*s %*s %127s %127s %255[^\n]", state, local, peer, proc) >= 3) {
                /* 포트 추출 */
                char* colon = strrchr(local, ':');
                char port[16] = "0";
                if (colon) snprintf(port, sizeof(port), "%s", colon + 1);

                /* 프로세스명 추출 */
                char pname[128] = "";
                char* np = strstr(proc, "users:((\"");
                if (np) {
                    np += 9;
                    char* end = strchr(np, '"');
                    if (end) { *end = '\0'; snprintf(pname, sizeof(pname), "%s", np); }
                }

                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"state\":\"%s\",\"local\":\"%s\",\"port\":\"%s\",\"process\":\"%s\"}",
                    state, local, port, pname);
                count++;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /traffic/:iface: 인터페이스별 트래픽 ──────────── */
static void handler_traffic_iface(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    const char* path = req->path ? req->path->c_str(req->path) : "/";
    char iface_target[64] = "";
    sscanf(path, "/traffic/%63s", iface_target);

    char json_buf[1024];
    int found = 0;

    FILE* fp = fopen("/proc/net/dev", "r");
    if (fp) {
        char line[512];
        if (fgets(line, sizeof(line), fp)) { /* skip header 1 */ }
        if (fgets(line, sizeof(line), fp)) { /* skip header 2 */ }
        while (fgets(line, sizeof(line), fp)) {
            char iface[32];
            unsigned long long rx_bytes, rx_packets, tx_bytes, tx_packets;
            char* colon = strchr(line, ':');
            if (colon) {
                *colon = ' ';
                if (sscanf(line, "%31s %llu %llu %*u %*u %*u %*u %*u %*u %llu %llu",
                           iface, &rx_bytes, &rx_packets, &tx_bytes, &tx_packets) >= 5) {
                    /* 앞뒤 공백 제거 */
                    char* p = iface;
                    while (*p == ' ') p++;
                    if (strcmp(p, iface_target) == 0) {
                        char ts[32]; get_current_timestamp(ts, sizeof(ts));
                        gettimeofday(&end_t, NULL);
                        double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
                        char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));
                        snprintf(json_buf, sizeof(json_buf),
                            "{\"iface\":\"%s\",\"rx_bytes\":%llu,\"rx_packets\":%llu,"
                            "\"tx_bytes\":%llu,\"tx_packets\":%llu,"
                            "\"timestamp\":\"%s\",\"exec_time\":\"%s\"}",
                            p, rx_bytes, rx_packets, tx_bytes, tx_packets, ts, elapsed_str);
                        found = 1;
                        break;
                    }
                }
            }
        }
        fclose(fp);
    }

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    if (!found) {
        Router_sendError(res, 404, "Interface_Not_Found", "요청한 네트워크 인터페이스를 찾을 수 없습니다.");
    } else {
        res->sendText(res, json_buf);
    }
}

/* ── /connections: TCP 연결 상태 ───────────────────── */
static void handler_connections(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[8192];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"connections\":[");
    int count = 0;

    /* 상태별 집계 */
    int established=0, time_wait=0, close_wait=0, listen=0, other=0;

    FILE* fp = popen("ss -tnp 2>/dev/null | tail -n +2", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            char state[32], local[128], peer[128], proc[256];
            proc[0] = '\0';
            if (sscanf(line, "%31s %*s %*s %127s %127s %255[^\n]", state, local, peer, proc) >= 3) {
                char pname[128] = "";
                char* np = strstr(proc, "users:((\"");
                if (np) {
                    np += 9;
                    char* end = strchr(np, '"');
                    if (end) { *end = '\0'; snprintf(pname, sizeof(pname), "%s", np); }
                }

                if (strcmp(state, "ESTAB") == 0) established++;
                else if (strcmp(state, "TIME-WAIT") == 0) time_wait++;
                else if (strcmp(state, "CLOSE-WAIT") == 0) close_wait++;
                else if (strcmp(state, "LISTEN") == 0) listen++;
                else other++;

                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"state\":\"%s\",\"local\":\"%s\",\"peer\":\"%s\",\"process\":\"%s\"}",
                    state, local, peer, pname);
                count++;
                if (offset > (int)sizeof(json_buf) - 512) break;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"summary\":{\"total\":%d,\"established\":%d,\"time_wait\":%d,"
        "\"close_wait\":%d,\"listen\":%d,\"other\":%d},"
        "\"timestamp\":\"%s\",\"exec_time\":\"%s\"}",
        count, established, time_wait, close_wait, listen, other, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /services: systemd 서비스 목록 ────────────────── */
static void handler_services(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[16384];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"services\":[");
    int count = 0;
    int active=0, inactive=0, failed=0;

    FILE* fp = popen("systemctl list-units --type=service --no-pager --no-legend 2>/dev/null", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            char name[128], load[32], state[32], sub[32], desc[256];
            desc[0] = '\0';
            if (sscanf(line, "%127s %31s %31s %31s %255[^\n]", name, load, state, sub, desc) >= 4) {
                if (strcmp(state, "active") == 0) active++;
                else if (strcmp(state, "inactive") == 0) inactive++;
                else if (strcmp(state, "failed") == 0) failed++;

                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"name\":\"%s\",\"load\":\"%s\",\"active\":\"%s\","
                    "\"sub\":\"%s\",\"description\":\"%s\"}",
                    name, load, state, sub, desc);
                count++;
                if (offset > (int)sizeof(json_buf) - 512) break;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"summary\":{\"total\":%d,\"active\":%d,\"inactive\":%d,\"failed\":%d},"
        "\"timestamp\":\"%s\",\"exec_time\":\"%s\"}",
        count, active, inactive, failed, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /users: 현재 접속자 ────────────────────────────── */
static void handler_users(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[4096];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"users\":[");
    int count = 0;

    FILE* fp = popen("who 2>/dev/null", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            char user[64], tty[32], date[32], time_str[16], ip[64];
            ip[0] = '\0';
            if (sscanf(line, "%63s %31s %31s %15s %63[^\n]", user, tty, date, time_str, ip) >= 4) {
                /* IP에서 괄호 제거 */
                char clean_ip[64] = "";
                if (ip[0] == '(') {
                    snprintf(clean_ip, sizeof(clean_ip), "%s", ip + 1);
                    char* rp = strchr(clean_ip, ')');
                    if (rp) *rp = '\0';
                } else {
                    snprintf(clean_ip, sizeof(clean_ip), "%s", ip);
                }
                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"user\":\"%s\",\"tty\":\"%s\",\"login_time\":\"%s %s\",\"ip\":\"%s\"}",
                    user, tty, date, time_str, clean_ip);
                count++;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /lastlog: 로그인 이력 ──────────────────────────── */
static void handler_lastlog(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    const char* limit_str = req->query ? hashmap_get_str(req->query, "limit") : NULL;
    int limit = limit_str ? atoi(limit_str) : 50;
    if (limit <= 0 || limit > 500) limit = 50;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "last -n %d --time-format iso 2>/dev/null | head -n %d", limit, limit);

    char json_buf[16384];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"lastlog\":[");
    int count = 0;

    FILE* fp = popen(cmd, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            char user[64], tty[32], ip[64], login[32], logout[64];
            ip[0] = '\0'; logout[0] = '\0';
            if (sscanf(line, "%63s %31s %63s %31s %63[^\n]", user, tty, ip, login, logout) >= 4) {
                if (strcmp(user, "wtmp") == 0 || strcmp(user, "reboot") == 0 || strlen(user) == 0) continue;
                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"user\":\"%s\",\"tty\":\"%s\",\"ip\":\"%s\",\"login\":\"%s\",\"logout\":\"%s\"}",
                    user, tty, ip, login, logout);
                count++;
                if (offset > (int)sizeof(json_buf) - 512) break;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /auth/failures: 로그인 실패 이력 ──────────────── */
static void handler_auth_failures(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    const char* limit_str = req->query ? hashmap_get_str(req->query, "limit") : NULL;
    int limit = limit_str ? atoi(limit_str) : 100;
    if (limit <= 0 || limit > 1000) limit = 100;

    char json_buf[16384];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"auth_failures\":[");
    int count = 0;

    /* journalctl 또는 /var/log/secure 파싱 */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "journalctl _COMM=sshd --no-pager -n %d 2>/dev/null"
        " | grep 'Failed\\|Invalid\\|refused' | tail -n %d", limit, limit);

    FILE* fp = popen(cmd, "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            /* 이중 따옴표 이스케이프 */
            char escaped[1024] = "";
            int ei = 0;
            for (int i = 0; line[i] && ei < 1020; i++) {
                if (line[i] == '"') escaped[ei++] = '\\';
                escaped[ei++] = line[i];
            }
            if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
            offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                "{\"log\":\"%s\"}", escaped);
            count++;
            if (offset > (int)sizeof(json_buf) - 512) break;
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /sessions: SSH 세션 ────────────────────────────── */
static void handler_sessions(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char json_buf[4096];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"sessions\":[");
    int count = 0;

    FILE* fp = popen("ss -tnp 2>/dev/null | grep ':22 '", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            char state[32], local[128], peer[128], proc[256];
            proc[0] = '\0';
            if (sscanf(line, "%31s %*s %*s %127s %127s %255[^\n]", state, local, peer, proc) >= 3) {
                char pname[128] = "";
                char* np = strstr(proc, "users:((\"");
                if (np) {
                    np += 9;
                    char* end = strchr(np, '"');
                    if (end) { *end = '\0'; snprintf(pname, sizeof(pname), "%s", np); }
                }
                if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
                offset += snprintf(json_buf + offset, sizeof(json_buf) - offset,
                    "{\"state\":\"%s\",\"local\":\"%s\",\"peer\":\"%s\",\"process\":\"%s\"}",
                    state, local, peer, pname);
                count++;
            }
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /logs: 로그 파일 tail ──────────────────────────── */
static void handler_logs(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    const char* file_str  = req->query ? hashmap_get_str(req->query, "file")  : NULL;
    const char* lines_str = req->query ? hashmap_get_str(req->query, "lines") : NULL;

    if (!file_str || *file_str == '\0') {
        Router_sendError(res, 400, "Missing_Parameter",
            "file 파라미터가 필요합니다. 예: /logs?file=messages&lines=100");
        return;
    }

    /* 보안: .. 경로 탐색 차단 */
    if (strstr(file_str, "..")) {
        Router_sendError(res, 403, "Access_Denied",
            "경로 탐색(..)이 감지되었습니다.");
        return;
    }

    /* 보안: 허용 문자 검사 (알파벳·숫자·-_./만 허용, 서브디렉토리 OK) */
    for (int i = 0; file_str[i]; i++) {
        char c = file_str[i];
        if (!isalnum((unsigned char)c) &&
            c != '-' && c != '_' && c != '.' && c != '/') {
            Router_sendError(res, 403, "Access_Denied",
                "파일 경로에 허용되지 않은 문자가 포함되어 있습니다.");
            return;
        }
    }

    /* 경로 조합: /var/log/ + 경로(서브디렉토리 포함 가능) */
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/var/log/%s", file_str);

    int lines = lines_str ? atoi(lines_str) : 100;
    if (lines <= 0 || lines > 1000) lines = 100;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tail -n %d %s 2>/dev/null", lines, full_path);

    char json_buf[65536];
    int offset = snprintf(json_buf, sizeof(json_buf),
        "{\"file\":\"%s\",\"lines\":[", full_path);
    int count = 0;

    FILE* fp = popen(cmd, "r");
    if (fp) {
        char line[2048];
        while (fgets(line, sizeof(line), fp)) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            /* 이중 따옴표 이스케이프 */
            char escaped[4096] = "";
            int ei = 0;
            for (int i = 0; line[i] && ei < 4090; i++) {
                if (line[i] == '"') escaped[ei++] = '\\';
                else if (line[i] == '\\') escaped[ei++] = '\\';
                escaped[ei++] = line[i];
            }
            if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ",");
            offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, "\"%s\"", escaped);
            count++;
            if (offset > (int)sizeof(json_buf) - 1024) break;
        }
        pclose(fp);
    }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}

/* ── /info: OS·커널·에이전트 버전 ──────────────────── */
static void handler_info(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char hostname[128] = ""; gethostname(hostname, sizeof(hostname));
    char kernel[128] = "";
    FILE* fp = popen("uname -r 2>/dev/null", "r");
    if (fp) { if (fgets(kernel, sizeof(kernel), fp)) { char* nl = strchr(kernel,'\n'); if(nl)*nl='\0'; } pclose(fp); }

    char os_name[256] = "";
    FILE* of = fopen("/etc/os-release", "r");
    if (of) {
        char line[256];
        while (fgets(line, sizeof(line), of)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                snprintf(os_name, sizeof(os_name), "%s", line + 13);
                char* nl = strchr(os_name, '\n');
                char* qt = strchr(os_name, '"');
                if (nl) *nl = '\0';
                if (qt) { /* 앞 따옴표 제거 */ memmove(os_name, os_name, strlen(os_name)); }
                break;
            }
        }
        fclose(of);
    }

    char arch[32] = "";
    FILE* af = popen("uname -m 2>/dev/null", "r");
    if (af) { if (fgets(arch, sizeof(arch), af)) { char* nl = strchr(arch,'\n'); if(nl)*nl='\0'; } pclose(af); }

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    char resp[512];
    snprintf(resp, sizeof(resp),
        "{\"hostname\":\"%s\",\"os\":\"%s\",\"kernel\":\"%s\",\"arch\":\"%s\","
        "\"agent\":\"jsoftlab-server-agent\",\"agent_version\":\"1.7.0\","
        "\"timestamp\":\"%s\",\"exec_time\":\"%s\"}",
        hostname, os_name, kernel, arch, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, resp);
}

/* ── /alerts: 임계치 초과 알림 ─────────────────────── */
static void handler_alerts(HttpRequest* req, HttpResponse* res, void* ctx) {
    AUTH_CHECK(req, res);
    (void)req; (void)ctx;
    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    /* /status 와 동일한 수집 로직 재사용 */
    long total_mb = 0, available_mb = 0, swap_total_mb = 0, swap_free_mb = 0;
    FILE* mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[128];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %ld kB", &total_mb)) total_mb /= 1024;
            else if (sscanf(line, "MemAvailable: %ld kB", &available_mb)) available_mb /= 1024;
            else if (sscanf(line, "SwapTotal: %ld kB", &swap_total_mb)) swap_total_mb /= 1024;
            else if (sscanf(line, "SwapFree: %ld kB", &swap_free_mb)) swap_free_mb /= 1024;
        }
        fclose(mf);
    }
    long used_mb = total_mb - available_mb;
    long swap_used_mb = swap_total_mb - swap_free_mb;
    double mem_percent = (total_mb > 0) ? ((double)used_mb * 100.0 / total_mb) : 0.0;
    double swap_pct = (swap_total_mb > 0) ? ((double)swap_used_mb * 100.0 / swap_total_mb) : 0.0;

    struct statvfs st; statvfs("/", &st);
    double disk_total = (double)st.f_blocks * st.f_frsize / 1024.0 / 1024.0 / 1024.0;
    double disk_avail = (double)st.f_bavail * st.f_frsize / 1024.0 / 1024.0 / 1024.0;
    double disk_pct   = (disk_total > 0) ? ((disk_total - disk_avail) * 100.0 / disk_total) : 0.0;

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    FILE* lf = fopen("/proc/loadavg", "r");
    if (lf) { if (fscanf(lf, "%lf %lf %lf", &load1, &load5, &load15) != 3) { load1 = load5 = load15 = 0.0; } fclose(lf); }

    long core_count = sysconf(_SC_NPROCESSORS_ONLN);
    double cpu_total = 0.0; int proc_count = 0, zombie_count = 0;
    FILE* fp = popen("ps aux --no-headers | awk '{cpu+=$3; count++; if($8 ~ /^Z/) z++} END {print cpu, count, z+0}'", "r");
    if (fp) { if (fscanf(fp, "%lf %d %d", &cpu_total, &proc_count, &zombie_count) != 3) { cpu_total = 0.0; proc_count = 0; zombie_count = 0; } pclose(fp); }
    double cpu_avg = (core_count > 0) ? (cpu_total / core_count) : 0.0;

    char json_buf[2048];
    int offset = snprintf(json_buf, sizeof(json_buf), "{\"alerts\":[");
    int count = 0;

#define ADD_ALERT(type, msg, val) \
    do { \
        if (count > 0) offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, ","); \
        offset += snprintf(json_buf + offset, sizeof(json_buf) - offset, \
            "{\"type\":\"%s\",\"msg\":\"%s\",\"value\":%.1f}", type, msg, (double)val); \
        count++; \
    } while(0)

    if (cpu_avg    > 90.0)       ADD_ALERT("CPU_CRITICAL",  "CPU usage > 90%",        cpu_avg);
    if (mem_percent > 90.0)      ADD_ALERT("MEM_CRITICAL",  "Memory usage > 90%",     mem_percent);
    if (disk_pct   > 95.0)       ADD_ALERT("DISK_CRITICAL", "Disk usage > 95%",       disk_pct);
    if (load1      > core_count) ADD_ALERT("LOAD_WARN",     "Load1 exceeds CPU cores", load1);
    if (swap_pct   > 80.0)       ADD_ALERT("SWAP_WARN",     "Swap usage > 80%",        swap_pct);
    if (zombie_count > 0)        ADD_ALERT("ZOMBIE_WARN",   "Zombie process detected", zombie_count);
#undef ADD_ALERT

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32]; format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    snprintf(json_buf + offset, sizeof(json_buf) - offset,
        "],\"count\":%d,\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", count, ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, json_buf);
}


#define KEY_STORE_PATH "/etc/jsoftlab-agent/.key_store"

/* SHA-256 해시 계산 (libcore Hasher) */
static void compute_sha256_hex(const char* input, char* out_hex, size_t hex_size) {
    out_hex[0] = '\0';
    Hasher* h = new_Hasher("SHA-256");
    if (!h) return;
    String* result = h->hash(h, input);
    RELEASE((Object*)h);
    if (!result) return;
    snprintf(out_hex, hex_size, "%s", result->value);
    RELEASE((Object*)result);
}

/* .key_store 에 해시 저장 */
static int save_key_hash(const char* api_key) {
    char hash_hex[65] = "";
    compute_sha256_hex(api_key, hash_hex, sizeof(hash_hex));
    if (strlen(hash_hex) == 0) return 0;

    mkdir("/etc/jsoftlab-agent", 0700);
    FILE* fp = fopen(KEY_STORE_PATH, "w");
    if (!fp) return 0;
    fprintf(fp, "%s\n", hash_hex);
    fclose(fp);
    chmod(KEY_STORE_PATH, 0600);
    return 1;
}

/* .key_store 해시와 비교 */
static int verify_key_hash(const char* api_key) {
    FILE* fp = fopen(KEY_STORE_PATH, "r");
    if (!fp) return 0;
    char stored[65] = "";
    if (fgets(stored, sizeof(stored), fp)) {
        char* nl = strchr(stored, '\n'); if (nl) *nl = '\0';
    }
    fclose(fp);

    char hash_hex[65] = "";
    compute_sha256_hex(api_key, hash_hex, sizeof(hash_hex));
    return strcmp(hash_hex, stored) == 0;
}

/* .key_store 존재 여부 */
static int key_store_exists(void) {
    FILE* fp = fopen(KEY_STORE_PATH, "r");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

/* ── POST /register: API 키 등록/변경 ──────────────── */
static void handler_register(HttpRequest* req, HttpResponse* res, void* ctx) {
    (void)ctx;

    /* 1. req->body 확인 */
    if (!req->body || strlen((char*)req->body) == 0) {
        Router_sendError(res, 400, "Bad_Request",
            "Request body가 비어 있습니다. {\"api_key\":\"your-key\"}");
        return;
    }

    /* 2. strstr로 api_key 값 발라먹기 */
    char new_key_buf[512] = "";
    char* p = strstr((char*)req->body, "\"api_key\"");
    if (p) {
        p += 9;
        while (*p == ' ' || *p == ':' || *p == '\t') p++;
        if (*p == '"') p++;
        int i = 0;
        while (*p && *p != '"' && i < 511)
            new_key_buf[i++] = *p++;
        new_key_buf[i] = '\0';
    }

    const char* new_key = new_key_buf;
    if (strlen(new_key) == 0) {
        Router_sendError(res, 400, "Bad_Request",
            "api_key 값이 올바르지 않거나 비어 있습니다.");
        return;
    }

    /* 3. 이미 등록된 경우 → 기존 키 인증 필수 */
    if (key_store_exists()) {
        const char* auth = req->headers
                         ? hashmap_get_str(req->headers, "authorization")
                         : NULL;
        if (!auth || strncmp(auth, "Bearer ", 7) != 0) {
            Router_sendError(res, 409, "Already_Registered",
                "이미 API 키가 등록되어 있습니다. 변경하려면 기존 Bearer 토큰 인증이 필요합니다.");
            return;
        }
        if (!verify_key_hash(auth + 7)) {
            Router_sendError(res, 401, "Unauthorized",
                "기존 API 키가 올바르지 않습니다.");
            return;
        }
    }

    /* 4. SHA-256 해시로 저장 */
    if (!save_key_hash(new_key)) {
        Router_sendError(res, 500, "Internal_Server_Error",
            "키 저장에 실패했습니다.");
        return;
    }

    printf("🔐 API 키 등록/변경 완료.\n");

    char ts[32]; get_current_timestamp(ts, sizeof(ts));
    char resp[256];
    snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"message\":\"API 키가 정상적으로 등록되었습니다.\","
        "\"timestamp\":\"%s\"}", ts);
    res->setStatus(res, 201);
    res->setHeader(res, "Content-Type", "application/json");
    res->setHeader(res, "Access-Control-Allow-Origin", "*");
    res->sendText(res, resp);
}

static void handler_health(HttpRequest* req, HttpResponse* res, void* ctx) {
    (void)req;
    (void)ctx;

    struct timeval start_t, end_t;
    gettimeofday(&start_t, NULL);

    char ts[32];
    get_current_timestamp(ts, sizeof(ts));

    gettimeofday(&end_t, NULL);
    double elapsed_ms = (end_t.tv_sec - start_t.tv_sec) * 1000.0 + (end_t.tv_usec - start_t.tv_usec) / 1000.0;
    char elapsed_str[32];
    format_time_str(elapsed_ms, elapsed_str, sizeof(elapsed_str));

    char resp[256];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"agent\":\"jsoftlab-server-agent\",\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->sendText(res, resp);
}

int main(int argc, char* argv[]) {
    const char* conf_path = DEFAULT_CONF_PATH;
    const char* key_path  = DEFAULT_KEY_PATH;

    /* 인자 파싱 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i+1 < argc)
            conf_path = argv[++i];
        else if (strcmp(argv[i], "--keyfile") == 0 && i+1 < argc)
            key_path = argv[++i];
    }

    /* 설정 파일 로드 */
    load_config(conf_path, key_path);

    /* .key_store 에서 API 키 로드 */
    FILE* ks = fopen("/etc/jsoftlab-agent/.key_store", "r");
    if (ks) {
        char stored_key[512] = "";
        if (fgets(stored_key, sizeof(stored_key), ks)) {
            char* nl = strchr(stored_key, '\n'); if (nl) *nl = '\0';
            if (strlen(stored_key) > 0)
                printf("🔐 API 키 로드: .key_store\n");
        }
        fclose(ks);
    }

    printf("✅ [jsoftlab-server-agent v1.7.0] Listening on 0.0.0.0:%d\n", AGENT_PORT);

    if (key_store_exists())
        printf("🔐 Security: Bearer Token Auth ENABLED.\n");
    else
        printf("⚠️  Security: API Key not registered. POST /register to set key.\n");

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    g_loop = event_loop_create();
    if (!g_loop) return 1;

    Router* router = new_Router(NULL);
    if (!router) {
        RELEASE((Object*)g_loop);
        return 1;
    }

    router->GET(router, "/health",          handler_health);
    router->POST(router, "/register",         handler_register);
    router->GET(router, "/status",          handler_status);
    router->GET(router, "/metrics",         handler_metrics);
    router->GET(router, "/disk",            handler_disk);
    router->GET(router, "/traffic",         handler_traffic);
    router->GET(router, "/traffic/:iface",  handler_traffic_iface);
    router->GET(router, "/processes",       handler_processes);
    router->GET(router, "/processes/:id",   handler_process_by_pid);
    router->GET(router, "/ports",           handler_ports);
    router->GET(router, "/connections",     handler_connections);
    router->GET(router, "/services",        handler_services);
    router->GET(router, "/users",           handler_users);
    router->GET(router, "/lastlog",         handler_lastlog);
    router->GET(router, "/auth/failures",   handler_auth_failures);
    router->GET(router, "/sessions",        handler_sessions);
    router->GET(router, "/logs",            handler_logs);
    router->GET(router, "/info",            handler_info);
    router->GET(router, "/alerts",          handler_alerts);

    g_server = new_HttpServer(g_loop, router);
    RELEASE((Object*)router);

    if (!g_server) {
        RELEASE((Object*)g_loop);
        return 1;
    }

    g_server->listen(g_server, AGENT_PORT);
    event_loop_run(g_loop);

    if (g_server) {
        RELEASE((Object*)g_server);
        g_server = NULL;
    }
    if (g_loop) {
        RELEASE((Object*)g_loop);
        g_loop = NULL;
    }

    return 0;
}
