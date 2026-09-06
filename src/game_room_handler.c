#include "game_room_handler.h"
#include "json.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern uint64_t get_monotonic_ms(void);
extern GameRoom *g_active_room;
extern const Class jsonValueClass;
extern Logger *logger;

static void PlayerSession_finalize(Object *obj) {
    (void)obj;
}

static const Class _PlayerSession_Class = {
    .name = "PlayerSession",
    .size = sizeof(PlayerSession),
    .finalize = PlayerSession_finalize
};

PlayerSession* new_PlayerSession(GameRoom *room, uint64_t user_id) {
    PlayerSession *session = (PlayerSession*)calloc(1, sizeof(PlayerSession));
    if (!session) return NULL;
    Object_Init((Object*)session, &_PlayerSession_Class);
    session->room = room;
    session->user_id = user_id;
    session->player_idx = MAX_PLAYERS;
    session->is_joined = false;
    return session;
}

static inline int is_json_value_type(Object *obj, int expected_type) {
    if (!obj) return 0;
    const Class *cls = *(const Class**)obj;
    if (cls != &jsonValueClass) return 0;
    return ((int)((JsonValue*)obj)->type == expected_type);
}

static int json_number_to_int(Object *obj, int min_value, int max_value, int *out) {
    if (!out || !is_json_value_type(obj, J_NUMBER)) return 0;
    double d = ((JsonValue*)obj)->number;
    if (d < (double)min_value || d > (double)max_value) return 0;
    int v = (int)d;
    if ((double)v != d) return 0;
    *out = v;
    return 1;
}

static void Broadcast_To_Room(HttpServer* server, GameRoom* room, const char* buf) {
    if (!server) return;
    HttpConnection *c = server->conns_head;
    while (c) {
        HttpConnection *next = c->next;
        PlayerSession *s = (PlayerSession*)c->ws_user_data;
        if (c->mode == CONN_MODE_WS && !c->is_closing && s && s->is_joined && s->room == room) {
            HttpConnection_ws_send(c, buf);
        }
        c = next;
    }
}

void GameRoomHandler_on_game_over(GameRoom *room, HttpServer *server) {
    if (!room || room->game_over_broadcasted || room->state != ROOM_FINISHED) return;
    room->game_over_broadcasted = true;

    char buf[256];
    const char* winner = (room->board->red_score > room->board->blue_score) ? "RED" :
                         (room->board->red_score < room->board->blue_score) ? "BLUE" : "TIE";

    snprintf(buf, sizeof(buf), "{\"type\":\"GAME_OVER\",\"redScore\":%u,\"blueScore\":%u,\"winner\":\"%s\"}",
             (unsigned)room->board->red_score, (unsigned)room->board->blue_score, winner);

    Broadcast_To_Room(server, room, buf);
    LOG_INFO(logger, "[GAME] Game Over! Winner: %s", winner);
}

static void refresh_game_state(HttpConnection *conn, PlayerSession *session) {
    GameRoom *room = session->room;
    if (!room || room->state != ROOM_RUNNING) return;

    RoomState old_state = room->state;
    GameRoom_check_game_over(room, get_monotonic_ms());

    if (old_state == ROOM_RUNNING && room->state == ROOM_FINISHED) {
        GameRoomHandler_on_game_over(room, conn->server);
    }
}

static void Send_ACK(HttpConnection *conn, int req_id, const char *status) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"ACK\",\"requestId\":%d,\"status\":\"%s\"}", req_id, status);
    if (n > 0 && (size_t)n < sizeof(buf)) HttpConnection_ws_send(conn, buf);
}

