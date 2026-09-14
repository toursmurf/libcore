#include "game_room_handler.h"
#include "json.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

extern uint64_t get_monotonic_ms(void);
extern const Class jsonValueClass;
extern Logger *logger;

GameRoom *g_active_room = NULL;

#define JSON_SAFE_UINT_MAX 9007199254740991ULL

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
    if (!isfinite(d)) return 0;
    if (d < (double)min_value || d > (double)max_value) return 0;
    int v = (int)d;
    if ((double)v != d) return 0;
    *out = v;
    return 1;
}

static int json_number_to_uint64(Object *obj, uint64_t *out) {
    if (!out || !is_json_value_type(obj, J_NUMBER)) return 0;
    double d = ((JsonValue*)obj)->number;
    if (!isfinite(d) || d < 0.0 || d > (double)JSON_SAFE_UINT_MAX) return 0;
    uint64_t v = (uint64_t)d;
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

/*  단일 연결에 현재 방 상태를 직격으로 꽂아주는 전용 송신기 */
static void Send_Room_Status(HttpConnection *conn, GameRoom *room) {
    if (!conn || !room) return;

    int red = 0, blue = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (room->player_sessions[i] != 0) {
            if (room->player_teams[i] == TEAM_RED) red++;
            else if (room->player_teams[i] == TEAM_BLUE) blue++;
        }
    }

    const char *state_str = "WAITING";
    if (room->state == ROOM_RUNNING) state_str = "RUNNING";
    else if (room->state == ROOM_FINISHED) state_str = "FINISHED";

    bool can_start = GameRoom_can_start(room);

    uint16_t w = room->board ? room->board->width : 0;
    uint16_t h = room->board ? room->board->height : 0;
    uint64_t seq = room->board ? room->board->sequence : 0;
    uint32_t r_score = room->board ? room->board->red_score : 0;
    uint32_t b_score = room->board ? room->board->blue_score : 0;

    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"ROOM_STATUS\",\"state\":\"%s\",\"redPlayers\":%d,\"bluePlayers\":%d,\"readyToStart\":%s,"
        "\"generation\":%llu,\"width\":%u,\"height\":%u,\"brushSize\":%u,\"duration\":%lu,\"boardSeq\":%llu,\"redScore\":%u,\"blueScore\":%u}",
        state_str,
        red, blue,
        can_start ? "true" : "false",
        (unsigned long long)room->generation,
        (unsigned)w, (unsigned)h,
        (unsigned)room->configured_brush_size,
        (unsigned long)(room->configured_duration_ms / 1000),
        (unsigned long long)seq,
        (unsigned)r_score, (unsigned)b_score
    );

    if (n > 0 && (size_t)n < sizeof(buf)) {
        HttpConnection_ws_send(conn, buf);
    }
}

static void Broadcast_Room_Status(HttpServer *server, GameRoom *room) {
    if (!server || !room) return;

    HttpConnection *c = server->conns_head;
    while (c) {
        HttpConnection *next = c->next;
        PlayerSession *session = (PlayerSession*)c->ws_user_data;

        if (c->mode == CONN_MODE_WS && !c->is_closing && session && session->is_joined && session->room == room) {
            Send_Room_Status(c, room);
        }
        c = next;
    }
}

static void GameRoomHandler_on_game_over(GameRoom *room, void *ctx) {
    HttpServer *server = (HttpServer*)ctx;
    if (!room || !server || !room->board) return;

    char buf[256];
    const char* winner = (room->board->red_score > room->board->blue_score) ? "RED" :
                         (room->board->red_score < room->board->blue_score) ? "BLUE" : "TIE";

    snprintf(buf, sizeof(buf), "{\"type\":\"GAME_OVER\",\"redScore\":%u,\"blueScore\":%u,\"winner\":\"%s\",\"generation\":%llu}",
             (unsigned)room->board->red_score, (unsigned)room->board->blue_score, winner, (unsigned long long)room->generation);
    Broadcast_To_Room(server, room, buf);
    Broadcast_Room_Status(server, room);
}

static void Send_ACK(HttpConnection *conn, int req_id, const char *status) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"ACK\",\"requestId\":%d,\"status\":\"%s\"}", req_id, status);
    if (n > 0 && (size_t)n < sizeof(buf)) HttpConnection_ws_send(conn, buf);
}

