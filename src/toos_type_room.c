#include "toos_type_room.h"
#include <string.h>

static bool make_deadline(uint64_t now_ms, uint64_t duration_ms, uint64_t *out_deadline) {
    if (!out_deadline) return false;
    if (UINT64_MAX - now_ms < duration_ms) return false;
    *out_deadline = now_ms + duration_ms;
    return true;
}

void ToosTypeRoom_init(ToosTypeRoom *self, uint64_t room_id, const ToosTypeGameProfile *profile) {
    if (!self || !profile) return;
    memset(self, 0, sizeof(ToosTypeRoom));

    self->room_id = room_id;
    self->profile = *profile;
    self->state = TOOS_TYPE_STATE_WAITING;
}

uint32_t ToosTypeRoom_phase_at(const ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return 1;
    if (now_ms <= self->game_start_ms) return 1;

    uint64_t elapsed = now_ms - self->game_start_ms;
    uint64_t cumulative = 0;

    for (uint32_t i = 0; i < TOOS_TYPE_MAX_PHASES; i++) {
        cumulative += self->profile.phases[i].duration_ms;
        if (elapsed < cumulative) return i + 1;
    }
    return TOOS_TYPE_MAX_PHASES;
}

uint64_t ToosTypeRoom_sentence_timeout_ms(const ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return 0;
    uint32_t phase = ToosTypeRoom_phase_at(self, now_ms);
    if (phase < 1 || phase > TOOS_TYPE_MAX_PHASES) return 0;
    return self->profile.phases[phase - 1].input_timeout_ms;
}

bool ToosTypeRoom_start_countdown(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return false;
    if (self->state != TOOS_TYPE_STATE_WAITING) return false;

    uint64_t deadline = 0;
    if (!make_deadline(now_ms, self->profile.countdown_ms, &deadline)) return false;

    self->state = TOOS_TYPE_STATE_COUNTDOWN;
    self->state_start_ms = now_ms;
    self->countdown_ends_at_ms = deadline;
    return true;
}

//  [패치 완료] 다른 상태에서 무단 취소 방지 및 시간값 완벽 초기화
void ToosTypeRoom_cancel_countdown(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return;
    if (self->state != TOOS_TYPE_STATE_COUNTDOWN) return;

    self->state = TOOS_TYPE_STATE_WAITING;
    self->state_start_ms = now_ms;
    self->countdown_ends_at_ms = 0;
    self->game_start_ms = 0;
    self->game_end_ms = 0;
    self->current_phase = 0;
}

ToosTypeRoomTickResult ToosTypeRoom_tick(ToosTypeRoom *self, uint64_t now_ms) {
    if (!self) return TOOS_TYPE_ROOM_TICK_NONE;

    if (self->state == TOOS_TYPE_STATE_COUNTDOWN) {
        if (now_ms >= self->countdown_ends_at_ms) {
            self->game_start_ms = self->countdown_ends_at_ms;

            uint64_t total_duration = 0;
            for (uint32_t i = 0; i < TOOS_TYPE_MAX_PHASES; i++) {
                if (UINT64_MAX - total_duration < self->profile.phases[i].duration_ms) {
                    self->state = TOOS_TYPE_STATE_RESULT;
                    self->state_start_ms = now_ms;
                    return TOOS_TYPE_ROOM_TICK_GAME_ENDED;
                }
                total_duration += self->profile.phases[i].duration_ms;
            }

            uint64_t game_end = 0;
            if (!make_deadline(self->game_start_ms, total_duration, &game_end)) {
                self->state = TOOS_TYPE_STATE_RESULT;
                self->state_start_ms = now_ms;
                return TOOS_TYPE_ROOM_TICK_GAME_ENDED;
            }
            self->game_end_ms = game_end;

            if (now_ms >= self->game_end_ms) {
                self->state = TOOS_TYPE_STATE_RESULT;
                self->state_start_ms = self->game_end_ms;
                self->current_phase = TOOS_TYPE_MAX_PHASES;
                return TOOS_TYPE_ROOM_TICK_GAME_ENDED;
            }

            self->state = TOOS_TYPE_STATE_PLAYING;
            self->state_start_ms = self->game_start_ms;
            self->current_phase = ToosTypeRoom_phase_at(self, now_ms);
            return TOOS_TYPE_ROOM_TICK_GAME_STARTED;
        }
    }
    else if (self->state == TOOS_TYPE_STATE_PLAYING) {
        if (now_ms >= self->game_end_ms) {
            self->state = TOOS_TYPE_STATE_RESULT;
            self->state_start_ms = self->game_end_ms;
            return TOOS_TYPE_ROOM_TICK_GAME_ENDED;
        }

        uint32_t new_phase = ToosTypeRoom_phase_at(self, now_ms);
        if (new_phase != self->current_phase) {
            self->current_phase = new_phase;
            return TOOS_TYPE_ROOM_TICK_PHASE_CHANGED;
        }
    }
    return TOOS_TYPE_ROOM_TICK_NONE;
}