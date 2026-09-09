#include "toos_type_room.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// --------------------------------------------------------
// Internal Helpers
// --------------------------------------------------------

static bool make_deadline(uint64_t now_ms, uint64_t duration_ms, uint64_t *out_deadline) {
    if (!out_deadline) return false;
    if (UINT64_MAX - now_ms < duration_ms) return false;
    *out_deadline = now_ms + duration_ms;
    return true;
}

// 시퀀스 오버플로우 방어를 단일 헬퍼로 통일
static bool advance_seq(ToosTypeRoom *self, uint64_t *out_seq) {
    if (!self) return false;
    if (self->seq == UINT64_MAX) return false;
    self->seq++;
    if (out_seq) *out_seq = self->seq;
    return true;
}

static void ToosTypeRoom_finalize(Object *obj) {
    (void)obj; // 🔴 [마감] 컴파일러 warning (unused parameter) 완벽 소거
}

static const Class _ToosTypeRoom_Class = {
    .name = "ToosTypeRoom",
    .size = sizeof(ToosTypeRoom),
    .finalize = ToosTypeRoom_finalize
};

// --------------------------------------------------------
// VTable Methods
// --------------------------------------------------------

static ToosTypeTransition ToosTypeRoom_startCountdown(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return TOOS_TYPE_TRANSITION_ERROR;
    if (self->state != TOOS_TYPE_STATE_WAITING) return TOOS_TYPE_TRANSITION_NONE;

    uint64_t deadline = 0;
    if (!make_deadline(now_ms, TOOS_TYPE_COUNTDOWN_MS, &deadline)) return TOOS_TYPE_TRANSITION_ERROR;
    if (!advance_seq(self, NULL)) return TOOS_TYPE_TRANSITION_ERROR;

    self->state = TOOS_TYPE_STATE_COUNTDOWN;
    self->state_start_ms = now_ms;
    self->countdown_ends_at_ms = deadline;

    return TOOS_TYPE_TRANSITION_COUNTDOWN_STARTED;
}

static ToosTypeTransition ToosTypeRoom_tick(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return TOOS_TYPE_TRANSITION_ERROR;

    switch (self->state) {
        case TOOS_TYPE_STATE_WAITING:
        case TOOS_TYPE_STATE_RESULT:
            return TOOS_TYPE_TRANSITION_NONE;

        case TOOS_TYPE_STATE_COUNTDOWN:
            if (now_ms >= self->countdown_ends_at_ms) {
                uint64_t deadline = 0;
                uint64_t game_start_ms = self->countdown_ends_at_ms;
                if (!make_deadline(game_start_ms, TOOS_TYPE_PHASE1_DURATION_MS, &deadline)) return TOOS_TYPE_TRANSITION_ERROR;
                if (!advance_seq(self, NULL)) return TOOS_TYPE_TRANSITION_ERROR;

                self->state = TOOS_TYPE_STATE_PLAYING;
                self->state_start_ms = game_start_ms;
                self->game_ends_at_ms = deadline;
                return TOOS_TYPE_TRANSITION_GAME_STARTED;
            }
            return TOOS_TYPE_TRANSITION_NONE;

        case TOOS_TYPE_STATE_PLAYING:
            if (now_ms >= self->game_ends_at_ms) {
                if (!advance_seq(self, NULL)) return TOOS_TYPE_TRANSITION_ERROR;
                self->state = TOOS_TYPE_STATE_RESULT;
                self->state_start_ms = self->game_ends_at_ms;
                return TOOS_TYPE_TRANSITION_GAME_OVER;
            }
            return TOOS_TYPE_TRANSITION_NONE;

        default:
            return TOOS_TYPE_TRANSITION_ERROR; // 🔴 [마감 1] 비정상 메모리 오염 시 확실한 에러 리턴 복구
    }
}

static ToosTypeTransition ToosTypeRoom_abortGame(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return TOOS_TYPE_TRANSITION_ERROR;
    if (self->state == TOOS_TYPE_STATE_RESULT) return TOOS_TYPE_TRANSITION_NONE;

    if (!advance_seq(self, NULL)) return TOOS_TYPE_TRANSITION_ERROR;
    self->state = TOOS_TYPE_STATE_RESULT;
    self->state_start_ms = now_ms;

    return TOOS_TYPE_TRANSITION_GAME_OVER;
}

static bool ToosTypeRoom_nextSeq(ToosTypeRoom *self, uint64_t *out_seq) {
    return advance_seq(self, out_seq);
}

ToosTypeRoom *new_ToosTypeRoom(uint64_t room_id) {
    ToosTypeRoom *self = (ToosTypeRoom *)calloc(1, sizeof(ToosTypeRoom));
    if (!self) return NULL;

    Object_Init(&self->base, &_ToosTypeRoom_Class);

    self->room_id = room_id;
    self->state = TOOS_TYPE_STATE_WAITING;
    self->seq = 0;
    self->phase = 1;

    self->startCountdown = ToosTypeRoom_startCountdown;
    self->tick = ToosTypeRoom_tick;
    self->abortGame = ToosTypeRoom_abortGame;
    self->nextSeq = ToosTypeRoom_nextSeq;

    return self;
}