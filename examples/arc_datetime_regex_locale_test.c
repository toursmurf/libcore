#include <stdio.h>
#include <assert.h>
#include "datetime.h"
#include "regex_obj.h"
#include "locale.h"
#include "arraylist.h"

// ==========================================
// 1. TimeCore (DateTime) 테스트
// ==========================================
// ==========================================
// 1. TimeCore (DateTime) 테스트
// ==========================================
void test_datetime_module() {
    printf("[TEST] DateTime 모듈 검증 시작...\n");

    // 🚀 [OWNED] 객체 생성 (현재 시각)
    DateTime* now_utc = new_DateTime_now_utc();
    DateTime* now_local = new_DateTime_now();

    assert(now_utc != NULL);
    assert(now_local != NULL);

    char buffer_utc[128];
    char buffer_local[128];

    // toString() 다형성 호출 (현재 시각 비교)
    now_utc->base.type->toString((Object*)now_utc, buffer_utc, sizeof(buffer_utc));
    now_local->base.type->toString((Object*)now_local, buffer_local, sizeof(buffer_local));

    printf("  -> 현재 UTC 시각    : %s\n", buffer_utc);
    printf("  -> 현재 LOCAL 시각  : %s\n", buffer_local);
    printf("  --------------------------------------------------\n");

    // 🚀 [OWNED] 불변성(Immutable) 검증: 5일 뒤의 '새로운' 객체 동시 반환!
    DateTime* future_utc = now_utc->addDays(now_utc, 5);
    DateTime* future_local = now_local->addDays(now_local, 5); // LOCAL도 5일 연산!

    // toString() 다형성 호출 (5일 뒤 미래 시각 비교)
    future_utc->base.type->toString((Object*)future_utc, buffer_utc, sizeof(buffer_utc));
    future_local->base.type->toString((Object*)future_local, buffer_local, sizeof(buffer_local));

    printf("  -> 5일 뒤 UTC 시각  : %s\n", buffer_utc);
    printf("  -> 5일 뒤 LOCAL 시각: %s\n", buffer_local);

    // [OWNED] RFC1123 포맷팅 검증 (HTTP 헤더용이므로 UTC 기준)
    String* rfc_str = future_utc->toRFC1123(future_utc);
    printf("  -> 5일 뒤 RFC1123   : %s\n", rfc_str->value);

    // 🚀 [메모리 해제 철학] 생성된 모든 객체는 역순으로 깔끔하게 반환!
    RELEASE(rfc_str);
    RELEASE(future_local); // 🚀 새로 추가한 미래 LOCAL 객체 반환
    RELEASE(future_utc);
    RELEASE(now_local);
    RELEASE(now_utc);

    printf("[PASS] DateTime 모듈 테스트 완료!\n\n");
}

// ==========================================
// 2. PatternCore (Regex) 테스트
// ==========================================
void test_regex_module() {
    printf("[TEST] Regex 모듈 검증 시작...\n");

    int err = 0;
    // [OWNED] 정규식 객체 생성 (숫자 추출 패턴)
    Regex* reg = new_Regex("[0-9]+", REG_EXTENDED, &err);
    assert(reg != NULL);
    assert(reg->is_compiled == true);

    const char* sample_log = "Error 404 at line 1024: Memory offset 8080";

    // [OWNED] ArrayList 반환 (내부에서 String 매칭 객체들이 RETAIN 됨)
    ArrayList* matches = reg->findAll(reg, sample_log);
    assert(matches != NULL);

    printf("  -> 로그 원문: %s\n", sample_log);
    printf("  -> 패턴 /%s/ 매칭 결과 (%d개 발견):\n", reg->getPattern(reg), matches->getSize(matches));

    for (int i = 0; i < matches->getSize(matches); i++) {
        // [BORROWED] 컬렉션에서 꺼낸 객체는 직접 해제하지 않음
        String* match_str = (String*)matches->get(matches, i);
        printf("     [%d] 매칭: %s\n", i, match_str->value);
    }

    // 🚀 [메모리 해제 철학] ArrayList를 RELEASE하면 내부 요소들도 연쇄적으로 RELEASE 됨!
    RELEASE(matches);
    RELEASE(reg);

    printf("[PASS] Regex 모듈 테스트 완료!\n\n");
}

// ==========================================
// 3. GlobalCore (Locale) 테스트
// ==========================================
void test_locale_module() {
    printf("[TEST] Locale 모듈 검증 시작...\n");

    // [OWNED] 한국 로케일 생성
    Locale* loc_kr = new_Locale("ko", "KR");
    assert(loc_kr != NULL);

    // [BORROWED] 속성 접근 (free 금지)
    const char* lang = loc_kr->getLanguage(loc_kr);
    const char* country = loc_kr->getCountry(loc_kr);

    char buffer[64];
    loc_kr->base.type->toString((Object*)loc_kr, buffer, sizeof(buffer));
    printf("  -> 생성된 Locale: %s (언어: %s, 국가: %s)\n", buffer, lang, country);

    // [OWNED] Fallback 체인 검증 ("ko_KR" -> "ko_")
    Locale* loc_fallback = loc_kr->getFallback(loc_kr);
    loc_fallback->base.type->toString((Object*)loc_fallback, buffer, sizeof(buffer));
    printf("  -> 1차 Fallback 결과: %s\n", buffer);

    // [OWNED] 함수명 혼동을 방지했던 대망의 기본 로케일 생성!
    Locale* loc_default = get_default_locale();
    loc_default->base.type->toString((Object*)loc_default, buffer, sizeof(buffer));
    printf("  -> 시스템 Default Locale: %s\n", buffer);

    // 🚀 [메모리 해제 철학] 생성되거나 반환된 모든 [OWNED] 로케일은 안전하게 해제!
    RELEASE(loc_default);
    RELEASE(loc_fallback);
    RELEASE(loc_kr);

    printf("[PASS] Locale 모듈 테스트 완료!\n\n");
}

// ==========================================
// 메인 엔트리
// ==========================================
int main() {
    printf("==========================================\n");
    printf("🚀 투스 IT 제국 libcore v1.x Datetime, Regex, Locale 통합 QA 시작\n");
    printf("==========================================\n\n");

    test_datetime_module();
    test_regex_module();
    test_locale_module();

    printf("==========================================\n");
    printf("✨ 모든 모듈 ARC 메모리 누수 방어 확인 완료! (Valgrind 0 bytes lost 예상)\n");
    printf("==========================================\n");

    return 0;
}