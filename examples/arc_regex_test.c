/**
 * @file arc_regex_test.c
 * @brief 🇰🇷 String 객체 내부의 정규표현식 매칭 기능 테스트입니다.
 * 🇬🇧 Regular expression matching function test inside the String object.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include <stdio.h>
#include "string_obj.h"
int main() {
    printf("=== [libcore 1.1-ARC] String Regex (정규표현식) 테스트 ===\n\n");

    // ---------------------------------------------------------
    // 1. MAC 주소 검증 (대소문자 무시 eregi 테스트)
    // ---------------------------------------------------------
    String* mac = new_String("8A:00:27:2C:C4:5F");
    printf("[MAC 검사] %s\n", mac->c_str(mac));

    // ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ : 전형적인 MAC 주소 정규식
    if (mac->eregi(mac, "^([0-9a-f]{2}:){5}[0-9a-f]{2}$")) {
        printf(" -> ✅ 완벽한 MAC 주소 형식입니다! (대소문자 무시 성공)\n");
    } else {
        printf(" -> ❌ 잘못된 MAC 주소입니다.\n");
    }


    // ---------------------------------------------------------
    // 2. 이메일 형식 검증 (엄격한 대소문자/특수문자 매칭)
    // ---------------------------------------------------------
    String* email = new_String("admin@toos-it.com");
    printf("\n[이메일 검사] %s\n", email->c_str(email));

    // 이메일 정규식 패턴
    if (email->eregi(email, "^[a-z0-9._%+-]+@[a-z0-9.-]+\\.[a-z]{2,}$")) {
        printf(" -> ✅ 유효한 이메일 형식입니다!\n");
    } else {
        printf(" -> ❌ 이메일 형식이 아닙니다.\n");
    }


    // ---------------------------------------------------------
    // 3. NMS 시스템 로그 분석 (특정 에러 키워드 스캐닝)
    // ---------------------------------------------------------
    String* log = new_String("System HALTED due to CRITICAL failure.");
    printf("\n[로그 검사] %s\n", log->c_str(log));

    // 여러 키워드 중 하나라도 걸리는지 확인 (OR 연산자 | 사용)
    if (log->eregi(log, "critical|error|fail|halt")) {
        printf(" -> 🚨 경고! 심각한 오류 키워드가 감지되었습니다!\n");
    } else {
        printf(" -> 🟢 정상 로그입니다.\n");
    }


    // ---------------------------------------------------------
    // 4. IP 주소 형식 검증 (대소문자 구분 matches 테스트)
    // ---------------------------------------------------------
    String* ip_addr = new_String("192.168.100.25");
    printf("\n[IP 검사] %s\n", ip_addr->c_str(ip_addr));

    // 간단한 IPv4 정규식 패턴
    if (ip_addr->matches(ip_addr, "^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$")) {
        printf(" -> ✅ IPv4 주소 형식이 맞습니다!\n");
    } else {
        printf(" -> ❌ IPv4 형식이 아닙니다.\n");
    }

    // ---------------------------------------------------------
    // 5. 메모리 누수 방지 (ARC 생명주기 관리)
    // ---------------------------------------------------------
    printf("\n=== ARC 메모리 안전 해제 ===\n");
    RELEASE(mac);
    RELEASE(email);
    RELEASE(log);
    RELEASE(ip_addr);

    printf(">> 모든 정규표현식 객체 소멸 완료. (Zero Leak)\n");

    return 0;
}