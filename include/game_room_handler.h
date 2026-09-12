#ifndef GAME_ROOM_HANDLER_H
#define GAME_ROOM_HANDLER_H

#include "game_room.h"
#include "http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlayerSession PlayerSession;

struct PlayerSession {
    Object base;
    GameRoom *room;
    uint64_t  user_id;
    uint8_t   player_idx;
    bool      is_joined;
};

PlayerSession* new_PlayerSession(GameRoom *room, uint64_t user_id);

bool GameRoomHandler_bind(HttpServer *server, GameRoom *room);
void GameRoomHandler_unbind(HttpServer *server, GameRoom *room);

#ifdef __cplusplus
}
#endif
#endif /* GAME_ROOM_HANDLER_H */