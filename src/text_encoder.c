#include "text_encoder.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================
 * 🚀 [Zero-Allocation API] 핵심 로직 구현부
 * 메모리 할당(malloc) 오버헤드 0을 위한 버퍼 주입 방식!
 * ========================================================= */

/* ① HTML 이스케이프 (클순이 검수: < > & " ' 5종 완벽 변환) */
static bool impl_escapeHtmlTo(TextEncoder* self, StringBuilder* out, const char* input) {
    (void)self;
    if (!out || !input) return false;

    while (*input) {
        switch (*input) {
            case '&':  out->append(out, "&amp;"); break;
            case '<':  out->append(out, "&lt;"); break;
            case '>':  out->append(out, "&gt;"); break;
            case '"':  out->append(out, "&quot;"); break;
            case '\'': out->append(out, "&#x27;"); break; /* XSS 방어 최적화 (#39 대신 #x27 권장) */
            default:   out->appendChar(out, *input); break;
        }
        input++;
    }
    return true;
}

/* ② URL 인코딩 (클순이 검수: RFC 3986 기준 비예약문자만 통과) */
static bool impl_urlEncodeTo(TextEncoder* self, StringBuilder* out, const char* input) {
    (void)self;
    if (!out || !input) return false;

    const char *hex = "0123456789ABCDEF";
    while (*input) {
        unsigned char c = (unsigned char)*input;
        /* RFC 3986 Unreserved Characters: 알파벳, 숫자, 하이픈(-), 언더스코어(_), 마침표(.), 물결표(~) */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out->appendChar(out, (char)c);
        } else {
            out->appendChar(out, '%');
            out->appendChar(out, hex[c >> 4]);
            out->appendChar(out, hex[c & 0x0F]);
        }
        input++;
    }
    return true;
}

/* ②-1 URL 디코딩 (Form 데이터의 '+' 기호 공백 치환 포함) */
static bool impl_urlDecodeTo(TextEncoder* self, StringBuilder* out, const char* input) {
    (void)self;
    if (!out || !input) return false;

    while (*input) {
        if (*input == '%') {
            if (input[1] && input[2]) {
                char hexStr[3] = { input[1], input[2], '\0' };
                char decoded = (char)strtol(hexStr, NULL, 16);
                out->appendChar(out, decoded);
                input += 3;
                continue;
            }
        } else if (*input == '+') {
            out->appendChar(out, ' '); /* application/x-www-form-urlencoded 표준 대응 */
        } else {
            out->appendChar(out, *input);
        }
        input++;
    }
    return true;
}

/* ③ Base64 인코딩 (표준 모드 우선 적용 - URL Safe는 추후 플래그 확장 대비) */
static bool impl_base64EncodeTo(TextEncoder* self, StringBuilder* out, const void* data, size_t len) {
    (void)self;
    if (!out || (!data && len > 0)) return false;

    const unsigned char* in = (const unsigned char*)data;
    const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (in[i] << 16);
        if (i + 1 < len) val |= (in[i + 1] << 8);
        if (i + 2 < len) val |= in[i + 2];

        out->appendChar(out, table[(val >> 18) & 0x3F]);
        out->appendChar(out, table[(val >> 12) & 0x3F]);
        out->appendChar(out, (i + 1 < len) ? table[(val >> 6) & 0x3F] : '=');
        out->appendChar(out, (i + 2 < len) ? table[val & 0x3F] : '=');
    }
    return true;
}

/* ③-1 Base64 디코딩용 내부 유틸: 표준(+/)과 URL-Safe(-_) 기호를 동시 지원! */
static inline int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62; /* ✨ 표준(+)과 URL-Safe(-) 동시 호환 ✨ */
    if (c == '/' || c == '_') return 63; /* ✨ 표준(/)과 URL-Safe(_) 동시 호환 ✨ */
    return -1;
}

