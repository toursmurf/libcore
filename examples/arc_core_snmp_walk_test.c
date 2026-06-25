#include "libcore.h"
#include "coresnmp.h"
#include <stdio.h>
#include <string.h>

// localhost 로 테스트
#define TARGET_IP "127.0.0.1"

void example_snmp_get_and_walk(void) {
    printf("\n--- [2. SNMP V2c GetWalkTest] ---\n");
    CoreSnmp* snmp_v2c = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    if (snmp_v2c) {
        ArrayList* v2_results = new_ArrayList(16);

        if (snmp_v2c->snmpWalk(snmp_v2c, TARGET_IP, "1.3.6.1.2.1.25.4.2", v2_results) == OK) {
            printf("V2c GetWalk Success! Parsed %d varbinds.\n", v2_results->getSize(v2_results));

            //[데이터 폭포수 출력] 리스트에 동적으로 적재된 결과 실시간 파싱 출력!
            for (int i = 0; i < v2_results->getSize(v2_results); i++) {
                SnmpVarBind* vb = (SnmpVarBind*)v2_results->get(v2_results, i);
                printf("  -> [%d] OID: %s, Value: %s, Type: %s, len=%d\n", i + 1, vb->oid, vb->value_str, vb->getTypeName(vb), vb->value_len);
            }
        } else {
            printf("V2c GetWalk Timeout or Error! (Check Sync Socket & Community String)\n");
        }

        //캐스팅 완전 제거형 객체 소각
        RELEASE_NULL(v2_results);
        RELEASE_NULL(snmp_v2c);
    }
}

int main(int argc, char* argv[]) {
    //-Wextra 옵션의 unused parameter 경고 원천 진압!
    (void)argc;
    (void)argv;

    printf("=== Toos IT Holdings: CoreSnmp Walk & Trap Engine Test ===\n");

    example_snmp_get_and_walk();

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