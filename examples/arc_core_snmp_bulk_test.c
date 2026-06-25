#include "libcore.h"
#include "coresnmp.h"
#include <stdio.h>
#include <string.h>

// localhost 로 테스트
#define TARGET_IP "127.0.0.1"

void example_snmp_get_and_bulk(void) {
    printf("\n--- [1. SNMP V1 GetNext Test] ---\n");
    CoreSnmp* snmp_v1 = new_Snmp(SNMP_TRANS_UDP, "1", "public");
    if (snmp_v1) {
        uint8_t out_buf[2048];
        size_t actual_len = 0;

        // 🚨 최신 엔진 스펙에 맞게 6번째 인자 &actual_len 전달 및 OK 판단!
        if (snmp_v1->sendGetNext(snmp_v1, TARGET_IP, "1.3.6.1.2.1.1.1.0", out_buf, sizeof(out_buf), &actual_len) == OK) {
            printf("V1 GetNext Success! Received %zu bytes.\n", actual_len);
        } else {
            printf("V1 GetNext Timeout or Error! (Check connection or OID)\n");
        }

        // 🚨 캐스팅 완전 제거! lvalue 및 strict-aliasing 경고 완전 차단!
        RELEASE_NULL(snmp_v1);
    }

    printf("\n--- [2. SNMP V2c GetBulk Test] ---\n");
    CoreSnmp* snmp_v2c = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    if (snmp_v2c) {
        ArrayList* v2_results = new_ArrayList(16);

        if (snmp_v2c->sendGetBulk(snmp_v2c, TARGET_IP, "1.3.6.1.2.1.2.2.1.2", 0, 100, v2_results) == OK) {
            printf("V2c GetBulk Success! Parsed %d varbinds.\n", v2_results->getSize(v2_results));

            // 🚨 [데이터 폭포수 출력] 리스트에 동적으로 적재된 결과 실시간 파싱 출력!
            for (int i = 0; i < v2_results->getSize(v2_results); i++) {
                SnmpVarBind* vb = (SnmpVarBind*)v2_results->get(v2_results, i);
                printf("  -> [%d] OID: %s, Value: %s, Type: %s\n", i + 1, vb->oid, vb->value_str, vb->getTypeName(vb));
            }
        } else {
            printf("V2c GetBulk Timeout or Error! (Check Sync Socket & Community String)\n");
        }

        // 🚨 캐스팅 완전 제거형 객체 소각
        RELEASE_NULL(v2_results);
        RELEASE_NULL(snmp_v2c);
    }
/*
    printf("\n--- [3. SNMP V3 GetBulk Test] ---\n");
    // (V3는 장비 측에 해당 보안 계정 스펙이 타겟 매핑되어 있지 않다면 Timeout이 발생하는 것이 아키텍처상 정상입니다)
    CoreSnmp* snmp_v3 = new_SnmpV3(SNMP_TRANS_UDP, "admin", SNMP_SEC_AUTH_PRIV,
                                   SNMP_AUTH_SHA, (const uint8_t*)"authpass123", 11,
                                   SNMP_PRIV_AES, (const uint8_t*)"privpass123", 11);
    if (snmp_v3) {
        ArrayList* v3_results = new_ArrayList(16);

        if (snmp_v3->sendGetBulk(snmp_v3, TARGET_IP, "1.3.6.1.2.1.31.1.1.1.1", 0, 20, v3_results) == OK) {
            printf("V3 GetBulk Success! Parsed %d varbinds.\n", v3_results->getSize(v3_results));
        } else {
            printf("V3 GetBulk Timeout (Expected status if V3 context is missing on target).\n");
        }

        // 🚨 순수 변수 해제 원칙 고수
        RELEASE_NULL(v3_results);
        RELEASE_NULL(snmp_v3);
    }*/
}

void example_trap_send_receive(void) {
    printf("\n--- [4. SNMP Trap Manager/Agent Test] ---\n");
    CoreSnmp* trap_manager = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    if (trap_manager) {
        if (trap_manager->startListen(trap_manager, 1620) == OK) {
            printf("Trap Manager listening on port 1620...\n");
        }

        CoreSnmp* trap_agent = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
        if (trap_agent) {
            trap_agent->setTrapPort(trap_agent, 1620);

            // 로컬 루프백 방식을 이용한 안전한 트랩 발송 시뮬레이션
            if (trap_agent->sendTrap(trap_agent, "127.0.0.1", "1.3.6.1.6.3.1.1.5.3") == OK) {
                printf("Trap successfully sent to Manager!\n");
            }
            RELEASE_NULL(trap_agent);
        }
        RELEASE_NULL(trap_manager);
    }
}

int main(int argc, char* argv[]) {
    // 🚨 -Wextra 옵션의 unused parameter 경고 원천 진압!
    (void)argc;
    (void)argv;

    printf("=== Toos IT Holdings: CoreSnmp Bulk & Trap Engine Test ===\n");

    example_snmp_get_and_bulk();
    example_trap_send_receive();

    printf("\n--- [5. General NMS Get Test] ---\n");
    CoreSnmp* nms = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    if (nms) {
        uint8_t raw_buf[2048];
        size_t actual_len = 0;

        // 장비의 가동 시간을 체크하는 표준 sysUpTime OID 테스트
        if (nms->sendGet(nms, TARGET_IP, "1.3.6.1.2.1.1.3.0", raw_buf, 2048, &actual_len) == OK) {
            printf("NMS Get Success! Received %zu bytes.\n", actual_len);
        } else {
            printf("NMS Get Timeout or Error!\n");
        }

        RELEASE_NULL(nms);
    }

    printf("\nAll Tests Executed. The Empire rests safely.\n");
    return 0;
}