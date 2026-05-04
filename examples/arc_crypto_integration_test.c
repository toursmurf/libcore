/**
 * @file arc_crypto_integration_test.c
 * @brief 🇰🇷 OpenSSL 기반의 AES 암복호화, SHA 해시, Base64 인코딩/디코딩 통합 검증 예제입니다.
 * 🇬🇧 OpenSSL-based AES encryption/decryption, SHA hashing, and Base64 encoding/decoding integrated verification example.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("==================================================\n");
    printf("🛡️ [libcore v1.0] Crypto 모듈 통합 검증 테스트 🛡️\n");
    printf("==================================================\n\n");

    /* --- Phase 1: Hash --- */
    printf(">>> [Phase 1] SHA-256 테스트...\n");
    Hasher* sha256 = new_Hasher("SHA256"); // 하이픈 제거 버전이 더 안전할 수 있음
    if (!sha256) sha256 = new_Hasher("SHA-256"); // 재시도

    if (sha256) {
        const char* pass = "Imperial_Password_123!";
        String* res = sha256->hash(sha256, pass);
        if (res) {
            printf("  [+] 원본: %s\n", pass);
            printf("  [+] 해시: %s\n", res->value);
            printf("  [+] 해시 길이: %zu 글자\n", strlen(res->value));
            bool ok = sha256->verify(sha256, pass, res->value);
            printf("  [+] 검증: %s\n", ok ? "SUCCESS" : "FAIL");
            RELEASE((Object*)res);
        }
        RELEASE((Object*)sha256);
    } else {
        printf("  [-] 에러: SHA-256 엔진 로드 실패!\n");
    }

		/* --- Phase 1.5: SHA-512 (중화기 추가 테스트) --- */
    printf("\n>>> [Phase 1.5] SHA-512 엔진 가동 (512-bit Power!)...\n");
    Hasher* sha512 = new_Hasher("SHA512");
    if (sha512) {
      const char* msg = "Imperial_Top_Secret_Data_512";
      String* res = sha512->hash(sha512, msg);
      if (res) {
        printf("  [+] SHA-512 결과: %s\n", res->value); // 256보다 훨씬 깁니다!
        printf("  [+] 해시 길이: %zu 글자\n", strlen(res->value));
        bool ok = sha512->verify(sha512, msg, res->value);
        printf("  [+] 검증: %s\n", ok ? "SUCCESS" : "FAIL");
        RELEASE((Object*)res);
      }
      RELEASE((Object*)sha512);
    }


    /* --- Phase 2: AES --- */
    printf("\n>>> [Phase 2] AES-256-CBC 테스트...\n");
    Cipher* aes = new_Cipher("AES-256-CBC");
    if (aes) {
        const char* msg = "Top Secret Message from Core!";
        printf("  [+] 평문: %s\n", msg);

        ByteBuffer* enc = aes->encrypt(aes, (uint8_t*)msg, strlen(msg));
        if (enc) {
            char* b64 = Base64_encode(enc->data, enc->write_pos);
            if (b64) {
                printf("  [+] 암호(B64): %s\n", b64);
                free(b64);
            }

            ByteBuffer* dec = aes->decrypt(aes, enc->data, enc->write_pos);
            if (dec) {
                printf("  [+] 복호화: %.*s\n", (int)dec->write_pos, (char*)dec->data);
                RELEASE((Object*)dec);
            }
            RELEASE((Object*)enc);
        }
        RELEASE((Object*)aes);
    } else {
        printf("  [-] 에러: AES 엔진 로드 실패!\n");
    }

    printf("\n==================================================\n");
    printf("✅ 모든 테스트 완료 (Segfault Zero!)\n");
    printf("==================================================\n");

    return 0;
}
