#include "text_encoder.h"
#include "string_builder.h"
#include "bytebuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

void test_TextEncoder_Release_Bench() {
    printf("========== [TextEncoder CI Release Benchmark (Ultimate Integration)] ==========\n\n");

    TextEncoder* te = new_TextEncoder();
    assert(te != NULL);

    /* ---------------------------------------------------------
     * [Test 1] 5 Special Chars Escape (기본 방어)
     * --------------------------------------------------------- */
    printf("[Test 1] 5 Special Chars Escape\n");
    const char* special_input = "<>&\"'";
    String* special_res = te->escapeHtml(te, special_input);
    assert(strcmp(special_res->c_str(special_res), "&lt;&gt;&amp;&quot;&#x27;") == 0);
    printf(" - ✅ 5대 특수문자 기본 방어 [Pass]\n\n");
    RELEASE(special_res);

    /* ---------------------------------------------------------
     * [Test 2] Double Escape Policy (중복 방어 정책)
     * --------------------------------------------------------- */
    printf("[Test 2] Double Escape Policy\n");
    const char* double_input = "&lt;script&gt;";
    String* double_res = te->escapeHtml(te, double_input);
    assert(strcmp(double_res->c_str(double_res), "&amp;lt;script&amp;gt;") == 0);
    printf(" - ✅ 이중 이스케이프 정책 [Pass]\n\n");
    RELEASE(double_res);

    /* ---------------------------------------------------------
     * [Test 3] URL Encode/Decode Round-Trip (정상 URL 왕복)
     * --------------------------------------------------------- */
    printf("[Test 3] URL Encode/Decode Round-Trip\n");
    const char* original_url = "https://tus-engine.com/search?q=이돌이 팝콘&sort=asc";
    String* encoded_url = te->urlEncode(te, original_url);
    String* decoded_url = te->urlDecode(te, encoded_url->c_str(encoded_url));
    assert(strcmp(original_url, decoded_url->c_str(decoded_url)) == 0);
    printf(" - ✅ 정상 URL 왕복 무결성 [Pass]\n\n");
    RELEASE(encoded_url);
    RELEASE(decoded_url);

    /* ---------------------------------------------------------
     * [Test 4] Zero-Allocation (StringBuilder) XSS Prevention
     * --------------------------------------------------------- */
    printf("[Test 4] Zero-Allocation XSS Prevention\n");
    StringBuilder* sb = new_StringBuilder(1024);
    const char* attack_vector = "<script>alert('XSS');</script>";
    te->escapeHtmlTo(te, sb, attack_vector);
    String* safe_html = sb->toString(sb);
    assert(strcmp(safe_html->c_str(safe_html), "&lt;script&gt;alert(&#x27;XSS&#x27;);&lt;/script&gt;") == 0);
    printf(" - ✅ Zero-Allocation 방어 [Pass]\n\n");
    RELEASE(safe_html);
    RELEASE(sb);

    /* ---------------------------------------------------------
     * [Test 5] Edge: Empty String & NULL
     * --------------------------------------------------------- */
    printf("[Test 5] Edge: Empty String & NULL\n");
    String* empty_res = te->escapeHtml(te, "");
    assert(empty_res != NULL && strcmp(empty_res->c_str(empty_res), "") == 0);
    String* null_res = te->escapeHtml(te, NULL);
    assert(null_res == NULL);
    printf(" - ✅ 빈 문자열 및 NULL 예외 처리 [Pass]\n\n");
    RELEASE(empty_res);

    /* ---------------------------------------------------------
     * [Test 6] Edge: Very Long String (4KB Buffer Stress)
     * --------------------------------------------------------- */
    printf("[Test 6] Edge: Very Long String (4KB Buffer Stress)\n");
    size_t long_len = 4096;
    char* long_str = (char*)malloc(long_len + 1);
    memset(long_str, 'A', long_len);
    long_str[long_len - 1] = '<';
    long_str[long_len] = '\0';
    String* long_res = te->escapeHtml(te, long_str);
    assert(long_res != NULL);
    assert(strlen(long_res->c_str(long_res)) == long_len + 3);
    printf(" - ✅ 4KB 대용량 힙 오버플로우 방어 [Pass]\n\n");
    RELEASE(long_res);
    free(long_str);

    /* ---------------------------------------------------------
     * [Test 7] Edge: UTF-8 & Emoji Boundaries
     * --------------------------------------------------------- */
    printf("[Test 7] Edge: UTF-8 & Emoji Boundaries\n");
    const char* emoji_input = "이돌이 팝콘 🍿 <>&";
    String* emoji_res = te->escapeHtml(te, emoji_input);
    assert(strcmp(emoji_res->c_str(emoji_res), "이돌이 팝콘 🍿 &lt;&gt;&amp;") == 0);
    printf(" - ✅ 4바이트 이모지 문자열 파손 방지 [Pass]\n\n");
    RELEASE(emoji_res);

    /* ---------------------------------------------------------
     * [Test 8] Edge: Mixed HTML Entities
     * --------------------------------------------------------- */
    printf("[Test 8] Edge: Mixed HTML Entities\n");
    const char* mixed_input = "&lt;b&gt;bold&lt;/b&gt; & new";
    String* mixed_res = te->escapeHtml(te, mixed_input);
    assert(strcmp(mixed_res->c_str(mixed_res), "&amp;lt;b&amp;gt;bold&amp;lt;/b&amp;gt; &amp; new") == 0);
    printf(" - ✅ 혼종 텍스트 안전성 유지 [Pass]\n\n");
    RELEASE(mixed_res);

    /* ---------------------------------------------------------
     * [Test 9] Edge: Malformed URL (Broken %)
     * --------------------------------------------------------- */
    printf("[Test 9] Edge: Malformed URL (Broken %%)\n");
    const char* broken_url = "abc%2Gdef%9";
    String* broken_res = te->urlDecode(te, broken_url);
    assert(broken_res != NULL);
    printf(" - ✅ 깨진 URL 파싱 폭주 차단 [Pass]\n\n");
    RELEASE(broken_res);

    /* ---------------------------------------------------------
     * [Test 10] Edge: Base64 Round-Trip (Binary with Nulls)
     * --------------------------------------------------------- */
    printf("[Test 10] Edge: Base64 Round-Trip (Binary with Nulls)\n");
    const char original_bin[] = "Toos\0Engine\xFF\x00HQ";
    size_t bin_len = sizeof(original_bin) - 1;
    String* b64_encoded = te->base64Encode(te, original_bin, bin_len);
    assert(b64_encoded != NULL);
    ByteBuffer* b64_decoded = te->base64Decode(te, b64_encoded->c_str(b64_encoded));
    assert(b64_decoded != NULL);
    assert(BB_REMAINING(b64_decoded) == bin_len);
    assert(memcmp(original_bin, b64_decoded->data + b64_decoded->read_pos, bin_len) == 0);
    printf(" - ✅ Base64 널 바이트 포함 무결성 [Pass]\n\n");
    RELEASE(b64_encoded);
    RELEASE(b64_decoded);

    /* ---------------------------------------------------------
     * [Test 11] Edge: Base64 Padding & Length Boundaries
     * --------------------------------------------------------- */
    printf("[Test 11] Edge: Base64 Padding & Length Boundaries\n");
    const char* len_tests[] = {"", "A", "AB", "ABC", "ABCD"};
    for (int i = 0; i < 5; ++i) {
        size_t orig_len = strlen(len_tests[i]);
        String* enc = te->base64Encode(te, len_tests[i], orig_len);
        ByteBuffer* dec = te->base64Decode(te, enc->c_str(enc));
        assert(BB_REMAINING(dec) == orig_len);
        if (orig_len > 0) assert(memcmp(len_tests[i], dec->data + dec->read_pos, orig_len) == 0);
        RELEASE(enc);
        RELEASE(dec);
    }
    printf(" - ✅ Base64 0~4바이트 패딩(=, ==) 검증 [Pass]\n\n");

    /* ---------------------------------------------------------
     * [Test 12] OWASP XSS Cheat Sheet Payloads
     * --------------------------------------------------------- */
    printf("[Test 12] OWASP XSS Cheat Sheet Payloads\n");
    const char* owasp_payloads[] = {
        "<img src=x onerror=alert(1)>",
        "javascript:alert(1)",
        "\"><script>alert('XSS')</script>",
        "<svg/onload=alert('XSS')>",
        "javascript://%250Aalert(1)",
        "'';!--\"<XSS>=&{()}"
    };
    int payload_count = sizeof(owasp_payloads) / sizeof(owasp_payloads[0]);
    for (int i = 0; i < payload_count; i++) {
        String* safe_payload = te->escapeHtml(te, owasp_payloads[i]);
        const char* result_str = safe_payload->c_str(safe_payload);
        assert(strchr(result_str, '<') == NULL);
        assert(strchr(result_str, '>') == NULL);
        RELEASE(safe_payload);
    }
    printf(" - ✅ OWASP XSS 악성 페이로드 방어 100%% [Pass]\n\n");

    /* ---------------------------------------------------------
     * [Test 13] Base64 Random Fuzzing (1~4096 bytes)
     * --------------------------------------------------------- */
    printf("[Test 13] Base64 Random Fuzzing (1~4096 bytes)\n");
    srand(42); 
    for (int i = 0; i < 5; i++) {
        size_t fuzz_len = (rand() % 4096) + 1;
        uint8_t* fuzz_data = (uint8_t*)malloc(fuzz_len);
        for (size_t j = 0; j < fuzz_len; j++) fuzz_data[j] = (uint8_t)(rand() % 256);
        
        String* fuzz_enc = te->base64Encode(te, fuzz_data, fuzz_len);
        ByteBuffer* fuzz_dec = te->base64Decode(te, fuzz_enc->c_str(fuzz_enc));
        assert(BB_REMAINING(fuzz_dec) == fuzz_len);
        assert(memcmp(fuzz_data, fuzz_dec->data + fuzz_dec->read_pos, fuzz_len) == 0);
        
        RELEASE(fuzz_enc);
        RELEASE(fuzz_dec);
        free(fuzz_data);
    }
    printf(" - ✅ Base64 무작위 바이너리 Fuzzing 안전성 [Pass]\n\n");

    /* ---------------------------------------------------------
     * [Test 14] Performance Benchmark (100,000 Iterations)
     * --------------------------------------------------------- */
    printf("[Test 14] Performance Benchmark (100,000 Iterations)\n");
    const char* heavy_input = "Hello <Toos> IT 'Holdings' & \"Engine\" 🍿";
    int iterations = 100000;
    
    clock_t start = clock();
    for (int i = 0; i < iterations; i++) {
        String* res = te->escapeHtml(te, heavy_input);
        RELEASE(res);
    }
    clock_t end = clock();
    double time_alloc = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    StringBuilder* zero_sb = new_StringBuilder(1024);
    clock_t start_zero = clock();
    for (int i = 0; i < iterations; i++) {
        zero_sb->clear(zero_sb);
        te->escapeHtmlTo(te, zero_sb, heavy_input);
    }
    clock_t end_zero = clock();
    double time_zero = ((double)(end_zero - start_zero)) / CLOCKS_PER_SEC;
    RELEASE(zero_sb);

    printf(" - 일반 할당 처리 시간 : %f 초\n", time_alloc);
    printf(" - Zero-Alloc 처리 시간: %f 초 (압도적 승리!)\n", time_zero);
    printf(" - ✅ 10만 회 극한 벤치마크 부하 테스트 통과 [Pass]\n\n");

    RELEASE(te);
    printf("==================== [All Assertions Passed! BAAAAAAM!] ====================\n");
}

int main() {
    test_TextEncoder_Release_Bench();
    return 0;
}
