#pragma once

#include "http_server.h"
#include "toos_type_room.h"
#include "toos_type_spawn.h"
#include "toos_type_judge.h"
#include "json.h"
#include <sqlite3.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TOOS_TYPE_MAX_ROOM_PLAYERS 8

// 플레이어 세션 구조체 (고정 슬롯 방식)
typedef struct {
    bool in_use;
    bool ready;
    uint64_t player_id;
    HttpConnection *conn; /* [BORROWED] 커넥션 생명주기 동안 고정 보장 */
    ToosTypePlayerState state;
} ToosTypeClientSession;

// 토스 타입 게임 컨텍스트
typedef struct {
    ToosTypeRoom *room;                    /* [OWNED] 권한(Authoritative) FSM 객체 (ARC) */
    ToosTypeSpawnEngine spawn;             /* 문장 스폰 엔진 */
    sqlite3 *db;                           /* [BORROWED] SQLite DB 핸들 */

    ToosTypeClientSession players[TOOS_TYPE_MAX_ROOM_PLAYERS];
    uint32_t active_player_count;

    ToosTypeLanguage language;             /* v0.3 EN 모델 고정 */
    uint8_t difficulty;
} ToosTypeGameContext;

// --------------------------------------------------------
// WebSocket & Room Integration API
// --------------------------------------------------------

// 🔴 [마감 패치] DBClient* 가 아니라 순정 sqlite3* 를 받도록 헤더 시그니처 일치화
bool toos_type_game_context_init(ToosTypeGameContext *ctx, sqlite3 *db, uint64_t room_id, uint8_t difficulty);
void toos_type_game_context_deinit(ToosTypeGameContext *ctx);

// 1. 클라이언트 세션 관리
bool toos_type_game_add_player(ToosTypeGameContext *ctx, uint64_t player_id, HttpConnection *conn);
// 🔴 [마감 패치] remove_player 에 now_ms 인자 추가 (구현부 및 서버 호출부와 일치)
bool toos_type_game_remove_player(ToosTypeGameContext *ctx, HttpConnection *conn, uint64_t now_ms);
ToosTypeClientSession* toos_type_game_find_session_by_conn(ToosTypeGameContext *ctx, HttpConnection *conn);

// 2. 프로토콜 액션 핸들러 (JOIN_GAME, READY, TYPE_RESULT 등)
void toos_type_game_handle_ws_message(ToosTypeGameContext *ctx, HttpConnection *conn, const char *msg, size_t len, uint64_t now_ms);

// 3. EventLoop 틱 통합 처리
void toos_type_game_tick(ToosTypeGameContext *ctx, uint64_t now_ms);