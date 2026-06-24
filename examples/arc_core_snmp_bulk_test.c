#include "libcore.h"
#include "coresnmp.h"
#include <stdio.h>
#include <string.h>

void example_snmp_get_and_bulk(void) {
    printf("\n--- [1. SNMP V1 GetNext Test] ---\n");
    CoreSnmp* snmp_v1 = new_Snmp(SNMP_TRANS_UDP, "1", "public");
    uint8_t out_buf[2048];
    size_t actual_len = 0;

    // 🚨 6번째 인자 &actual_len 추가 및 OK 비교
    if (snmp_v1->sendGetNext(snmp_v1, "10.0.0.1", "1.3.6.1.2.1.1.1.0", out_buf, sizeof(out_buf), &actual_len) == OK) {
        printf("V1 GetNext Success! Received %zu bytes.\n", actual_len);
    }
    // 🚨 캐스팅 완전 제거!
    RELEASE_NULL(snmp_v1);

    printf("\n--- [2. SNMP V2c GetBulk Test] ---\n");
    CoreSnmp* snmp_v2c = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    ArrayList* v2_results = new_ArrayList(16);

    // 🚨 OK 비교
    if (snmp_v2c->sendGetBulk(snmp_v2c, "10.0.0.1", "1.3.6.1.2.1.2.2.1.2", 0, 10, v2_results) == OK) {
        printf("V2c GetBulk Success! Parsed %d varbinds.\n", v2_results->getSize(v2_results));
    }
    // 🚨 캐스팅 완전 제거!
    RELEASE_NULL(v2_results);
    RELEASE_NULL(snmp_v2c);

    printf("\n--- [3. SNMP V3 GetBulk Test] ---\n");
    CoreSnmp* snmp_v3 = new_SnmpV3(SNMP_TRANS_UDP, "admin", SNMP_SEC_AUTH_PRIV,
                                   SNMP_AUTH_SHA, (const uint8_t*)"authpass123", 11,
                                   SNMP_PRIV_AES, (const uint8_t*)"privpass123", 11);
    ArrayList* v3_results = new_ArrayList(16);

    // 🚨 OK 비교
    if (snmp_v3->sendGetBulk(snmp_v3, "10.0.0.2", "1.3.6.1.2.1.31.1.1.1.1", 0, 20, v3_results) == OK) {
        printf("V3 GetBulk Success! Parsed %d varbinds.\n", v3_results->getSize(v3_results));
    }
    // 🚨 캐스팅 완전 제거!
    RELEASE_NULL(v3_results);
    RELEASE_NULL(snmp_v3);
}

void example_trap_send_receive(void) {
    printf("\n--- [4. SNMP Trap Manager/Agent Test] ---\n");
    CoreSnmp* trap_manager = new_Snmp(SNMP_TRANS_UDP, "2c", "public");

    // 🚨 OK 비교
    if (trap_manager->startListen(trap_manager, 1620) == OK) {
        printf("Trap Manager listening on port 1620...\n");
    }

    CoreSnmp* trap_agent = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    trap_agent->setTrapPort(trap_agent, 1620);

    // 🚨 OK 비교
    if (trap_agent->sendTrap(trap_agent, "127.0.0.1", "1.3.6.1.6.3.1.1.5.3") == OK) {
        printf("Trap successfully sent to Manager!\n");
    }

    // 🚨 캐스팅 완전 제거!
    RELEASE_NULL(trap_manager);
    RELEASE_NULL(trap_agent);
}

int main(int argc, char* argv[]) {
    // 🚨 unused parameter 경고 소각
    (void)argc;
    (void)argv;

    printf("=== Toos IT Holdings: CoreSnmp Bulk & Trap Engine Test ===\n");

    example_snmp_get_and_bulk();
    example_trap_send_receive();

    printf("\n--- [5. General NMS Get Test] ---\n");
    // 🚨 쓰레기 유니코드(\343 등)를 제거하고 수기 타이핑으로 복구 완료
    CoreSnmp* nms = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    uint8_t raw_buf[2048];
    size_t actual_len = 0;

    // 🚨 OK 비교
    if (nms->sendGet(nms, "192.168.1.1", "1.3.6.1.2.1.1.3.0", raw_buf, 2048, &actual_len) == OK) {
        printf("NMS Get Success!\n");
    }

    // 임의의 리스트가 있었다면 (에러 로그 기반 방어적 해제)
    ArrayList* list = new_ArrayList(10);

    // 🚨 캐스팅 완전 제거!
    RELEASE_NULL(list);
    RELEASE_NULL(nms);

    printf("\nAll Tests Executed. The Empire rests safely.\n");
    return 0;
}