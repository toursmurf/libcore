#ifndef GAME_ROOM_H
#define GAME_ROOM_H

#include "object.h"
#include "pixel_board.h"
#include "event_loop.h"
#include "timer.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PLAYERS 64
#define COOLDOWN_MS 100
#define MAX_BRUSH_SIZE 50

typedef enum {
    ROOM_WAITING,
    ROOM_RUNNING,
    ROOM_FINISHED
} RoomState;

typedef enum {
    GAME_ROOM_PAINT_CHANGED,
    GAME_ROOM_PAINT_UNCHANGED,
    GAME_ROOM_PAINT_COOLDOWN,
    GAME_ROOM_PAINT_ERROR,
    GAME_ROOM_PAINT_TIME_OVER,
    GAME_ROOM_PAINT_STALE_GENERATION
} GameRoomPaintResult;

typedef enum {
    GAME_ROOM_REMOVE_ERROR = -1,
    GAME_ROOM_REMOVE_OK = 0,
    GAME_ROOM_REMOVE_AUTO_RESET_FAILED = 1
} GameRoomRemoveResult;

typedef struct GameRoom GameRoom;

typedef void (*GameRoomGameOverCallback)(GameRoom *room, void *ctx);

struct GameRoom {
    Object base;
    uint64_t room_id;
    PixelBoard *board;

    EventLoop *loop;
    uint64_t generation;

    RoomState state;
    uint16_t brush_size;
    uint64_t duration_ms;
    uint64_t ends_at_ms;

    Timer *game_timer;
    GameRoomGameOverCallback on_game_over_cb;
    void *on_game_over_ctx;

    /*  최초 접속자(Initializer) 세션 ID */
    uint64_t initializer_session;

    /*  서버가 기억하는 공식 게임 설정 (후입장자 공유 및 START 시 활용) */
    uint16_t configured_brush_size;
    uint64_t configured_duration_ms;

    uint64_t player_sessions[MAX_PLAYERS];
    PixelTeam player_teams[MAX_PLAYERS];
    uint64_t cooldown_until[MAX_PLAYERS];
};

GameRoom* new_GameRoom(EventLoop *loop, uint64_t room_id, uint16_t w, uint16_t h);
bool GameRoom_close(GameRoom *room);
void GameRoom_set_on_game_over(GameRoom *room, GameRoomGameOverCallback cb, void *ctx);

uint8_t GameRoom_assign_player(GameRoom *room, uint64_t user_id, PixelTeam req_team, bool *out_can_reset);
GameRoomRemoveResult GameRoom_remove_player(GameRoom *room, uint8_t player_idx);

bool GameRoom_can_start(GameRoom *room);

/*  수정: RESET 시 붓 크기와 게임 시간 설정값도 함께 확정 */
bool GameRoom_reset(GameRoom *room, uint64_t requester_session, uint16_t w, uint16_t h, uint16_t brush, uint64_t duration_ms);

/*  수정: START는 서버가 기억하는 configured 설정으로 즉시 개전 */
bool GameRoom_start(GameRoom *room, uint64_t now_ms);

GameRoomPaintResult GameRoom_paint(GameRoom *room, uint8_t player_idx, uint64_t req_generation, uint16_t x, uint16_t y, uint64_t now_ms);

void GameRoom_trigger_time_over(GameRoom *room);
void GameRoom_check_game_over(GameRoom *room, uint64_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* GAME_ROOM_H */