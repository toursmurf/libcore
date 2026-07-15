#ifndef PATH_VALIDATOR_H
#define PATH_VALIDATOR_H

#include "object.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_PATH_LEN 2048
#define MAX_SEGMENT_LEN 255
#define MAX_SEGMENTS 100

typedef struct PathValidator PathValidator;

struct PathValidator {
    Object base;

    /*
     * 🚀 validate: 경로 유효성 검증 및 정규화(Canonicalize)
     *
     * [호출자 버퍼 가이드]
     * out_size는 최소 (MAX_PATH_LEN + 1) 이상을 권장함.
     * (디코딩 및 정규화 과정은 원본보다 길이를 축소시키므로 버퍼 오버플로를 완벽히 방어함)
     *
     * [Thread-Safety]
     * 본 객체는 내부 상태(State)를 가지지 않는 무상태(Stateless) 구조임.
     * 따라서 단일 인스턴스(Single Instance)를 생성하여 여러 스레드 및 커넥션에서
     * 공유(Shared)하여 호출해도 안전함.
     */
    bool (*validate)(PathValidator* self, const char* raw_path, char* out_canonical, size_t out_size);
};

PathValidator* new_PathValidator(void);

#endif /* PATH_VALIDATOR_H */