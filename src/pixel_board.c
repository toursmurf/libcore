#include "pixel_board.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================
   libcore 표준 Object Lifecycle 연동 (v1.7.2 규격)
   ========================================================= */
static void PixelBoard_finalize(Object *obj) {
    PixelBoard *self = (PixelBoard*)obj;
    if (self->pixels) {
        free(self->pixels);
        self->pixels = NULL;
    }
}

/* libcore 클래스 메타데이터 구조체 */
static const Class pixelBoardClass = {
    .name = "PixelBoard",
    .size = sizeof(PixelBoard),
    .finalize = PixelBoard_finalize
};

/* =========================================================
   생성자
   ========================================================= */
PixelBoard* new_PixelBoard(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) return NULL;

    /* GCC 에러 완벽 해결: calloc + Object_Init 방식 사용 */
    PixelBoard *self = (PixelBoard*)calloc(1, sizeof(PixelBoard));
    if (!self) return NULL;

    Object_Init((Object*)self, &pixelBoardClass);

    self->width = width;
    self->height = height;
    self->red_score = 0;
    self->blue_score = 0;

    /* 초기 점수 불변식 세팅 */
    self->empty_count = (uint32_t)width * height;
    self->sequence = 0;

    /* 캔버스 메모리 할당 (0 = TEAM_EMPTY 로 자동 초기화) */
    self->pixels = (uint8_t*)calloc(self->empty_count, sizeof(uint8_t));
    if (!self->pixels) {
        RELEASE((Object*)self);
        return NULL;
    }

    return self;
}

/* =========================================================
   상태 변경 및 조회 API
   ========================================================= */
PixelPaintResult PixelBoard_paint(PixelBoard *self, uint16_t x, uint16_t y, PixelTeam team) {
    /* 1. 인자 및 범위 에러 체크 */
    if (!self || x >= self->width || y >= self->height) {
        return PIXEL_PAINT_ERROR;
    }
    if (team != TEAM_RED && team != TEAM_BLUE) {
        return PIXEL_PAINT_ERROR;
    }

    size_t idx = (size_t)y * self->width + x;
    uint8_t prev = self->pixels[idx];

    /* 2. UNCHANGED 확인 (불변식 유지, seq 변동 없음) */
    if (prev == (uint8_t)team) {
        return PIXEL_PAINT_UNCHANGED;
    }

    /* 3. 불변식 점수 갱신 로직 (O(1)) */
    if (prev == TEAM_EMPTY) {
        self->empty_count--;
        if (team == TEAM_RED) self->red_score++;
        else self->blue_score++;
    }
    else if (prev == TEAM_RED) {  /* RED -> BLUE */
        self->red_score--;
        self->blue_score++;
    }
    else if (prev == TEAM_BLUE) { /* BLUE -> RED */
        self->blue_score--;
        self->red_score++;
    }

    /* 4. 상태 변경 및 버전(sequence) 증가 */
    self->pixels[idx] = (uint8_t)team;
    self->sequence++; /* 단조 증가 Board State Version */

    return PIXEL_PAINT_CHANGED;
}

PixelTeam PixelBoard_get(PixelBoard *self, uint16_t x, uint16_t y) {
    if (!self || x >= self->width || y >= self->height) {
        return TEAM_EMPTY;
    }
    return (PixelTeam)self->pixels[(size_t)y * self->width + x];
}

/* =========================================================
   스냅샷 API
   ========================================================= */
void PixelBoard_snapshot(PixelBoard *self, uint8_t *out, uint64_t *out_seq) {
    if (!self || !out || !out_seq) return;

    /* 현재 버전 저장 */
    *out_seq = self->sequence;

    /* 캔버스 전체 복사 (256KiB) */
    size_t total_pixels = (size_t)self->width * self->height;
    memcpy(out, self->pixels, total_pixels);
}