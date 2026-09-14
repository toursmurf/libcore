#include "game_room.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

static bool GameRoom_detach_timer(GameRoom *room) {
    if (!room || !room->game_timer) {
        return true;
    }

    Timer *timer = room->game_timer;
    if (room->loop->removeTimer(room->loop, timer) != 0) {
        return false;
    }

    room->game_timer = NULL;
    RELEASE((Object*)timer);
    return true;
}

bool GameRoom_close(GameRoom *room) {
    if (!room) {
        return false;
    }

    if (!GameRoom_detach_timer(room)) {
        return false;
    }

    room->on_game_over_cb = NULL;
    room->on_game_over_ctx = NULL;
    return true;
}

static bool _GameRoom_force_reset(GameRoom *room, uint16_t w, uint16_t h, uint16_t brush, uint64_t duration_ms) {
    PixelBoard *new_board = NULL;

    if (!room || w == 0 || h == 0) {
        return false;
    }

    if (!room->board || room->board->width != w || room->board->height != h) {
        new_board = new_PixelBoard(w, h);
        if (!new_board) {
            return false;
        }
    }

    if (!GameRoom_detach_timer(room)) {
        if (new_board) {
            RELEASE((Object*)new_board);
        }
        return false;
    }

    if (new_board) {
        if (room->board) {
            RELEASE((Object*)room->board);
        }
        room->board = new_board;
    } else {
        PixelBoard_clear(room->board);
    }

    room->generation++;
    room->brush_size = 0;
    room->duration_ms = 0;
    room->ends_at_ms = 0;

    /*  서버 공식 설정 기억 반영 */
    room->configured_brush_size = brush;
    room->configured_duration_ms = duration_ms;

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        room->cooldown_until[i] = 0;
    }
    room->state = ROOM_WAITING;

    return true;
}

static void on_room_timer_expired(void *user_data) {
    GameRoom *room = (GameRoom*)user_data;
    GameRoom_trigger_time_over(room);
}

static void GameRoom_finalize(Object *obj) {
    GameRoom *self = (GameRoom*)obj;
    assert(self->game_timer == NULL);

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

GameRoom* new_GameRoom(EventLoop *loop, uint64_t room_id, uint16_t w, uint16_t h) {
    if (!loop || !loop->addTimer || !loop->removeTimer || w == 0 || h == 0) {
        return NULL;
    }

    GameRoom *room = (GameRoom*)calloc(1, sizeof(GameRoom));
    if (!room) return NULL;

    Object_Init((Object*)room, &_GameRoom_Class);
    room->loop = loop;
    room->room_id = room_id;
    room->state = ROOM_WAITING;
    room->generation = 1;

    /* 기본 설정값 초기화 (붓: 5, 시간: 180초 = 180000ms) */
    room->configured_brush_size = 5;
    room->configured_duration_ms = 180000;

    room->board = new_PixelBoard(w, h);
    if (!room->board) {
        RELEASE((Object*)room);
        return NULL;
    }

    return room;
}

void GameRoom_set_on_game_over(GameRoom *room, GameRoomGameOverCallback cb, void *ctx) {
    if (room) {
        room->on_game_over_cb = cb;
        room->on_game_over_ctx = ctx;
    }
}

uint8_t GameRoom_assign_player(GameRoom *room, uint64_t user_id, PixelTeam req_team, bool *out_can_reset) {
    if (out_can_reset) *out_can_reset = false;

    if (!room || user_id == 0 || (req_team != TEAM_RED && req_team != TEAM_BLUE)) return MAX_PLAYERS;
    if (room->state != ROOM_WAITING) return MAX_PLAYERS;

    int red_count = 0, blue_count = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] != 0 && room->player_sessions[i] != user_id) {
            if (room->player_teams[i] == TEAM_RED) red_count++;
            else if (room->player_teams[i] == TEAM_BLUE) blue_count++;
        }
    }

    if (req_team == TEAM_RED && red_count >= 3) return MAX_PLAYERS;
    if (req_team == TEAM_BLUE && blue_count >= 3) return MAX_PLAYERS;

    if (room->initializer_session == 0) {
        room->initializer_session = user_id;
    }

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] == user_id) {
            room->player_teams[i] = req_team;
            if (out_can_reset) *out_can_reset = (room->initializer_session == user_id);
            return i;
        }
    }

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] == 0) {
            room->player_sessions[i] = user_id;
            room->player_teams[i] = req_team;
            room->cooldown_until[i] = 0;

            if (out_can_reset) *out_can_reset = (room->initializer_session == user_id);
            return i;
        }
    }
    return MAX_PLAYERS;
}