/* ③-2 Base64 디코딩 (클순이 검수: 디코더에서 표준/URL-safe 양방향 완벽 파싱!) */
static bool impl_base64DecodeTo(TextEncoder* self, ByteBuffer* out, const char* input) {
    (void)self;
    if (!out || !input) return false;

    unsigned int  val = 0; 
    int valb = -8;
    while (*input) {
        unsigned char c = (unsigned char)*input++;
        if (c == '=') break; /* 패딩 도달 시 종료 */

        int dec = b64_val(c);
        if (dec == -1) continue; /* 공백 및 잘못된 문자 무시 */

        val = (val << 6) | dec;
        valb += 6;
        if (valb >= 0) {
            unsigned char decoded_byte = (unsigned char)((val >> valb) & 0xFF);
            out->write(out, &decoded_byte, 1); /* ByteBuffer에 바이너리 데이터 적재 */
            valb -= 8;
        }
    }
    return true;
}

/* =========================================================
 * 🛠️ [Allocation API] 편의성 유틸 구현부
 * 내부적으로 Zero-Allocation API를 호출하여 중복 코드 완벽 제거!
 * ========================================================= */
static String* impl_escapeHtml(TextEncoder* self, const char* input) {
    if (!input) return NULL;
    StringBuilder* sb = new_StringBuilder(32);
    if (sb) {
        self->escapeHtmlTo(self, sb, input);
        String* result = sb->toString(sb);
        RELEASE(sb); /* 내부 버퍼만 안전하게 해제 */
        return result;
    }
    return NULL;
}

static String* impl_urlEncode(TextEncoder* self, const char* input) {
    if (!input) return NULL;
    StringBuilder* sb = new_StringBuilder(32);
    if (sb) {
        self->urlEncodeTo(self, sb, input);
        String* result = sb->toString(sb);
        RELEASE(sb);
        return result;
    }
    return NULL;
}

static String* impl_urlDecode(TextEncoder* self, const char* input) {
    if (!input) return NULL;
    StringBuilder* sb = new_StringBuilder(32);
    if (sb) {
        self->urlDecodeTo(self, sb, input);
        String* result = sb->toString(sb);
        RELEASE(sb);
        return result;
    }
    return NULL;
}

static String* impl_base64Encode(TextEncoder* self, const void* data, size_t len) {
    if (!data && len > 0) return NULL;
    StringBuilder* sb = new_StringBuilder(32);
    if (sb) {
        self->base64EncodeTo(self, sb, data, len);
        String* result = sb->toString(sb);
        RELEASE(sb);
        return result;
    }
    return NULL;
}

static ByteBuffer* impl_base64Decode(TextEncoder* self, const char* input) {
    if (!input) return NULL;
    ByteBuffer* buf = new_ByteBuffer(30); /* 바이너리 데이터를 위한 전용 객체 생성 */
    if (buf) {
        self->base64DecodeTo(self, buf, input);
        return buf; /* 호출자가 나중에 RELEASE 해야 함 */
    }
    return NULL;
}

/* =========================================================
 * 📦 [생성자 및 소멸자] 무상태(Stateless) 인스턴스
 * ========================================================= */
static void TextEncoder_finalize(Object* obj) {
    (void)obj; /* 내부 상태가 없으므로 해제할 자원 없음 */
}

static const Class _TextEncoder_Class = {
    .name = "TextEncoder",
    .size = sizeof(TextEncoder),
    .finalize = TextEncoder_finalize
};

TextEncoder* new_TextEncoder(void) {
    TextEncoder* self = (TextEncoder*)calloc(1, sizeof(TextEncoder));
    if (!self) return NULL;

    Object_Init((Object*)self, &_TextEncoder_Class);

    /* Allocation API 바인딩 */
    self->escapeHtml = impl_escapeHtml;
    self->urlEncode = impl_urlEncode;
    self->urlDecode = impl_urlDecode;
    self->base64Encode = impl_base64Encode;
    self->base64Decode = impl_base64Decode;

    /* Zero-Allocation API 바인딩 */
    self->escapeHtmlTo = impl_escapeHtmlTo;
    self->urlEncodeTo = impl_urlEncodeTo;
    self->urlDecodeTo = impl_urlDecodeTo;
    self->base64EncodeTo = impl_base64EncodeTo;
    self->base64DecodeTo = impl_base64DecodeTo;

    return self;
}
