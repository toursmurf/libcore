#ifndef PIXEL_BOARD_H
#define PIXEL_BOARD_H

#include "object.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 팀 정의 */
typedef enum {
    TEAM_EMPTY = 0,
    TEAM_RED   = 1,
    TEAM_BLUE  = 2
} PixelTeam;

/* Paint 결과 (v0.2 규격) */
typedef enum {
    PIXEL_PAINT_ERROR     = -1,  /* 범위 초과 등 에러 */
    PIXEL_PAINT_UNCHANGED =  0,  /* 같은 팀 → seq 증가 X, broadcast X */
    PIXEL_PAINT_CHANGED   =  1   /* 성공 → seq 증가, broadcast O */
} PixelPaintResult;

typedef struct PixelBoard PixelBoard;

/* PixelBoard 구조체 (Mutex 없음 - Single Writer 보장) */
struct PixelBoard {
    Object    base;           /* libcore Object lifecycle 관리 */

    uint16_t  width;
    uint16_t  height;
    uint8_t  *pixels;         /* width × height 배열 (1D) */

    uint32_t  red_score;
    uint32_t  blue_score;
    uint32_t  empty_count;    /* 불변식 유지용: R+B+E = W*H */
    uint64_t  sequence;       /* Board State Version */
};

/* API */
PixelBoard* new_PixelBoard(uint16_t width, uint16_t height); /* [OWNED] */

PixelPaintResult PixelBoard_paint(PixelBoard *self, uint16_t x, uint16_t y, PixelTeam team);
PixelTeam PixelBoard_get(PixelBoard *self, uint16_t x, uint16_t y);

/*
 * DANGER: MUST be called from the GameRoom single-writer thread.
 * Concurrent paint/snapshot access is undefined behavior!
 */
void PixelBoard_snapshot(PixelBoard *self, uint8_t *out, uint64_t *out_seq);

#ifdef __cplusplus
}
#endif

#endif /* PIXEL_BOARD_H */