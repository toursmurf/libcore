#define _GNU_SOURCE
#include "crypto.h"
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define SHA1_ROL(val, bits) (((val) << (bits)) | ((val) >> (32 - (bits))))

// ==========================================
// 4. Pure Libcore Crypto (SHA1 / Base64)
// ==========================================
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* Crypto_Base64Encode(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return NULL;
    }

    size_t out_len = ((len + 2) / 3) * 4;
    char* out = (char*)malloc(out_len + 1);

    if (!out) {
        return NULL;
    }

    size_t i;
    size_t j;

    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        uint32_t v = data[i] << 16;

        if (i + 1 < len) {
            v |= data[i + 1] << 8;
        }

        if (i + 2 < len) {
            v |= data[i + 2];
        }

        out[j]     = b64_table[(v >> 18) & 0x3F];
        out[j + 1] = b64_table[(v >> 12) & 0x3F];
        out[j + 2] = (i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=';
        out[j + 3] = (i + 2 < len) ? b64_table[v & 0x3F] : '=';
    }

    out[out_len] = '\0';
    return out;
}

void Crypto_SHA1(const uint8_t* data, size_t len, uint8_t out_hash[20]) {
    if (!data || !out_hash) {
        return;
    }

    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t bit_len = (uint64_t)len * 8;
    size_t padded_len = (len + 1 + 8 + 63) & ~63;

    // 🚨 NULL 체크 방어 완비!!
    uint8_t* buf = (uint8_t*)calloc(1, padded_len);

    if (!buf) {
        return;
    }

    memcpy(buf, data, len);
    buf[len] = 0x80;

    for (int i = 0; i < 8; i++) {
        buf[padded_len - 8 + i] = (uint8_t)(bit_len >> (56 - i * 8));
    }

    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[80];
        const uint8_t* block = buf + offset;

        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)block[i*4] << 24 | (uint32_t)block[i*4+1] << 16 |
                   (uint32_t)block[i*4+2] << 8  | (uint32_t)block[i*4+3];
        }

        for (int i = 16; i < 80; i++) {
            w[i] = SHA1_ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; i++) {
            uint32_t f;
            uint32_t k;

            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = SHA1_ROL(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = SHA1_ROL(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    uint32_t result[5] = {h0, h1, h2, h3, h4};

    for (int i = 0; i < 20; i++) {
        out_hash[i] = (uint8_t)(result[i/4] >> (24 - (i%4) * 8));
    }

    free(buf);
}

// ==========================================
// 1. Hasher 구현체 (단방향 해시)
// ==========================================
static void Hasher_finalize(Object* obj) {
    Hasher* self = (Hasher*)obj;

    if (self && self->ctx) {
        EVP_MD_CTX_free((EVP_MD_CTX*)self->ctx);
    }
}

static Class Hasher_Class = {
    .name = "Hasher",
    .size = sizeof(Hasher), // 🚨 .size 완비!
    .finalize = Hasher_finalize
};

static String* Hasher_hash(Hasher* self, const char* plain_text) {
    if (!self || !self->ctx || !plain_text) {
        return NULL;
    }

    const EVP_MD* md = EVP_get_digestbyname(self->algo);

    if (!md) {
        return NULL;
    }

    EVP_MD_CTX* ctx = (EVP_MD_CTX*)self->ctx;

    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        return NULL;
    }

    if (EVP_DigestUpdate(ctx, plain_text, strlen(plain_text)) != 1) {
        return NULL;
    }

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    if (EVP_DigestFinal_ex(ctx, md_value, &md_len) != 1) {
        return NULL;
    }

    char hex_str[EVP_MAX_MD_SIZE * 2 + 1];

    for (unsigned int i = 0; i < md_len; i++) {
        snprintf(&hex_str[i * 2], 3, "%02x", md_value[i]);
    }

    return new_String(hex_str);
}

static bool Hasher_verify(Hasher* self, const char* plain_text, const char* hashed_text) {
    if (!self || !plain_text || !hashed_text) {
        return false;
    }

    String* new_hash = self->hash(self, plain_text);

    if (!new_hash) {
        return false;
    }

    bool match = (strcmp(new_hash->value, hashed_text) == 0);
    RELEASE((Object*)new_hash);

    return match;
}

Hasher* new_Hasher(const char* algo) {
    OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS | OPENSSL_INIT_LOAD_CONFIG, NULL);
    const EVP_MD* md = EVP_get_digestbyname(algo);

    if (!md) {
        return NULL;
    }

    Hasher* self = calloc(1, sizeof(Hasher));

    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &Hasher_Class);
    snprintf(self->algo, sizeof(self->algo), "%s", algo);

    self->ctx = EVP_MD_CTX_new();

    if (!self->ctx) {
        free(self);
        return NULL;
    }

    self->hash = Hasher_hash;
    self->verify = Hasher_verify;

    return self;
}

