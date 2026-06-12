/* 3단계 상태 머신 */
#ifndef SNMP_STATE_H
#define SNMP_STATE_H

#include <stdint.h>
#include "object.h"

/**
 * 🚨 [투스 IT 제국: CoreSnmp 상태 머신 규격]
 * 의장님의 철학 반영: 이벤트성 상태를 배제하고 본질적인 상태(State)만 남긴 10년 대계 아키텍처
 */
typedef enum {
    REQ_PENDING, // 🚀 [대기] 발송 직후 및 재전송 후 응답을 기다리는 상태
    REQ_SUCCESS, // ✅ [종결] 응답 수신 성공 완료 상태
    REQ_FAILED   // 💣 [종결] 3회 초과 혹은 소켓 전송 치명적 실패 상태
} RequestState;

#endif // SNMP_STATE_H