static void GameRoomHandler_on_ws_message(HttpConnection *conn, const char *msg, size_t len) {
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

        bool can_reset = false;
        uint8_t idx = GameRoom_assign_player(session->room, session->user_id, (PixelTeam)req_team, &can_reset);

        if (idx >= MAX_PLAYERS) {
            Send_ACK(conn, 0, "ERROR_ROOM_FULL_OR_RUNNING");
            RELEASE((Object*)root);
            return;
        }

        session->player_idx = idx;
        session->is_joined = true;

        char ack_buf[128];
        snprintf(ack_buf, sizeof(ack_buf), "{\"type\":\"JOIN_ACK\",\"playerIdx\":%d,\"team\":%d,\"canReset\":%s}",
                 idx, session->room->player_teams[idx], can_reset ? "true" : "false");
        HttpConnection_ws_send(conn, ack_buf);
        Broadcast_Room_Status(conn->server, session->room);
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 10 && memcmp(type, "RESET_GAME", 10) == 0) {
        int w, h, brush, duration;
        if (!json_number_to_int(root->get(root, "width"), 8, 2048, &w) ||
            !json_number_to_int(root->get(root, "height"), 8, 2048, &h) ||
            !json_number_to_int(root->get(root, "brushSize"), 1, 50, &brush) ||
            !json_number_to_int(root->get(root, "duration"), 1, 3600, &duration)) {
            Send_ACK(conn, 0, "ERROR_INVALID_FIELDS");
            RELEASE((Object*)root);
            return;
        }

        if (GameRoom_reset(session->room, session->user_id, (uint16_t)w, (uint16_t)h, (uint16_t)brush, (uint64_t)duration * 1000)) {
            Send_ACK(conn, 0, "SUCCESS");
            Broadcast_Room_Status(conn->server, session->room);
        } else {
            Send_ACK(conn, 0, "ERROR_RESET_FAILED_OR_NOT_INITIALIZER");
        }
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 10 && memcmp(type, "START_GAME", 10) == 0) {
        if (!session->is_joined) {
            Send_ACK(conn, 0, "ERROR_NOT_JOINED");
            RELEASE((Object*)root);
            return;
        }

        uint64_t now = get_monotonic_ms();
        if (GameRoom_start(session->room, now)) {
            char buf[256];
            snprintf(buf, sizeof(buf), "{\"type\":\"GAME_STARTED\",\"generation\":%llu,\"width\":%u,\"height\":%u,\"brushSize\":%d,\"serverNowMs\":%llu,\"endsAtMs\":%llu}",
                     (unsigned long long)session->room->generation,
                     (unsigned)session->room->board->width,
                     (unsigned)session->room->board->height,
                     (int)session->room->brush_size,
                     (unsigned long long)now,
                     (unsigned long long)session->room->ends_at_ms);

            Broadcast_To_Room(conn->server, session->room, buf);
            Broadcast_Room_Status(conn->server, session->room);
        } else {
            Send_ACK(conn, 0, "ERROR_START_FAILED");
        }
        RELEASE((Object*)root);
        return;
    }

    if (type_len == 5 && memcmp(type, "PAINT", 5) == 0) {
        if (!session->is_joined) { Send_ACK(conn, 0, "ERROR_NOT_JOINED"); RELEASE((Object*)root); return; }

        int req_id;
        uint64_t req_gen;
        if (!json_number_to_int(root->get(root, "requestId"), 0, INT_MAX, &req_id) ||
            !json_number_to_uint64(root->get(root, "generation"), &req_gen)) {
            Send_ACK(conn, 0, "ERROR_INVALID_FIELDS");
            RELEASE((Object*)root);
            return;
        }

        if (req_gen != session->room->generation) {
            Send_ACK(conn, req_id, "REJECT_STALE_GENERATION");
            RELEASE((Object*)root);
            return;
        }

        int x_val, y_val;
        if (!session->room->board) {
            Send_ACK(conn, req_id, "ERROR_BOARD_NOT_READY");
            RELEASE((Object*)root);
            return;
        }

        if (!json_number_to_int(root->get(root, "x"), 0, session->room->board->width - 1, &x_val) ||
            !json_number_to_int(root->get(root, "y"), 0, session->room->board->height - 1, &y_val)) {
            Send_ACK(conn, req_id, "ERROR_INVALID_FIELDS_OR_OUT_OF_BOUNDS");
            RELEASE((Object*)root);
            return;
        }

        uint64_t now = get_monotonic_ms();
        GameRoomPaintResult result = GameRoom_paint(session->room, session->player_idx, req_gen, (uint16_t)x_val, (uint16_t)y_val, now);

        if (result == GAME_ROOM_PAINT_STALE_GENERATION) {
            Send_ACK(conn, req_id, "REJECT_STALE_GENERATION");
        } else if (result == GAME_ROOM_PAINT_TIME_OVER) {
            Send_ACK(conn, req_id, "REJECT_TIME_OVER");
        } else if (result == GAME_ROOM_PAINT_COOLDOWN) {
            Send_ACK(conn, req_id, "REJECT_COOLDOWN");
        } else if (result == GAME_ROOM_PAINT_CHANGED) {
            char buf[512];
            snprintf(buf, sizeof(buf), "{\"type\":\"PAINT_APPLIED\",\"generation\":%llu,\"seq\":%llu,\"x\":%u,\"y\":%u,\"brushSize\":%d,\"team\":%d,\"redScore\":%u,\"blueScore\":%u}",
                     (unsigned long long)session->room->generation,
                     (unsigned long long)session->room->board->sequence,
                     (unsigned)x_val, (unsigned)y_val,
                     (int)session->room->brush_size,
                     (int)session->room->player_teams[session->player_idx],
                     (unsigned)session->room->board->red_score, (unsigned)session->room->board->blue_score);
            Broadcast_To_Room(conn->server, session->room, buf);
            Send_ACK(conn, req_id, "SUCCESS");
        } else if (result == GAME_ROOM_PAINT_UNCHANGED) {
            Send_ACK(conn, req_id, "ACK_UNCHANGED");
        } else {
            Send_ACK(conn, req_id, "ERROR_PAINT_FAILED");
        }

        RELEASE((Object*)root);
        return;
    }

    Send_ACK(conn, 0, "ERROR_UNKNOWN_TYPE");
    RELEASE((Object*)root);
}