void GameRoomHandler_on_ws_message(HttpConnection *conn, const char *msg, size_t len) {
    if (!conn || !msg) return;
    PlayerSession *session = (PlayerSession*)conn->ws_user_data;
    if (!session || !session->room) return;

    if (len == 0 || len > 8191 || memchr(msg, '\0', len) != NULL) {
        Send_ACK(conn, 0, "ERROR_INVALID_JSON");
        return;
    }

    char *json_buf = (char*)malloc(len + 1);
    if (!json_buf) { Send_ACK(conn, 0, "ERROR_INTERNAL"); return; }
    memcpy(json_buf, msg, len);
    json_buf[len] = '\0';

    ParseResult parsed = parse_JSON(json_buf);
    free(json_buf);

    if (!parsed.success || !parsed.root) {
        Send_ACK(conn, 0, "ERROR_INVALID_JSON");
        return;
    }
    JSONNode *root = parsed.root;

    refresh_game_state(conn, session);

    size_t type_len = 0;
    const char *type = root->getStringLen(root, "type", &type_len);
    if (!type) {
        Send_ACK(conn, 0, "ERROR_MISSING_TYPE");
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 9 && memcmp(type, "JOIN_GAME", 9) == 0) {
        int req_team;
        if (!json_number_to_int(root->get(root, "team"), 1, 2, &req_team)) {
             Send_ACK(conn, 0, "ERROR_INVALID_TEAM"); RELEASE((Object*)root); return;
        }

        bool is_host = false;
        uint8_t idx = GameRoom_assign_player(session->room, session->user_id, (PixelTeam)req_team, &is_host);

        if (idx >= MAX_PLAYERS) {
            Send_ACK(conn, 0, "ERROR_ROOM_FULL_OR_RUNNING");
            RELEASE((Object*)root);
            return;
        }

        session->player_idx = idx;
        session->is_joined = true;

        char ack_buf[128];
        snprintf(ack_buf, sizeof(ack_buf), "{\"type\":\"JOIN_ACK\",\"playerIdx\":%d,\"team\":%d,\"isHost\":%s}",
                 idx, session->room->player_teams[idx], is_host ? "true" : "false");
        HttpConnection_ws_send(conn, ack_buf);
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 10 && memcmp(type, "START_GAME", 10) == 0) {
        if (session->room->host_session != session->user_id) {
            Send_ACK(conn, 0, "ERROR_NOT_HOST"); RELEASE((Object*)root); return;
        }

        int w, h, brush, duration;
        if (!json_number_to_int(root->get(root, "width"), 8, 2048, &w) ||
            !json_number_to_int(root->get(root, "height"), 8, 2048, &h) ||
            !json_number_to_int(root->get(root, "brushSize"), 1, 50, &brush) ||
            !json_number_to_int(root->get(root, "duration"), 10, 3600, &duration)) {
            Send_ACK(conn, 0, "ERROR_INVALID_FIELDS");
            RELEASE((Object*)root);
            return;
        }

        if (GameRoom_start(session->room, (uint16_t)w, (uint16_t)h, (uint16_t)brush, (uint64_t)duration * 1000, get_monotonic_ms())) {
            char buf[256];
            snprintf(buf, sizeof(buf), "{\"type\":\"GAME_STARTED\",\"width\":%d,\"height\":%d,\"brushSize\":%d,\"durationMs\":%d}",
                     w, h, brush, duration * 1000);
            Broadcast_To_Room(conn->server, session->room, buf);
            LOG_INFO(logger, "[GAME] Started %dx%d (Brush: %d, Dur: %ds)", w, h, brush, duration);
        } else {
            Send_ACK(conn, 0, "ERROR_ALREADY_RUNNING_OR_ALLOC_FAILED");
        }
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 15 && memcmp(type, "CHECK_GAME_OVER", 15) == 0) {
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 5 && memcmp(type, "PAINT", 5) == 0) {
        if (!session->is_joined) { Send_ACK(conn, 0, "ERROR_NOT_JOINED"); RELEASE((Object*)root); return; }

        int x_val, y_val, req_id;
        if (!json_number_to_int(root->get(root, "x"), 0, INT_MAX, &x_val) ||
            !json_number_to_int(root->get(root, "y"), 0, INT_MAX, &y_val) ||
            !json_number_to_int(root->get(root, "requestId"), 0, INT_MAX, &req_id)) {
            Send_ACK(conn, 0, "ERROR_INVALID_FIELDS");
            RELEASE((Object*)root);
            return;
        }

        if (!session->room->board ||
            x_val < 0 || x_val >= session->room->board->width ||
            y_val < 0 || y_val >= session->room->board->height) {
            Send_ACK(conn, req_id, "ERROR_OUT_OF_BOUNDS");
            RELEASE((Object*)root);
            return;
        }

        uint64_t now = get_monotonic_ms();
        GameRoomPaintResult result = GameRoom_paint(session->room, session->player_idx, (uint16_t)x_val, (uint16_t)y_val, now);

        if (result == GAME_ROOM_PAINT_TIME_OVER) {
            GameRoomHandler_on_game_over(session->room, conn->server);
            Send_ACK(conn, req_id, "REJECT_TIME_OVER");
            RELEASE((Object*)root);
            return;
        }

        if (result == GAME_ROOM_PAINT_CHANGED) {
            char buf[256];
            snprintf(buf, sizeof(buf), "{\"type\":\"PIXEL_CHANGED\",\"seq\":%llu,\"x\":%u,\"y\":%u,\"team\":%d,\"redScore\":%u,\"blueScore\":%u}",
                     (unsigned long long)session->room->board->sequence, (unsigned)x_val, (unsigned)y_val,
                     (int)session->room->player_teams[session->player_idx],
                     (unsigned)session->room->board->red_score, (unsigned)session->room->board->blue_score);
            Broadcast_To_Room(conn->server, session->room, buf);
            Send_ACK(conn, req_id, "SUCCESS");

            /* 🚨 마지막 픽셀로 보드가 꽉 찼을 때 즉시 GAME_OVER 브로드캐스트 전송! */
            if (session->room->state == ROOM_FINISHED) {
                GameRoomHandler_on_game_over(session->room, conn->server);
            }
        } else if (result == GAME_ROOM_PAINT_UNCHANGED) {
            Send_ACK(conn, req_id, "ACK_UNCHANGED");
        } else if (result == GAME_ROOM_PAINT_COOLDOWN) {
            Send_ACK(conn, req_id, "REJECT_COOLDOWN");
        }

        RELEASE((Object*)root);
        return;
    }

    Send_ACK(conn, 0, "ERROR_UNKNOWN_TYPE");
    RELEASE((Object*)root);
}

