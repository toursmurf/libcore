#include "exception.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================
   제한
   ========================= */
extern char* safe_strdup(const char* src, size_t max_len);
#define EX_MSG_MAX  1024
#define EX_FILE_MAX 256
/* =========================
   ErrorCode 문자열
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

        default: return "UNKNOWN_ERROR";
    }
}

/* =========================
   메서드
   ========================= */

static const char* Exception_getMessage(Exception* self) {
    return self->message;
}

static Exception* Exception_getCause(Exception* self) {
    return self->cause;
}

static ErrorCode Exception_getCode(Exception* self) {
    return self->code;
}

static bool Exception_hasCause(Exception* self) {
    return self->cause != NULL;
}

static void Exception_printStackTrace(Exception* self) {
    Exception* cur = self;

    while (cur) {
        printf("[%s:%d] (%s) %s\n",
            cur->fileName ? cur->fileName : "unknown",
            cur->lineNumber,
            ErrorCode_toString(cur->code),
            cur->message ? cur->message : ""
        );
        cur = cur->cause;
    }
}

/* =========================
   finalize (ARC)
   ========================= */

static void Exception_finalize(Object* obj) {
    Exception* self = (Exception*)obj;

    if (self->message) free(self->message);
    if (self->fileName) free(self->fileName);

    if (self->cause) {
        RELEASE(self->cause);
    }
}

/* =========================
   Class
   ========================= */

static Class Exception_Class = {
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
    const char* msg,
    Exception* cause,
    const char* file,
    int line
) {
    Exception* self = calloc(1, sizeof(Exception));
    if (!self) return NULL;

    Object_Init((Object*)self, &Exception_Class);

    self->code = code;
    self->message  = safe_strdup(msg, EX_MSG_MAX);
    self->fileName = safe_strdup(file, EX_FILE_MAX);
    self->lineNumber = line;

    if (cause) {
        RETAIN(cause);
        self->cause = cause;
    }

    /* VTable */
    self->getMessage = Exception_getMessage;
    self->getCause   = Exception_getCause;
    self->getCode    = Exception_getCode;
    self->hasCause   = Exception_hasCause;
    self->printStackTrace = Exception_printStackTrace;

    return self;
}