GameRoomRemoveResult GameRoom_remove_player(GameRoom *room, uint8_t player_idx) {
    if (!room || player_idx >= MAX_PLAYERS) return GAME_ROOM_REMOVE_ERROR;
    if (room->player_sessions[player_idx] == 0) return GAME_ROOM_REMOVE_ERROR;

    room->player_sessions[player_idx] = 0;
    room->player_teams[player_idx] = (PixelTeam)0;
    room->cooldown_until[player_idx] = 0;

    int remaining = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] != 0) remaining++;
    }

    /*  방이 완전히 비었을 때의 리셋 순서 보정 (Failure Atomicity 강화) */
    if (remaining == 0) {
        if (room->state != ROOM_WAITING) {
            if (!_GameRoom_force_reset(room, room->board->width, room->board->height, room->configured_brush_size, room->configured_duration_ms)) {
                return GAME_ROOM_REMOVE_AUTO_RESET_FAILED;
            }
        }
        room->initializer_session = 0;
    }

    return GAME_ROOM_REMOVE_OK;
}

bool GameRoom_can_start(GameRoom *room) {
    if (!room || room->state != ROOM_WAITING) return false;

    int red_count = 0, blue_count = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] != 0) {
            if (room->player_teams[i] == TEAM_RED) red_count++;
            else if (room->player_teams[i] == TEAM_BLUE) blue_count++;
        }
    }

    return (red_count >= 1 && blue_count >= 1);
}

bool GameRoom_reset(GameRoom *room, uint64_t requester_session, uint16_t w, uint16_t h, uint16_t brush, uint64_t duration_ms) {
    if (!room || w == 0 || h == 0 || brush == 0 || duration_ms == 0) return false;
    if (room->state == ROOM_RUNNING) return false;
    if (requester_session == 0 || requester_session != room->initializer_session) return false;

    return _GameRoom_force_reset(room, w, h, brush, duration_ms);
}

/*  서버가 기억하는 configured 설정으로 타이머 개전 */
bool GameRoom_start(GameRoom *room, uint64_t now_ms) {
    if (!room) return false;
    if (!GameRoom_can_start(room)) return false;
    if (room->game_timer != NULL) return false;

    uint16_t brush = room->configured_brush_size;
    uint64_t dur_ms = room->configured_duration_ms;

    if (brush == 0 || brush > MAX_BRUSH_SIZE || dur_ms == 0) return false;
    if (dur_ms > (uint64_t)LONG_MAX) return false;
    if (dur_ms > UINT64_MAX - now_ms) return false;

    Timer *t = new_Timer((long)dur_ms, false, on_room_timer_expired, room);
    if (!t) return false;

    if (room->loop->addTimer(room->loop, t) != 0) {
        RELEASE((Object*)t);
        return false;
    }

    room->game_timer = t;

    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        room->cooldown_until[i] = 0;
    }

    room->brush_size = brush;
    room->duration_ms = dur_ms;
    room->ends_at_ms = now_ms + dur_ms;
    room->state = ROOM_RUNNING;

    return true;
}

void GameRoom_trigger_time_over(GameRoom *room) {
    if (!room) return;
    if (room->state != ROOM_RUNNING) return;

    room->state = ROOM_FINISHED;
    if (room->on_game_over_cb) {
        room->on_game_over_cb(room, room->on_game_over_ctx);
    }
}

void GameRoom_check_game_over(GameRoom *room, uint64_t now_ms) {
    if (!room || room->state != ROOM_RUNNING) return;

    if (now_ms >= room->ends_at_ms) {
        GameRoom_trigger_time_over(room);
    }
}

GameRoomPaintResult GameRoom_paint(GameRoom *room, uint8_t player_idx, uint64_t req_generation, uint16_t x, uint16_t y, uint64_t now_ms) {
    if (!room || !room->board || player_idx >= MAX_PLAYERS) return GAME_ROOM_PAINT_ERROR;

    if (req_generation != room->generation) {
        return GAME_ROOM_PAINT_STALE_GENERATION;
    }

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

    return painted_any ? GAME_ROOM_PAINT_CHANGED : GAME_ROOM_PAINT_UNCHANGED;
}