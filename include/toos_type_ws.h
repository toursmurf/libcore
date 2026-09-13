#pragma once
#include "http_server.h"
#include "toos_type_game_context.h"

//WebSocket은 이제 JSON 직렬화와 소켓 전송(I/O)만 책임집니다.
void toos_type_ws_bind(HttpServer *server, ToosTypeGameContext *ctx);