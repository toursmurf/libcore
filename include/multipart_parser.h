#ifndef MULTIPART_PARSER_H
#define MULTIPART_PARSER_H

/*
 * multipart_parser.h
 * multipart/form-data 서버 수신 파서
 *
 * Writer(multipart_writer.c)가 보내는 포맷을 역방향으로 파싱한다.
 * HttpMultipartFile 을 재사용하여 Writer↔Parser 대칭 구조 유지.
 *
 * 소유권 규칙:
 *   MultipartResult  [OWNED] — RELEASE() 로 해제
 *   fields HashMap   [OWNED] — name → String*
 *   files  HashMap   [OWNED] — name → HttpMultipartFile*
 *
 * 사용법:
 *   const char* ct  = hashmap_get_str(req->headers, "content-type");
 *   char boundary[256];
 *   if (Multipart_extract_boundary(ct, boundary, sizeof(boundary)) < 0)
 *       return;  // boundary 없음
 *
 *   MultipartResult* mp = Multipart_parse(req->body, req->body_len, boundary);
 *   if (mp) {
 *       const char* title = MultipartResult_get_field(mp, "title");
 *       HttpMultipartFile* f = MultipartResult_get_file(mp, "avatar");
 *       ...
 *       RELEASE((Object*)mp);
 *   }
 */

#include "object.h"
#include "hashmap.h"
#include "http_client.h"   /* HttpMultipartFile */
#include <stddef.h>

/* ── 파싱 결과 컨테이너 ── */
typedef struct {
    Object   base;
    HashMap* fields;   /* [OWNED] name → String* (텍스트 파트) */
    HashMap* files;    /* [OWNED] name → HttpMultipartFile* (파일 파트) */
} MultipartResult;

/*
 * Multipart_extract_boundary
 *   Content-Type 헤더 전체 값에서 boundary 값만 추출한다.
 *   e.g. "multipart/form-data; boundary=----WebCore..."
 *
 *   반환값: 성공 시 boundary 길이(>0), 실패 시 -1
 */
int Multipart_extract_boundary(const char* ct_header,
                                char* out_buf, size_t out_size);

/*
 * Multipart_parse
 *   body      : raw body 바이트 (NUL 포함 가능한 바이너리)
 *   body_len  : body 실제 바이트 수
 *   boundary  : Multipart_extract_boundary 로 추출한 boundary 문자열
 *               (앞의 "--" 없이 순수 boundary 값)
 *
 *   반환값: 성공 시 MultipartResult* (RELEASE 책임은 호출자),
 *           파싱 실패 / boundary 없음 시 NULL
 *
 *   🚨 password 등 민감 데이터는 호출자가 OPENSSL_cleanse 후 처리할 것
 */
MultipartResult* Multipart_parse(const void* body, size_t body_len,
                                 const char* boundary);

/* ── 조회 헬퍼 — 없으면 NULL ── */
const char*        MultipartResult_get_field(const MultipartResult* self,
                                             const char* name);
HttpMultipartFile* MultipartResult_get_file (const MultipartResult* self,
                                             const char* name);

#endif /* MULTIPART_PARSER_H */
