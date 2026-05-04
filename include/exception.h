#ifndef EXCEPTION_H
#define EXCEPTION_H

#include "object.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================
   ErrorCode (Core 제거)
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

    // Network
    ERR_NET_CONNECT = 300,
    ERR_NET_TIMEOUT = 301,
    ERR_NET_HTTP    = 302,

    // Config
    ERR_CONFIG = 500

} ErrorCode;

/* 문자열 변환 */
const char* ErrorCode_toString(ErrorCode code);

/* ==============================
   Exception 클래스
   ============================== */

typedef struct Exception Exception;

//Object<-Exception
struct Exception {
    Object base;

    ErrorCode code;
    char* message;
    char* fileName;
    int lineNumber;
    Exception* cause;

    /* 메서드 */
    const char* (*getMessage)(Exception*);
    Exception*  (*getCause)(Exception*);
    ErrorCode   (*getCode)(Exception*);
    bool        (*hasCause)(Exception*);
    void        (*printStackTrace)(Exception*);
};

/* ==============================
   생성자
   ============================== */

Exception* new_Exception(
    ErrorCode code,
    const char* msg,
    Exception* cause,
    const char* file,
    int line
);

/* ==============================
   매크로
   ============================== */

#define throw_Exception(code, msg) \
    new_Exception((code), (msg), NULL, __FILE__, __LINE__)

#define throw_ExceptionCause(code, msg, cause) \
    new_Exception((code), (msg), (cause), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif // EXCEPTION_H

