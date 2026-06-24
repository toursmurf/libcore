/*
 * File: arc_snmp_parallel_walk.c
 * Author: InDong KIM (idong322@naver.com)
 * Copyright (c) 2026 Toos IT Holdings. All rights reserved.
 * License: MIT
 *
 * ThreadPool 기반 병렬 SNMP Walk 예제
 * 장비 N대를 동시에 Walk하여 대규모 NMS 수집 구현
 */
#include "libcore.h"
#include "coresnmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* =========================================
 * [설정]
 * ========================================= */
#define WORKER_THREADS   10       // 동시 처리 스레드 수
#define SNMP_COMMUNITY   "public"
#define SNMP_VERSION     "2c"
#define ROOT_OID         "1.3.6.1.2.1.25.4.2"  // 프로세스 테이블

/* =========================================
 * [Task 구조체]
 * ========================================= */
typedef struct {
    char     ip[64];
    char     root_oid[128];
    char     community[64];
    int      result_count;
    int      success;
} SnmpWalkTask;

/* =========================================
 * [결과 수집용 Mutex]
 * ========================================= */
static pthread_mutex_t g_result_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_total_varbinds = 0;
static int g_success_count  = 0;
static int g_fail_count     = 0;

/* =========================================
 * [워커 함수]
 * 스레드별로 CoreSnmp 인스턴스 독립 생성!!
 * ========================================= */
static void* snmp_walk_worker(void* arg) {
    SnmpWalkTask* t = (SnmpWalkTask*)arg;
    if (!t) return NULL;

    /* 스레드별 독립 CoreSnmp 생성 (공유 금지!) */
    CoreSnmp* snmp = new_Snmp(
        SNMP_TRANS_UDP,
        t->community,
        t->community);

    if (!snmp) {
        pthread_mutex_lock(&g_result_lock);
        g_fail_count++;
        pthread_mutex_unlock(&g_result_lock);
        t->success = 0;
        return NULL;
    }

    /* Walk 실행 */
    ArrayList* result = new_ArrayList(128);
    ErrorCode ret = snmp->snmpWalk(
        snmp, t->ip, t->root_oid, result);

    int count = result->getSize(result);
    t->result_count = count;
    t->success = (ret == OK) ? 1 : 0;

    /* 전역 결과 업데이트 */
    pthread_mutex_lock(&g_result_lock);
    if (ret == OK) {
        g_success_count++;
        g_total_varbinds += count;
        printf("[OK] %-16s → %d varbinds\n",
            t->ip, count);
    } else {
        g_fail_count++;
        printf("[FAIL] %-16s → 수집 실패\n", t->ip);
    }
    pthread_mutex_unlock(&g_result_lock);

    RELEASE((Object*)result);
    RELEASE((Object*)snmp);
    return NULL;
}

/* =========================================
 * [Main]
 * ========================================= */
int main(void) {
    printf("=== Toos IT Holdings: Parallel SNMP Walk ===\n");
    printf("스레드 수: %d\n", WORKER_THREADS);
    printf("Root OID: %s\n\n", ROOT_OID);

    /* 테스트 장비 목록 (실제 환경에서는 DB에서 로드) */
    const char* ip_list[] = {
        "127.0.0.1",
        "127.0.0.1",
	"192.168.210.1",
        "127.0.0.1",
        "127.0.0.1",
	"192.168.210.2",
        "127.0.0.1",
    };

    int ip_count = (int)(sizeof(ip_list) / sizeof(ip_list[0]));

    /* Task 배열 생성 */
    SnmpWalkTask* tasks = calloc(ip_count, sizeof(SnmpWalkTask));
    if (!tasks) return 1;

    for (int i = 0; i < ip_count; i++) {
        strncpy(tasks[i].ip, ip_list[i], 63);
        strncpy(tasks[i].root_oid, ROOT_OID, 127);
        strncpy(tasks[i].community, SNMP_COMMUNITY, 63);
    }

    /* ThreadPool 생성 */
    ThreadPool* pool = new_ThreadPool(WORKER_THREADS, 0);
    if (!pool) {
        free(tasks);
        return 1;
    }

    printf("[시작] %d대 병렬 Walk 시작...\n\n", ip_count);

    /* Task 제출 */
    for (int i = 0; i < ip_count; i++) {
        pool->submit(pool, snmp_walk_worker, &tasks[i]);
    }

    /* 완료 대기 */
    pool->shutdown(pool);

    /* 최종 결과 출력 */
    printf("\n=== 결과 요약 ===\n");
    printf("총 장비: %d대\n", ip_count);
    printf("성공:    %d대\n", g_success_count);
    printf("실패:    %d대\n", g_fail_count);
    printf("총 수집: %d varbinds\n", g_total_varbinds);

    RELEASE((Object*)pool);
    free(tasks);
    pthread_mutex_destroy(&g_result_lock);

    return 0;
}