static void GameRoomHandler_on_ws_open(HttpConnection *conn) {
    if (!conn || !g_active_room) return;
    PlayerSession *session = new_PlayerSession(g_active_room, (uint64_t)(uintptr_t)conn);
    if (!session) return;
    conn->ws_user_data = session;

    /*  핵심 패치: 웹소켓이 열리는 즉시(JOIN 전이라도) 현재 방 상태 및 설정을 즉시 전송하여 동기화 지연 제거 */
    Send_Room_Status(conn, g_active_room);
}

static void GameRoomHandler_on_ws_close(HttpConnection *conn) {
    if (!conn) return;
    PlayerSession *session = (PlayerSession*)conn->ws_user_data;
    if (session) {
        if (session->is_joined && session->room) {
            GameRoomRemoveResult res = GameRoom_remove_player(session->room, session->player_idx);

            if (res == GAME_ROOM_REMOVE_AUTO_RESET_FAILED) {
                LOG_ERROR(logger, "[GAME] FATAL: Empty room auto-reset failed for room_id: %llu (Deadlock risk)",
                          (unsigned long long)session->room->room_id);
            }
            if (conn->server) {
                Broadcast_Room_Status(conn->server, session->room);
            }
        }
        RELEASE((Object*)session);
        conn->ws_user_data = NULL;
    }
}

bool GameRoomHandler_bind(HttpServer *server, GameRoom *room) {
    if (!server || !room) return false;

    server->on_ws_open = GameRoomHandler_on_ws_open;
    server->on_ws_message = GameRoomHandler_on_ws_message;
    server->on_ws_close = GameRoomHandler_on_ws_close;

    GameRoom_set_on_game_over(room, GameRoomHandler_on_game_over, server);
    g_active_room = room;

    return true;
}

void GameRoomHandler_unbind(HttpServer *server, GameRoom *room) {
    if (!server || !room) return;

    server->on_ws_open = NULL;
    server->on_ws_message = NULL;
    server->on_ws_close = NULL;

    GameRoom_set_on_game_over(room, NULL, NULL);

    for (HttpConnection *c = server->conns_head; c; c = c->next) {
        if (c->ws_user_data) {
            PlayerSession *session = (PlayerSession*)c->ws_user_data;
            if (session->is_joined) {
                GameRoom_remove_player(room, session->player_idx);
            }
            RELEASE((Object*)session);
            c->ws_user_data = NULL;
        }
    }

    GameRoom_close(room);
}