// ==========================================
// 2. Cipher 구현체 (양방향 암복호화)
// ==========================================
static void Cipher_finalize(Object* obj) {
    Cipher* self = (Cipher*)obj;

    if (self && self->ctx) {
        EVP_CIPHER_CTX_free((EVP_CIPHER_CTX*)self->ctx);
    }
}

static Class Cipher_Class = {
    .name = "Cipher",
    .size = sizeof(Cipher), // 🚨 .size 완비!
    .finalize = Cipher_finalize
};

// 🚨 동적 Key/IV 세팅 로직 완비!!
static bool Cipher_init(Cipher* self, const uint8_t* key, size_t key_len, const uint8_t* iv, size_t iv_len) {
    if (!self || !key || !iv || key_len > sizeof(self->key) || iv_len > sizeof(self->iv)) {
        return false; // 오버플로우 방어!
    }

    memcpy(self->key, key, key_len);
    self->key_len = key_len;

    memcpy(self->iv, iv, iv_len);
    self->iv_len = iv_len;

    return true;
}

static ByteBuffer* Cipher_encrypt(Cipher* self, const uint8_t* plain_data, size_t len) {
    if (!self || !self->ctx || !plain_data || len == 0) {
        return NULL;
    }

    const EVP_CIPHER* cipher = EVP_get_cipherbyname(self->algo);

    if (!cipher) {
        return NULL;
    }

    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*)self->ctx;

    // 🚨 저장된 Key/IV 사용!!
    if (EVP_EncryptInit_ex(ctx, cipher, NULL, self->key, self->iv) != 1) {
        return NULL;
    }

    int max_len = (int)len + EVP_CIPHER_block_size(cipher);
    ByteBuffer* out_buf = new_ByteBuffer(max_len);

    if (!out_buf) {
        return NULL;
    }

    int out_len1 = 0;

    if (EVP_EncryptUpdate(ctx, out_buf->data, &out_len1, plain_data, (int)len) != 1) {
        RELEASE((Object*)out_buf);
        return NULL;
    }

    int out_len2 = 0;

    if (EVP_EncryptFinal_ex(ctx, (unsigned char*)out_buf->data + out_len1, &out_len2) != 1) {
        RELEASE((Object*)out_buf);
        return NULL;
    }

    out_buf->write_pos = out_len1 + out_len2;
    return out_buf;
}

static ByteBuffer* Cipher_decrypt(Cipher* self, const uint8_t* encrypted_data, size_t len) {
    if (!self || !self->ctx || !encrypted_data || len == 0) {
        return NULL;
    }

    const EVP_CIPHER* cipher = EVP_get_cipherbyname(self->algo);

    if (!cipher) {
        return NULL;
    }

    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*)self->ctx;

    // 🚨 저장된 Key/IV 사용!!
    if (EVP_DecryptInit_ex(ctx, cipher, NULL, self->key, self->iv) != 1) {
        return NULL;
    }

    ByteBuffer* out_buf = new_ByteBuffer(len + EVP_CIPHER_block_size(cipher));

    if (!out_buf) {
        return NULL;
    }

    int out_len1 = 0;

    if (EVP_DecryptUpdate(ctx, out_buf->data, &out_len1, encrypted_data, (int)len) != 1) {
        RELEASE((Object*)out_buf);
        return NULL;
    }

    int out_len2 = 0;

    if (EVP_DecryptFinal_ex(ctx, (unsigned char*)out_buf->data + out_len1, &out_len2) != 1) {
        RELEASE((Object*)out_buf);
        return NULL;
    }

    out_buf->write_pos = out_len1 + out_len2;
    return out_buf;
}

