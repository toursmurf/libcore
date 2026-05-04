/**
 * @file arc_ringbuffer_test.c
 * @brief 🇰🇷 멀티스레드 환경에 최적화된 고성능 원형 버퍼(RingBuffer) 통신 예제입니다.
 * 🇬🇧 High-performance RingBuffer communication example optimized for multi-threaded environments.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#include "ring_buffer.h"
#include "object.h" // 의장님의 ARC 시스템

// 스레드 제어용 전역 플래그
static volatile bool g_running = true;

// 1. 큐에 담을 더미 패킷 구조체 (동적 할당용)
typedef struct {
    int packet_id;
    char data[64];
} DummyPacket;

// ============================================================
// 소비자 (Consumer) - Worker Thread 시뮬레이션
// ============================================================
void* consumer_thread(void* arg) {
    RingBuffer* rb = (RingBuffer*)arg;
    printf("[Consumer] 워커 스레드 가동 시작!\n");

    while (g_running) {
        // 100ms 단위로 대기하며 안전하게 Pop! (CPU 100% 방어)
        DummyPacket* pkt = (DummyPacket*)rb->popWait(rb, 100);
        
        if (pkt) {
            // 패킷 파싱 시뮬레이션
            printf("  [👉 POP] 워커가 패킷을 처리했습니다: ID=%d, Data='%s'\n", 
                   pkt->packet_id, pkt->data);
            
            // 🔥 의장님 수칙: 큐에서 빼낸 데이터는 소비자가 반드시 free! (Valgrind 0 bytes)
            free(pkt);
        }
    }
    
    printf("[Consumer] 워커 스레드 정상 종료.\n");
    return NULL;
}

// ============================================================
// 생산자 (Producer) - EventLoop 네트워크 수신 시뮬레이션
// ============================================================
void* producer_thread(void* arg) {
    RingBuffer* rb = (RingBuffer*)arg;
    printf("[Producer] 네트워크 수신 시뮬레이션 시작!\n");

    for (int i = 1; i <= 20 && g_running; i++) {
        // 네트워크에서 들어온 데이터를 동적 할당하여 캡슐화
        DummyPacket* pkt = (DummyPacket*)malloc(sizeof(DummyPacket));
        pkt->packet_id = i;
        snprintf(pkt->data, sizeof(pkt->data), "SNMP_TRAP_PAYLOAD_%d", i);

        // 큐에 밀어넣기 시도
        if (rb->push(rb, pkt)) {
            printf("[📥 PUSH] 링버퍼 삽입 성공: ID=%d\n", i);
        } else {
            // EventLoop 블로킹 방지를 위한 즉시 Drop 로직!
            printf("[🚨 DROP] 링버퍼 FULL! 패킷 드랍: ID=%d\n", i);
            free(pkt); // 큐에 못 들어갔으므로 여기서 소각!
        }
        
        // 50ms마다 패킷 수신 (소비자 대기시간인 100ms보다 빨라서 큐에 쌓임)
        usleep(50000); 
    }

    printf("[Producer] 생산자 임무 완료.\n");
    return NULL;
}

// ============================================================
// 메인 함수
// ============================================================
int main() {
    printf("=== RingBuffer 무결점 테스트 시작 ===\n");

    // 1. RingBuffer 생성 (의장님의 ARC Object_Init 탑재)
    // 용량을 10으로 작게 잡아 FULL/DROP 방어 로직을 테스트합니다.
    RingBuffer* rb = new_RingBuffer(10); 

    // 2. 스레드 생성
    pthread_t prod_tid, cons_tid;
    pthread_create(&cons_tid, NULL, consumer_thread, rb);
    pthread_create(&prod_tid, NULL, producer_thread, rb);

    // 3. 메인 스레드 대기 (생산자가 끝날 때까지 약 1초 대기)
    pthread_join(prod_tid, NULL);

    // 4. 안전 종료 시퀀스 가동
    printf("\n[Main] 시스템 셧다운 시퀀스 가동...\n");
    g_running = false; // 워커 스레드 탈출 유도
    pthread_join(cons_tid, NULL);

    // 5. 큐에 남아있는(처리 안 된) 패킷들 깔끔하게 청소
    printf("[Main] 잔류 패킷 소각 처리 중...\n");
    while (!rb->isEmpty(rb)) {
        DummyPacket* leftover = (DummyPacket*)rb->pop(rb);
        if (leftover) free(leftover);
    }

    // 6. RingBuffer 최종 해제 (ARC)
    RELEASE((Object*)rb);
    
    printf("=== RingBuffer 0 Bytes Leak 셧다운 완료 ===\n");
    return 0;
}
