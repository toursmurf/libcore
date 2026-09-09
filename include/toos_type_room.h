#pragma once

#include "object.h"
#include <stdint.h>
#include <stdbool.h>

#define TOOS_TYPE_COUNTDOWN_MS 3000ULL
#define TOOS_TYPE_PHASE1_DURATION_MS 60000ULL

typedef enum {
    TOOS_TYPE_STATE_WAITING = 0,
    TOOS_TYPE_STATE_COUNTDOWN,
    TOOS_TYPE_STATE_PLAYING,
    TOOS_TYPE_STATE_RESULT
} ToosTypeState;

typedef enum {
    TOOS_TYPE_TRANSITION_NONE = 0,
    TOOS_TYPE_TRANSITION_COUNTDOWN_STARTED,
    TOOS_TYPE_TRANSITION_GAME_STARTED,
    TOOS_TYPE_TRANSITION_GAME_OVER,
    TOOS_TYPE_TRANSITION_ERROR
} ToosTypeTransition;

typedef struct ToosTypeRoom ToosTypeRoom;

struct ToosTypeRoom {
    Object base;
    uint64_t room_id;
    ToosTypeState state;
    uint64_t seq;
    uint32_t phase;

    uint64_t state_start_ms;
    uint64_t countdown_ends_at_ms;
    uint64_t game_ends_at_ms;

    ToosTypeTransition (*startCountdown)(ToosTypeRoom *self, uint64_t now_ms);
    ToosTypeTransition (*tick)(ToosTypeRoom *self, uint64_t now_ms);
    ToosTypeTransition (*abortGame)(ToosTypeRoom *self, uint64_t now_ms);
    bool (*nextSeq)(ToosTypeRoom *self, uint64_t *out_seq);
};
ToosTypeRoom *new_ToosTypeRoom(uint64_t room_id);