#pragma once
#include "toos_type_types.h"

#define TOOS_TYPE_MIN_GAME_MINUTES 1u
#define TOOS_TYPE_MAX_GAME_MINUTES 5u

typedef struct {
    uint32_t duration_ms;
    uint32_t input_timeout_ms;
    uint32_t correct_score;
} ToosTypePhaseProfile;

typedef struct {
    uint32_t countdown_ms;
    uint64_t random_seed;
    uint32_t play_score_per_sec;
    uint32_t accuracy_score_multiplier;
    ToosTypePhaseProfile phases[TOOS_TYPE_MAX_PHASES];
} ToosTypeGameProfile;

typedef enum {
    TOOS_TYPE_ROOM_TICK_NONE = 0,
    TOOS_TYPE_ROOM_TICK_GAME_STARTED,
    TOOS_TYPE_ROOM_TICK_PHASE_CHANGED,
    TOOS_TYPE_ROOM_TICK_GAME_ENDED
} ToosTypeRoomTickResult;

typedef struct ToosTypeRoom {
    uint64_t room_id;
    ToosTypeGameProfile profile;
    ToosTypeState state;

    ToosTypeLanguage language;
    bool language_locked;
    uint32_t host_player_no;

    uint64_t state_start_ms;
    uint64_t game_start_ms;
    uint64_t game_end_ms;
    uint64_t countdown_ends_at_ms;

    uint32_t current_phase;
} ToosTypeRoom;

void ToosTypeRoom_init(ToosTypeRoom *self,  uint64_t room_id,  const ToosTypeGameProfile *profile);
uint32_t ToosTypeRoom_phase_at(const ToosTypeRoom *self, uint64_t now_ms);
uint64_t ToosTypeRoom_sentence_timeout_ms(const ToosTypeRoom *self, uint64_t now_ms);
uint64_t ToosTypeRoom_total_duration_ms(const ToosTypeRoom *self);

/*
 * WAITING 상태에서만 변경 가능.
 * 1~5분을 3개의 Phase에 동일하게 배분한다.
 *
 * Host 권한 및 READY 상태 검증은 GameContext/WS 계층의 책임.
 */
bool ToosTypeRoom_set_duration_minutes(ToosTypeRoom *self,  uint32_t minutes);
bool ToosTypeRoom_start_countdown(ToosTypeRoom *self, uint64_t now_ms);
void ToosTypeRoom_cancel_countdown(ToosTypeRoom *self,  uint64_t now_ms);
ToosTypeRoomTickResult ToosTypeRoom_tick(ToosTypeRoom *self, uint64_t now_ms);