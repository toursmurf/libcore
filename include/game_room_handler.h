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

void GameRoomHandler_on_ws_message(HttpConnection *conn, const char *msg, size_t len);
void GameRoomHandler_on_ws_open(HttpConnection *conn);
void GameRoomHandler_on_ws_close(HttpConnection *conn);
void GameRoomHandler_cleanup_sessions(HttpServer *server);
void GameRoomHandler_on_game_over(GameRoom *room, HttpServer *server);

#ifdef __cplusplus
}
#endif
#endif /* GAME_ROOM_HANDLER_H */