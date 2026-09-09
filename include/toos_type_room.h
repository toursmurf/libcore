#pragma once

#include "object.h"
#include <stdint.h>
#include <stdbool.h>

// 명시적 상수로 선언
#define TOOS_TYPE_COUNTDOWN_MS 3000ULL
#define TOOS_TYPE_PHASE1_DURATION_MS 60000ULL

// ToosPixel과의 네임스페이스 분리 (ToosType 전용)
typedef enum {
    TOOS_TYPE_STATE_WAITING = 0,
    TOOS_TYPE_STATE_COUNTDOWN,
    TOOS_TYPE_STATE_PLAYING,
    TOOS_TYPE_STATE_RESULT
} ToosTypeState;

// 상태 전이 결과 Enum (테스트 가능성 및 WebSocket 분리 목적)
typedef enum {
    TOOS_TYPE_TRANSITION_NONE = 0,
    TOOS_TYPE_TRANSITION_COUNTDOWN_STARTED,
    TOOS_TYPE_TRANSITION_GAME_STARTED,
    TOOS_TYPE_TRANSITION_GAME_OVER,
    TOOS_TYPE_TRANSITION_ERROR
} ToosTypeTransition;

typedef struct ToosTypeRoom ToosTypeRoom;

struct ToosTypeRoom {
    Object base; // libcore ARC 객체 헤더

    /* C 스타일 필드 네이밍 (snake_case) */
    uint64_t room_id;
    ToosTypeState state;
    uint64_t seq;
    uint32_t phase;

    /* 절대 시간(Deadline) 모델 적용 */
    uint64_t state_start_ms;
    uint64_t countdown_ends_at_ms;
    uint64_t game_ends_at_ms;

    /* Class API 메소드 (VTable, camelCase) */
    ToosTypeTransition (*startCountdown)(ToosTypeRoom *self, uint64_t now_ms);
    ToosTypeTransition (*tick)(ToosTypeRoom *self, uint64_t now_ms);
    void (*handleTypeResult)(ToosTypeRoom *self, uint32_t player_id, uint32_t sentence_id, const char *input, uint64_t now_ms);
};

// Constructor
ToosTypeRoom *new_ToosTypeRoom(uint64_t room_id);