void GameRoomHandler_on_ws_open(HttpConnection *conn) {
    if (!conn) return;
    PlayerSession *session = new_PlayerSession(g_active_room, (uint64_t)(uintptr_t)conn);
    if (!session) return;
    conn->ws_user_data = session;
}

void GameRoomHandler_on_ws_close(HttpConnection *conn) {
    if (!conn) return;
    PlayerSession *session = (PlayerSession*)conn->ws_user_data;
    if (session) {
        if (session->is_joined && session->room) {
            bool was_host = (session->user_id == session->room->host_session);
            uint64_t new_host_id = GameRoom_remove_player(session->room, session->player_idx);

            if (was_host && new_host_id != 0 && conn->server) {
                for (HttpConnection *c = conn->server->conns_head; c; c = c->next) {
                    PlayerSession *s = (PlayerSession*)c->ws_user_data;
                    if (s && s->user_id == new_host_id && !c->is_closing) {
                        HttpConnection_ws_send(c, "{\"type\":\"HOST_CHANGED\",\"isHost\":true}");
                        LOG_INFO(logger, "[GAME] Host migrated to a new player.");
                        break;
                    }
                }
            }
        }
        RELEASE((Object*)session);
        conn->ws_user_data = NULL;
    }
}

void GameRoomHandler_cleanup_sessions(HttpServer *server) {
    if (!server) return;
    for (HttpConnection *c = server->conns_head; c; c = c->next) {
        if (c->ws_user_data) {
            RELEASE((Object*)c->ws_user_data);
            c->ws_user_data = NULL;
        }
    }
}