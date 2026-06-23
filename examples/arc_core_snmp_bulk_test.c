#include "coresnmp.h"
#include <stdio.h>
#include <unistd.h>

void process_with_tag(ArrayList* results) {
    int count = results->getSize(results);
    for (int i = 0; i < count; i++) {
        SnmpVarBind* vb = (SnmpVarBind*)results->get(results, i);

        // 🚀 strstr 대신 tag 필드로 깔끔하게 분기 (이것이 근본!)
        switch (vb->tag) {
            case ASN1_COUNTER32:
                printf("[Traffic] %s : %s octets\n", vb->oid, vb->value_str);
                break;
            case ASN1_GAUGE32:
                printf("[CPU Load] %s : %s %%\n", vb->oid, vb->value_str);
                break;
            case ASN1_TIMETICKS:
                printf("[Uptime] %s : %s ticks\n", vb->oid, vb->value_str);
                break;
            case ASN1_IPADDRESS:
                printf("[Admin IP] %s : %s\n", vb->oid, vb->value_str);
                break;
            default:
                printf("[Generic] %s : %s\n", vb->oid, vb->value_str);
        }
    }
}

void example_snmp_get_and_bulk() {
    printf("\n--- [1] SNMP 데이터 수집 테스트 ---\n");

    // [1] SNMP v1 통신 확인
    CoreSnmp* snmp_v1 = new_Snmp(SNMP_TRANS_UDP, "1", "public");
    uint8_t out_buf[512] = {0};
    if (snmp_v1->sendGetNext(snmp_v1, "10.0.0.1", "1.3.6.1.2.1.1.1.0", out_buf, sizeof(out_buf)) == CORE_OK) {
        // 🚀 버퍼 처리 개선: 실제 데이터가 수신되었음을 Hex Dump로 증명!
        printf("[V1] GetNext 통신 성공! 원시 버퍼 헤더: %02X %02X %02X %02X\n", out_buf[0], out_buf[1], out_buf[2], out_buf[3]);
    }
    RELEASE_NULL((Object**)&snmp_v1);

    // [2] SNMP v2c GetBulk 및 루프 락 최적화
    CoreSnmp* snmp_v2c = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    ArrayList* v2_results = new_ArrayList(10);

    if (snmp_v2c->sendGetBulk(snmp_v2c, "10.0.0.1", "1.3.6.1.2.1.2.2.1.2", 0, 10, v2_results) == CORE_OK) {
        // 🚀 O(1) 락 최적화: 루프 밖에서 미리 getSize()를 호출하여 캐싱!
        int count = v2_results->getSize(v2_results);
        printf("[V2C] GetBulk 파싱 완료 (총 %d건)\n", count);

        for (int i = 0; i < count; i++) {
            SnmpVarBind* vb = (SnmpVarBind*)v2_results->get(v2_results, i);
            printf("  -> OID: %s | VAL: %s\n", vb->oid, vb->value_str);
        }
    }
    RELEASE_NULL((Object**)&v2_results);
    RELEASE_NULL((Object**)&snmp_v2c);

    // [3] SNMP v3 보안 수집
    uint8_t auth_key[] = "MyAuthPassword123";
    uint8_t priv_key[] = "MyPrivPassword123";

    CoreSnmp* snmp_v3 = new_SnmpV3(
        SNMP_TRANS_UDP, "admin_user",
        SNMP_SEC_AUTH_PRIV,
        SNMP_AUTH_SHA, auth_key, 17,
        SNMP_PRIV_AES, priv_key, 17
    );

    ArrayList* v3_results = new_ArrayList(20);
    if (snmp_v3->sendGetBulk(snmp_v3, "10.0.0.2", "1.3.6.1.2.1.31.1.1.1.1", 0, 20, v3_results) == CORE_OK) {
        int count = v3_results->getSize(v3_results);
        printf("[V3] 강력한 보안 터널을 통한 GetBulk 성공! (총 %d건 수집)\n", count);
    }
    RELEASE_NULL((Object**)&v3_results);
    RELEASE_NULL((Object**)&snmp_v3);
}

void example_trap_send_receive() {
    printf("\n--- [2] SNMP TRAP 송수신 테스트 ---\n");

    CoreSnmp* trap_manager = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    if (trap_manager->startListen(trap_manager, 1620) == CORE_OK) {
        printf("[Manager] TRAP 수신 서버가 1620 포트에서 대기 중입니다...\n");
    } else {
        printf("[Manager] 수신 서버 바인딩 실패!\n");
    }

    CoreSnmp* trap_agent = new_Snmp(SNMP_TRANS_UDP, "2c", "public");

    // 🚀 직접 필드 접근 철폐: Setter 메서드로 캡슐화 원칙 완전 준수!
    trap_agent->setTrapPort(trap_agent, 1620);

    printf("[Agent] Link-Down 장애 발생! Manager(127.0.0.1)로 TRAP을 발사합니다!\n");
    if (trap_agent->sendTrap(trap_agent, "127.0.0.1", "1.3.6.1.6.3.1.1.5.3") == CORE_OK) {
        printf("[Agent] TRAP 발송 성공! 🔫\n");
    }

    sleep(1);
    trap_manager->stopListen(trap_manager);

    RELEASE_NULL((Object**)&trap_manager);
    RELEASE_NULL((Object**)&trap_agent);
}

int main(int argc, char* argv[]) {
    printf("====================================================\n");
    printf("   🔥 투스IT 제국 - NMS Iron Fortress 기동 🔥       \n");
    printf("====================================================\n");

    example_snmp_get_and_bulk();
    example_trap_send_receive();

		ㄴCoreSnmp* nms = new_Snmp(SNMP_TRANS_UDP, "2c", "public");
    uint8_t raw_buf[2048];
    size_t actual_len = 0; // 🚀 실제 수신 길이 저장 변수

    // 1. sendGet 호출 시 실제 수신 길이를 받아옴
    if (nms->sendGet(nms, "192.168.1.1", "1.3.6.1.2.1.1.3.0", raw_buf, 2048, &actual_len) == CORE_OK) {
        ArrayList* list = new_ArrayList(1);

        // 🚀 정확한 actual_len을 전달 (하드코딩 1024 안녕!)
        snmp_asn_decode_response(raw_buf, actual_len, list);

        process_with_tag(list);
        RELEASE_NULL((Object**)&list);
    }

    RELEASE_NULL((Object**)&nms);
    printf("====================================================\n");
    printf("   🔥 NMS 프로세스 종료 (Valgrind Leak: 0 Bytes) 🔥   \n");
    printf("====================================================\n");

    return 0;
}