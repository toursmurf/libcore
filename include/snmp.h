#ifndef SNMP_H
#define SNMP_H

#include <stdint.h>
#include <stdbool.h>
#include "object.h"
#include "hashmap.h"
#include "snmp_state.h"

/* [Outstanding 패킷 구조체] */
typedef struct {
    Object base;         // [ARC] 참조 카운트 기반 메모리 자동 관리용 베이스

    uint32_t request_id;
    char target_ip[16];
    RequestState state;  // PENDING, SUCCESS, FAILED
    uint64_t send_time_ms;
    int retry_count;
    uint8_t raw_packet[512]; // 재전송용 원본 패킷 버퍼
    size_t packet_len;
} PendingRequest;

/*메인 엔진 구조체 */
typedef struct Snmp {
    Object base;
    HashMap* pending_requests; // Outstanding 상태 머신 관리 해시맵
    int socket_fd;             // 비동기 UDP 소켓
    // 가상 함수 테이블
    bool (*send_request)(struct Snmp* self, const char* ip, const char* oid);
} Snmp;
// 헬퍼 API
bool Snmp_resend_packet(Snmp* self, PendingRequest* req);
uint64_t get_current_time_ms(void);

#endif // SNMP_H