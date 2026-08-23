#ifndef TEXT_ENCODER_H
#define TEXT_ENCODER_H

#include "object.h"
#include "string_obj.h"
#include "string_builder.h"
#include "bytebuffer.h" // Base64 디코딩 결과물(바이너리)을 담기 위함
#include <stdbool.h>
#include <stddef.h>

typedef struct TextEncoder TextEncoder;

/* =========================================================
 * 🛡️ TextEncoder :웹 생태계의 데이터 변환 및 XSS 방어 수문장
 * ========================================================= */
struct TextEncoder {
    Object base;

    /* ---------------------------------------------------------
     * [1] 편의성 API (Allocation API)
     * - 호출 시 내부에서 String* 또는 ByteBuffer*를 새로 할당하여 반환.
     * - 사용 후 반드시 RELEASE() 해야 함.
     * --------------------------------------------------------- */
    String* (*escapeHtml)(TextEncoder* self, const char* input);
    String* (*urlEncode)(TextEncoder* self, const char* input);
    String* (*urlDecode)(TextEncoder* self, const char* input);
    String* (*base64Encode)(TextEncoder* self, const void* data, size_t len);
    ByteBuffer* (*base64Decode)(TextEncoder* self, const char* input);

    /* ---------------------------------------------------------
     * [2] 성능 극대화 API (Zero-Allocation API) 🚀
     * - 외부에서 주입된 StringBuilder나 ByteBuffer에 결과만 덧붙임(Append).
     * - 힙 할당(malloc) 오버헤드 0! 게시판 렌더링 속도의 핵심!
     * --------------------------------------------------------- */
    bool (*escapeHtmlTo)(TextEncoder* self, StringBuilder* out, const char* input);
    bool (*urlEncodeTo)(TextEncoder* self, StringBuilder* out, const char* input);
    bool (*urlDecodeTo)(TextEncoder* self, StringBuilder* out, const char* input);
    bool (*base64EncodeTo)(TextEncoder* self, StringBuilder* out, const void* data, size_t len);
    bool (*base64DecodeTo)(TextEncoder* self, ByteBuffer* out, const char* input);
};

/* 생성자 (단일 인스턴스로 Router 등에 소유되어 재사용 권장) */
TextEncoder* new_TextEncoder(void);

#endif /* TEXT_ENCODER_H */
