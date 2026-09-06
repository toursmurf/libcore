#ifndef GAME_ROOM_H
#define GAME_ROOM_H

#include "object.h"
#include "pixel_board.h"
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
    GAME_ROOM_PAINT_TIME_OVER
} GameRoomPaintResult;

typedef struct GameRoom GameRoom;

struct GameRoom {
    Object base;
    uint64_t room_id;
    PixelBoard *board;

    RoomState state;
    uint16_t brush_size;
    uint64_t duration_ms;
    uint64_t ends_at_ms;
    bool game_over_broadcasted;

    uint64_t host_session;
    uint64_t player_sessions[MAX_PLAYERS];
    PixelTeam player_teams[MAX_PLAYERS];
    uint64_t cooldown_until[MAX_PLAYERS];
};

GameRoom* new_GameRoom(uint64_t room_id, uint16_t w, uint16_t h);
uint8_t GameRoom_assign_player(GameRoom *room, uint64_t user_id, PixelTeam req_team, bool *out_is_host);
uint64_t GameRoom_remove_player(GameRoom *room, uint8_t player_idx);
bool GameRoom_start(GameRoom *room, uint16_t w, uint16_t h, uint16_t brush, uint64_t dur_ms, uint64_t now_ms);
GameRoomPaintResult GameRoom_paint(GameRoom *room, uint8_t player_idx, uint16_t x, uint16_t y, uint64_t now_ms);
void GameRoom_check_game_over(GameRoom *room, uint64_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* GAME_ROOM_H */