#include "toos_type_room.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

// --------------------------------------------------------
// Static Internal Helper
// --------------------------------------------------------

// Deadline 덧셈 Overflow 방어 헬퍼
static bool make_deadline(uint64_t now_ms, uint64_t duration_ms, uint64_t *out_deadline) {
    if (!out_deadline) {
        return false;
    }
    if (UINT64_MAX - now_ms < duration_ms) {
        return false;
    }
    *out_deadline = now_ms + duration_ms;
    return true;
}

static void ToosTypeRoom_finalize(Object *obj) {
    ToosTypeRoom *self = (ToosTypeRoom *)obj;
    printf("[ToosTypeRoom] Room %" PRIu64 " finalized.\n", self->room_id);
    // 향후 players[], activeSentences[] 등 동적 할당 필드가 추가되면 여기서 정리
}

// libcore Class VTable 등록
static const Class _ToosTypeRoom_Class = {
    .name = "ToosTypeRoom",
    .size = sizeof(ToosTypeRoom),
    .finalize = ToosTypeRoom_finalize
};

// --------------------------------------------------------
// VTable Methods (camelCase)
// --------------------------------------------------------
static ToosTypeTransition ToosTypeRoom_startCountdown(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return TOOS_TYPE_TRANSITION_ERROR;
    if (self->state != TOOS_TYPE_STATE_WAITING) return TOOS_TYPE_TRANSITION_NONE;

    uint64_t deadline = 0;
    if (!make_deadline(now_ms, TOOS_TYPE_COUNTDOWN_MS, &deadline)) {
        return TOOS_TYPE_TRANSITION_ERROR;
    }

    self->state = TOOS_TYPE_STATE_COUNTDOWN;
    self->state_start_ms = now_ms;
    self->countdown_ends_at_ms = deadline;
    self->seq++;

    return TOOS_TYPE_TRANSITION_COUNTDOWN_STARTED;
}

static ToosTypeTransition ToosTypeRoom_tick(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return TOOS_TYPE_TRANSITION_ERROR;

    switch (self->state) {
        case TOOS_TYPE_STATE_WAITING:
            return TOOS_TYPE_TRANSITION_NONE;

        case TOOS_TYPE_STATE_COUNTDOWN:
            if (now_ms >= self->countdown_ends_at_ms) {
                uint64_t deadline = 0;

                // 지연된 tick()이 게임 시간을 늘리지 않도록 시작 시점을 예정된 종료 시각으로 고정
                uint64_t game_start_ms = self->countdown_ends_at_ms;

                if (!make_deadline(game_start_ms, TOOS_TYPE_PHASE1_DURATION_MS, &deadline)) {
                    return TOOS_TYPE_TRANSITION_ERROR;
                }

                self->state = TOOS_TYPE_STATE_PLAYING;
                self->state_start_ms = game_start_ms;
                self->game_ends_at_ms = deadline;
                self->seq++;

                return TOOS_TYPE_TRANSITION_GAME_STARTED;
            }
            return TOOS_TYPE_TRANSITION_NONE;

        case TOOS_TYPE_STATE_PLAYING:
            if (now_ms >= self->game_ends_at_ms) {
                self->state = TOOS_TYPE_STATE_RESULT;
                self->state_start_ms = self->game_ends_at_ms;
                self->seq++;

                return TOOS_TYPE_TRANSITION_GAME_OVER;
            }
            return TOOS_TYPE_TRANSITION_NONE;

        case TOOS_TYPE_STATE_RESULT:
            return TOOS_TYPE_TRANSITION_NONE;

        default:
            return TOOS_TYPE_TRANSITION_ERROR;
    }
}

static void ToosTypeRoom_handleTypeResult(ToosTypeRoom *self, uint32_t player_id, uint32_t sentence_id, const char *input, uint64_t now_ms) {
    if (!self || !input) {
        return;
    }

    if (self->state != TOOS_TYPE_STATE_PLAYING) {
        return;
    }

    printf("[ToosTypeRoom] Room %" PRIu64 " | Player %u typed '%s' for sentence %u at %" PRIu64 " ms\n",
           self->room_id, player_id, input, sentence_id, now_ms);
}

// --------------------------------------------------------
// Constructor (new_ClassName)
// --------------------------------------------------------
ToosTypeRoom *new_ToosTypeRoom(uint64_t room_id) {
    ToosTypeRoom *self = (ToosTypeRoom *)calloc(1, sizeof(ToosTypeRoom));
    if (!self) {
        return NULL;
    }

    Object_Init(&self->base, &_ToosTypeRoom_Class);

    self->room_id = room_id;
    self->state = TOOS_TYPE_STATE_WAITING;
    self->seq = 0;
    self->phase = 1;

    self->state_start_ms = 0;
    self->countdown_ends_at_ms = 0;
    self->game_ends_at_ms = 0;

    self->startCountdown = ToosTypeRoom_startCountdown;
    self->tick = ToosTypeRoom_tick;
    self->handleTypeResult = ToosTypeRoom_handleTypeResult;

    return self;
}