#include "game_room.h"
#include <stdlib.h>
#include <string.h>

static void GameRoom_finalize(Object *obj) {
    GameRoom *self = (GameRoom*)obj;
    if (self->board) {
        RELEASE((Object*)self->board);
        self->board = NULL;
    }
}

static const Class _GameRoom_Class = {
    .name = "GameRoom",
    .size = sizeof(GameRoom),
    .finalize = GameRoom_finalize
};

GameRoom* new_GameRoom(uint64_t room_id, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) return NULL;

    GameRoom *room = (GameRoom*)calloc(1, sizeof(GameRoom));
    if (!room) return NULL;

    Object_Init((Object*)room, &_GameRoom_Class);
    room->room_id = room_id;
    room->state = ROOM_WAITING;

    room->board = new_PixelBoard(w, h);
    if (!room->board) {
        RELEASE((Object*)room);
        return NULL;
    }

    return room;
}

uint8_t GameRoom_assign_player(GameRoom *room, uint64_t user_id, PixelTeam req_team, bool *out_is_host) {
    if (!room || user_id == 0 || (req_team != TEAM_RED && req_team != TEAM_BLUE)) return MAX_PLAYERS;
    if (room->state == ROOM_RUNNING) return MAX_PLAYERS;

    int red_count = 0, blue_count = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] != 0 && room->player_sessions[i] != user_id) {
            if (room->player_teams[i] == TEAM_RED) red_count++;
            else if (room->player_teams[i] == TEAM_BLUE) blue_count++;
        }
    }

    if (req_team == TEAM_RED && red_count >= 3) return MAX_PLAYERS;
    if (req_team == TEAM_BLUE && blue_count >= 3) return MAX_PLAYERS;

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] == user_id) {
            room->player_teams[i] = req_team;
            if (out_is_host) *out_is_host = (room->host_session == user_id);
            return i;
        }
    }

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] == 0) {
            room->player_sessions[i] = user_id;
            room->player_teams[i] = req_team;
            room->cooldown_until[i] = 0;

            if (room->host_session == 0) room->host_session = user_id;
            if (out_is_host) *out_is_host = (room->host_session == user_id);
            return i;
        }
    }
    return MAX_PLAYERS;
}

uint64_t GameRoom_remove_player(GameRoom *room, uint8_t player_idx) {
    if (!room || player_idx >= MAX_PLAYERS) return 0;
    if (room->player_sessions[player_idx] == 0) return 0;

    if (room->player_sessions[player_idx] == room->host_session) {
        room->host_session = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (i != player_idx && room->player_sessions[i] != 0) {
                room->host_session = room->player_sessions[i];
                break;
            }
        }
    }

    room->player_sessions[player_idx] = 0;
    room->player_teams[player_idx] = (PixelTeam)0;
    room->cooldown_until[player_idx] = 0;

    return room->host_session;
}

bool GameRoom_start(GameRoom *room, uint16_t w, uint16_t h, uint16_t brush, uint64_t dur_ms, uint64_t now_ms) {
    if (!room || room->state == ROOM_RUNNING) return false;
    if (w == 0 || h == 0 || brush == 0 || dur_ms == 0) return false;
    if (brush > MAX_BRUSH_SIZE) return false;
    if (dur_ms > UINT64_MAX - now_ms) return false;

    PixelBoard *new_board = new_PixelBoard(w, h);
    if (!new_board) return false;

    if (room->board) RELEASE((Object*)room->board);
    room->board = new_board;

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        room->cooldown_until[i] = 0;
    }

    room->brush_size = brush;
    room->duration_ms = dur_ms;
    room->ends_at_ms = now_ms + dur_ms;
    room->game_over_broadcasted = false;
    room->state = ROOM_RUNNING;

    return true;
}

/* 🚨 통합 게임 종료 판정: 제한시간 종료 OR 빈칸 0개 */
void GameRoom_check_game_over(GameRoom *room, uint64_t now_ms) {
    if (!room || room->state != ROOM_RUNNING) return;

    bool time_over = (now_ms >= room->ends_at_ms);
    bool board_full = (room->board && room->board->empty_count == 0);

    if (time_over || board_full) {
        room->state = ROOM_FINISHED;
    }
}

GameRoomPaintResult GameRoom_paint(GameRoom *room, uint8_t player_idx, uint16_t x, uint16_t y, uint64_t now_ms) {
    if (!room || !room->board || player_idx >= MAX_PLAYERS) return GAME_ROOM_PAINT_ERROR;
    if (room->player_sessions[player_idx] == 0) return GAME_ROOM_PAINT_ERROR;

    if (x >= room->board->width || y >= room->board->height) {
        return GAME_ROOM_PAINT_ERROR;
    }

    GameRoom_check_game_over(room, now_ms);
    if (room->state != ROOM_RUNNING) return GAME_ROOM_PAINT_TIME_OVER;

    if (now_ms < room->cooldown_until[player_idx]) return GAME_ROOM_PAINT_COOLDOWN;
    room->cooldown_until[player_idx] = now_ms + COOLDOWN_MS;

    PixelTeam team = room->player_teams[player_idx];
    bool painted_any = false;

    int bs = (int)room->brush_size;
    int offset = bs / 2;
    for (int dy = 0; dy < bs; dy++) {
        for (int dx = 0; dx < bs; dx++) {
            int cx = (int)x - offset + dx;
            int cy = (int)y - offset + dy;
            if (cx >= 0 && cx < room->board->width && cy >= 0 && cy < room->board->height) {
                if (PixelBoard_paint(room->board, (uint16_t)cx, (uint16_t)cy, team) == PIXEL_PAINT_CHANGED) {
                    painted_any = true;
                }
            }
        }
    }

    /* 🚨 마지막 붓질로 보드가 꽉 찼는지 즉시 검사! */
    if (painted_any) {
        GameRoom_check_game_over(room, now_ms);
    }

    return painted_any ? GAME_ROOM_PAINT_CHANGED : GAME_ROOM_PAINT_UNCHANGED;
}