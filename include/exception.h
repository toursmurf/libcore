#ifndef EXCEPTION_H
#define EXCEPTION_H

#include "object.h"
#include "string_obj.h" // 🚀 [ARC 규격] String 객체 헤더 인클루드
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================
   ErrorCode
   ============================== */

typedef enum ErrorCode {
    OK = 0,

    // 일반
    ERR_NULL    = 1,
    ERR_OOM     = 2,
    ERR_IO      = 3,
    ERR_PARSE   = 4,
    ERR_INVALID = 5,

    // LLM
    ERR_LLM_LOAD    = 100,
    ERR_LLM_INFER   = 101,
    ERR_LLM_TIMEOUT = 102,
    ERR_LLM_TOKEN   = 103,

    // File
    ERR_FILE_NOT_FOUND = 200,
    ERR_FILE_PERM      = 201,
    ERR_FILE_READ      = 202,
    ERR_FILE_WRITE     = 203,

    // Network (Legacy)
    ERR_NET_CONNECT = 300,
    ERR_NET_TIMEOUT = 301,
    ERR_NET_HTTP    = 302,

    // Config
    ERR_CONFIG = 500,

    // 🚀 [챕터 10 패치] 소켓 에러 코드 (2000번대)
    ERR_SOCK_CREATE  = 2001,
    ERR_SOCK_BIND    = 2002,
    ERR_SOCK_LISTEN  = 2003,
    ERR_SOCK_CONNECT = 2004,
    ERR_SOCK_TIMEOUT = 2005,
    ERR_SOCK_REFUSED = 2006,
    ERR_SOCK_RESET   = 2007,
    ERR_SOCK_AGAIN   = 2008,  // 🚀 [누락 복구] EAGAIN/EWOULDBLOCK
    ERR_SOCK_CLOSED  = 2009,  // 🚀 [누락 복구] 정상 종료 방어
    ERR_SOCK_PERM    = 2010,
    ERR_SOCK_ADDRUSE = 2011,
    ERR_SOCK_URL     = 2012,
    ERR_SOCK_SCHEME  = 2013,  // 🚀 [누락 복구] 미지원 프로토콜
    ERR_SOCK_PORT    = 2014

} ErrorCode;

/* 문자열 변환 */
const char* ErrorCode_toString(ErrorCode code);

/* ==============================
   Exception 클래스
   ============================== */

typedef struct Exception Exception;

// Object <- Exception
struct Exception {
    Object base;

    ErrorCode code;
    int sys_errno;
    String* message;      // 🚀 [ARC 규격] char* -> String* (완벽 분리)
    String* fileName;     // 🚀 [ARC 규격] char* -> String* (완벽 분리)
    int lineNumber;
    Exception* cause;     // 🚀 [ARC 체인] 생성자에서 RETAIN, 소멸자에서 RELEASE

    /* 메서드 */
    String* (*getMessage)(Exception*);
    int         (*getSysErrno)(Exception*);
    Exception* (*getCause)(Exception*);
    ErrorCode   (*getCode)(Exception*);
    bool        (*hasCause)(Exception*);
    void        (*printStackTrace)(Exception*);
};

/* ==============================
   생성자
   ============================== */

Exception* new_Exception(
    ErrorCode code,
    int sys_errno,
    const char* msg,
    Exception* cause,
    const char* file,
    int line
);

/* ==============================
   매크로 (ARC & 파일/라인 자동화)
   ============================== */

#define throw_Exception(code, sys_err, msg) \
    new_Exception(code, sys_err, msg, NULL, __FILE__, __LINE__)

#define throw_ExceptionCause(code, sys_err, msg, cause) \
    new_Exception(code, sys_err, msg, cause, __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif // EXCEPTION_H