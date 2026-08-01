	#ifndef CRYPTO_H
	#define CRYPTO_H

	#include "object.h"
	#include "bytebuffer.h"
	#include "string_obj.h"
	#include <stdint.h>
	#include <stdbool.h>

	#ifdef __cplusplus
	extern "C" {
	#endif

	/* ==========================================
	 * 1. Hasher (단방향 해시 & 패스워드)
	 * 지원: "SHA-256", "SHA-512", "BCRYPT" 등
	 * ========================================== */
	typedef struct Hasher Hasher;

	struct Hasher {
	    Object base; // ARC 호환

	    char algo[32]; // 알고리즘 이름
	    void* ctx;     // [PRIVATE] OpenSSL / Bcrypt Context 은닉

	    // VTable
	    String* (*hash)(Hasher* self, const char* plain_text);
	    bool (*verify)(Hasher* self, const char* plain_text, const char* hashed_text);
	};

	Hasher* new_Hasher(const char* algo);

	/* ==========================================
	 * 2. Cipher (양방향 대칭키 암복호화)
	 * 지원: "AES-256-CBC", "AES-256-GCM" 등
	 * ========================================== */
	typedef struct Cipher Cipher;

	struct Cipher {
	    Object base; // ARC 호환

	    char algo[32];
	    void* ctx;   // [PRIVATE] OpenSSL EVP_CIPHER_CTX 포인터 은닉

	    // 🚨 SNMPv3 대응: 동적 Key/IV 저장 공간
	    uint8_t key[64];
	    uint8_t iv[64];
	    size_t key_len;
	    size_t iv_len;

	    // VTable (🚨 init 시그니처 완벽 동기화 완료!!)
	    bool (*init)(Cipher* self, const uint8_t* key, size_t key_len, const uint8_t* iv, size_t iv_len);
	    ByteBuffer* (*encrypt)(Cipher* self, const uint8_t* plain_data, size_t len);
	    ByteBuffer* (*decrypt)(Cipher* self, const uint8_t* encrypted_data, size_t len);
	};

	Cipher* new_Cipher(const char* algo);

	/* ==========================================
	 * 3. Base64 유틸리티
	 * ========================================== */
	char* Base64_encode(const uint8_t* data, size_t len);
	uint8_t* Base64_decode(const char* base64_str, size_t* out_len);

	/* ==========================================
	 * 4. Pure Libcore Crypto (No External Dependency)
	 * ========================================== */
	void Crypto_SHA1(const uint8_t* data, size_t len, uint8_t out_hash[20]);
	char* Crypto_Base64Encode(const uint8_t* data, size_t len);

	#ifdef __cplusplus
	}
	#endif

	#endif // CRYPTO_H