Cipher* new_Cipher(const char* algo) {
    OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_LOAD_CONFIG, NULL);
    const EVP_CIPHER* cipher = EVP_get_cipherbyname(algo);

    if (!cipher) {
        return NULL;
    }

    Cipher* self = calloc(1, sizeof(Cipher));

    if (!self) {
        return NULL;
    }

    Object_Init((Object*)self, &Cipher_Class);
    snprintf(self->algo, sizeof(self->algo), "%s", algo);

    self->ctx = EVP_CIPHER_CTX_new();

    if (!self->ctx) {
        free(self);
        return NULL;
    }

    // 🚨 기본 초기화 (보안상 0 초기화)
    memset(self->key, 0, sizeof(self->key));
    memset(self->iv, 0, sizeof(self->iv));
    self->key_len = 0;
    self->iv_len = 0;

    self->init = Cipher_init;
    self->encrypt = Cipher_encrypt;
    self->decrypt = Cipher_decrypt;

    return self;
}

// ==========================================
// 3. Base64 OpenSSL 래퍼 (🚀 보안 패치 완료 구간!)
// ==========================================
char* Base64_encode(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return NULL;
    }

    size_t max_len = 4 * ((len + 2) / 3) + 1;
    char* out = malloc(max_len);

    if (!out) {
        return NULL;
    }

    int out_len = EVP_EncodeBlock((unsigned char*)out, data, (int)len);
    out[out_len] = '\0';

    return out;
}

uint8_t* Base64_decode(const char* base64_str, size_t* out_len) {
    if (!base64_str || !out_len) {
        return NULL;
    }

    size_t len = strlen(base64_str);

    if (len == 0) {
        *out_len = 0;
        return NULL;
    }

    // 🚀 [보안 패치] 패딩(=) 처리 중 EVP_DecodeBlock의 임시 버퍼 초과 방지!
    // 정확한 길이 계산 공식에 +4 바이트를 더해 힙 스매싱 원천 차단!
    size_t max_out_len = ((len / 4) * 3) + 4;
    uint8_t* out = (uint8_t*)malloc(max_out_len);

    if (!out) {
        fprintf(stderr, "[Base64_decode] Error: malloc failed.\n");
        return NULL;
    }

    // 🚀 [보안 패치] 리턴값 검증 및 Silent Fail 방지 (메모리 릭 차단)
    int decoded_len = EVP_DecodeBlock(out, (const unsigned char*)base64_str, (int)len);

    if (decoded_len < 0) {
        fprintf(stderr, "[Base64_decode] Error: EVP_DecodeBlock failed.\n");
        free(out); // 실패 시 즉시 메모리 반환!
        return NULL;
    }

    // Base64 끝자리 패딩 '=' 문자에 대한 실제 길이 보정 로직
    size_t final_len = (size_t)decoded_len;
    size_t pad_check_len = len;

    // 원본 문자열의 끝에서부터 '=' 개수를 세어 디코딩된 길이를 차감
    while (final_len > 0 && pad_check_len > 0 && base64_str[pad_check_len - 1] == '=') {
        final_len--;
        pad_check_len--;
    }

    // 널 종료 보장 (바이너리 데이터일 수 있으나 안전을 위해)
    if (final_len < max_out_len) {
        out[final_len] = '\0';
    }

    *out_len = final_len;
    return out;
}