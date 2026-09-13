#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TOOS_TYPE_MAX_ROOM_PLAYERS 8
#define TOOS_TYPE_MIN_ROOM_PLAYERS 2    // 🚨 [패치 1] 혼자 시작 금지
#define TOOS_TYPE_MAX_PHASES 4

typedef enum {
    TOOS_TYPE_LANG_NONE = 0,
    TOOS_TYPE_LANG_EN,
    TOOS_TYPE_LANG_KO
} ToosTypeLanguage;

typedef enum {
    TOOS_TYPE_STATE_WAITING = 0,
    TOOS_TYPE_STATE_COUNTDOWN,
    TOOS_TYPE_STATE_PLAYING,
    TOOS_TYPE_STATE_RESULT
} ToosTypeState;

typedef enum {
    TOOS_TYPE_FINISH_NONE = 0,
    TOOS_TYPE_FINISH_FINISHED,
    TOOS_TYPE_FINISH_DNF
} ToosTypeFinishStatus;