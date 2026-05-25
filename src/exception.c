#include "exception.h"
#include "string_obj.h" // 🚀 [ARC 규격] String 객체 헤더
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================
   ErrorCode 문자열 변환
   ========================= */
const char* ErrorCode_toString(ErrorCode code) {
    switch (code) {
        case OK: return "OK";

        case ERR_NULL: return "NULL_POINTER";
        case ERR_OOM: return "OUT_OF_MEMORY";
        case ERR_IO: return "IO_ERROR";
        case ERR_PARSE: return "PARSE_ERROR";
        case ERR_INVALID: return "INVALID_ARGUMENT";

        case ERR_LLM_LOAD: return "LLM_LOAD_FAIL";
        case ERR_LLM_INFER: return "LLM_INFER_FAIL";
        case ERR_LLM_TIMEOUT: return "LLM_TIMEOUT";
        case ERR_LLM_TOKEN: return "LLM_TOKEN_EXCEEDED";

        case ERR_FILE_NOT_FOUND: return "FILE_NOT_FOUND";
        case ERR_FILE_PERM: return "FILE_PERMISSION_DENIED";
        case ERR_FILE_READ: return "FILE_READ_FAIL";
        case ERR_FILE_WRITE: return "FILE_WRITE_FAIL";

        case ERR_NET_CONNECT: return "NETWORK_CONNECT_FAIL";
        case ERR_NET_TIMEOUT: return "NETWORK_TIMEOUT";
        case ERR_NET_HTTP: return "HTTP_ERROR";

        case ERR_CONFIG: return "CONFIG_ERROR";

        // 🚀 [챕터 10] 네트워크 에러 2000번대 완벽 매핑
        case ERR_SOCK_CREATE:  return "SOCK_CREATE_FAIL";
        case ERR_SOCK_BIND:    return "SOCK_BIND_FAIL";
        case ERR_SOCK_LISTEN:  return "SOCK_LISTEN_FAIL";
        case ERR_SOCK_CONNECT: return "SOCK_CONNECT_FAIL";
        case ERR_SOCK_TIMEOUT: return "SOCK_TIMEOUT";
        case ERR_SOCK_REFUSED: return "SOCK_CONN_REFUSED";
        case ERR_SOCK_RESET:   return "SOCK_CONN_RESET";
        case ERR_SOCK_AGAIN:   return "SOCK_AGAIN";
        case ERR_SOCK_CLOSED:  return "SOCK_CLOSED";
        case ERR_SOCK_PERM:    return "SOCK_PERMISSION_DENIED";
        case ERR_SOCK_ADDRUSE: return "SOCK_ADDR_IN_USE";
        case ERR_SOCK_URL:     return "SOCK_URL_PARSE_FAIL";
        case ERR_SOCK_SCHEME:  return "SOCK_UNSUPPORTED_SCHEME";
        case ERR_SOCK_PORT:    return "SOCK_INVALID_PORT";

        default: return "UNKNOWN_ERROR";
    }
}

/* =========================
   메서드 구현부
   ========================= */

static String* Exception_getMessage(Exception* self) {
    return self ? self->message : NULL;
}

static int Exception_getSysErrno(Exception* self) {
    return self ? self->sys_errno : 0;
}

static Exception* Exception_getCause(Exception* self) {
    return self ? self->cause : NULL;
}

static ErrorCode Exception_getCode(Exception* self) {
    return self ? self->code : OK;
}

static bool Exception_hasCause(Exception* self) {
    return (self && self->cause != NULL);
}

static void Exception_printStackTrace(Exception* self) {
    Exception* cur = self;

    while (cur) {
        // 🚀 String 객체의 내부 value를 안전하게 꺼내어 출력 (NULL 방어막 탑재)
        printf("[%s:%d] (%s/errno:%d) %s\n",
            (cur->fileName && cur->fileName->value) ? cur->fileName->value : "unknown",
            cur->lineNumber,
            ErrorCode_toString(cur->code),
            cur->sys_errno,  // 🚀 플랫폼 원본 에러 코드 출력
            (cur->message && cur->message->value) ? cur->message->value : ""
        );
        cur = cur->cause;
    }
}

/* =========================
   finalize (ARC 연쇄 소각로)
   ========================= */

static void Exception_finalize(Object* obj) {
    Exception* self = (Exception*)obj;

    // 🚀 더 이상 free()는 없습니다! String 객체 연쇄 소각
    if (self->message)  RELEASE((Object*)self->message);
    if (self->fileName) RELEASE((Object*)self->fileName);

    // 🚀 원인(Cause) Exception 체인 연쇄 폭발
    if (self->cause) {
        RELEASE((Object*)self->cause);
    }
}

/* =========================
   Class (Iron Fortress 완벽 방어 규격 - 클순 부장님 PASS)
   ========================= */

static const Class Exception_Class = {
    .name = "Exception",
    .size = sizeof(Exception),
    .toString = NULL,
    .equals   = NULL,
    .hashCode = NULL,
    .finalize = Exception_finalize
};

/* =========================
   생성자
   ========================= */

Exception* new_Exception(
    ErrorCode code,
    int sys_errno,      // 🚀 sys_errno 파라미터 탑재
    const char* msg,
    Exception* cause,
    const char* file,
    int line
) {
    Exception* self = calloc(1, sizeof(Exception));
    if (!self) return NULL;

    Object_Init((Object*)self, &Exception_Class);

    self->code = code;
    self->sys_errno = sys_errno;

    // 🚀 char* 문자열을 String* 객체로 승격 (ARC의 품으로!)
    self->message  = new_String(msg ? msg : "");
    self->fileName = new_String(file ? file : "unknown");
    self->lineNumber = line;

    // 🚀 Cause 체인 RETAIN (소유권 확보)
    if (cause) {
        RETAIN((Object*)cause);
        self->cause = cause;
    }

    /* VTable 바인딩 */
    self->getMessage      = Exception_getMessage;
    self->getSysErrno     = Exception_getSysErrno;
    self->getCause        = Exception_getCause;
    self->getCode         = Exception_getCode;
    self->hasCause        = Exception_hasCause;
    self->printStackTrace = Exception_printStackTrace;

    return self;
}