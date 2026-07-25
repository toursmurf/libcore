/*
 * process_agent.c
 * NMS 원격 프로세스 감시 에이전트 [Official v1.6.6 - Global Timestamp Build]
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
    (void)req;
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
    (void)req;
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
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"agent\":\"libcore-process-agent\",\"timestamp\":\"%s\",\"exec_time\":\"%s\"}", ts, elapsed_str);

    res->setHeader(res, "Content-Type", "application/json");
    res->sendText(res, resp);
}

int main(void) {
    printf("✅ Listening on 0.0.0.0:%d\n", AGENT_PORT);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    g_loop = event_loop_create();
    if (!g_loop) return 1;

    Router* router = new_Router(NULL);
    if (!router) {
        RELEASE((Object*)g_loop);
        return 1;
    }

    router->GET(router, "/health", handler_health);
    router->GET(router, "/status", handler_status);
    router->GET(router, "/traffic", handler_traffic);
    router->GET(router, "/processes", handler_processes);
    router->GET(router, "/processes/:id", handler_process_by_pid);

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