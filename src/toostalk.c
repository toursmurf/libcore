#define _GNU_SOURCE
#include "toostalk.h"
#include "json.h"
#include "crypto.h"
#include "time_utils.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool Crypto_Password_Verify(const char* plain_password, const char* stored_hash);

static bool is_same_str(const char* s1, const char* s2) {
    if (s1 == s2) return true;
    if (!s1 || !s2) return false;
    return strcmp(s1, s2) == 0;
}

static void send_error(HttpConnection* conn, const char* err_msg) {
    if (!conn || !err_msg) return;
    JSONNode* j = new_JSON_Object();
    if (j) {
        JSONNode* jv;
        jv = new_JSON_String("ERROR");
        if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String(err_msg);
        if (jv) { j->put(j, "text", (Object*)jv); RELEASE((Object*)jv); }

        char* s = j->toString(j);
        if (s) {
            HttpConnection_ws_send(conn, s);
            free(s);
        }
        RELEASE((Object*)j);
    }
}

static void send_error_code(HttpConnection* conn, const char* code, const char* text) {
    if (!conn) return;
    JSONNode* j = new_JSON_Object();
    if (j) {
        JSONNode* jv;
        jv = new_JSON_String("ERROR"); if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String(code); if (jv) { j->put(j, "code", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String(text); if (jv) { j->put(j, "text", (Object*)jv); RELEASE((Object*)jv); }

        char* s = j->toString(j);
        if (s) {
            HttpConnection_ws_send(conn, s);
            free(s);
        }
        RELEASE((Object*)j);
    }
}

static void User_finalize(Object* obj) {
    User* self = (User*)obj;
    if (self->id) RELEASE(self->id);
    if (self->nick) RELEASE(self->nick);
}
static const Class _User_Class = { .name = "User", .size = sizeof(User), .finalize = User_finalize };

User* new_User(const char* id, const char* nick, HttpConnection* conn) {
    if (!nick || !conn) return NULL;
    User* self = (User*)calloc(1, sizeof(User));
    if (!self) return NULL;

    Object_Init((Object*)self, &_User_Class);
    self->id = new_String(id ? id : "?");
    self->nick = new_String(nick ? nick : "anonymous");
    if (!self->id || !self->nick) { RELEASE(self); return NULL; }

    self->conn = conn;
    self->room = NULL;
    self->failed_attempts = 0;
    self->cooldown_until = 0;

    return self;
}

static void RoomUser_finalize(Object* obj) {
    RoomUser* self = (RoomUser*)obj;
    if (self->user) RELEASE(self->user);
}
static const Class _RoomUser_Class = { .name = "RoomUser", .size = sizeof(RoomUser), .finalize = RoomUser_finalize };

RoomUser* new_RoomUser(User* user, Room* room) {
    if (!user || !room) return NULL;
    RoomUser* self = (RoomUser*)calloc(1, sizeof(RoomUser));
    if (!self) return NULL;
    Object_Init((Object*)self, &_RoomUser_Class);
    RETAIN((Object*)user);
    self->user = user;
    self->room = room;
    return self;
}

static void Room_finalize(Object* obj) {
    Room* self = (Room*)obj;
    if (self->name) RELEASE(self->name);
    if (self->members) RELEASE(self->members);
    if (self->owner_id) RELEASE(self->owner_id);
    if (self->topic) RELEASE(self->topic);
    if (self->password_hash) RELEASE(self->password_hash);
}
static const Class _Room_Class = { .name = "Room", .size = sizeof(Room), .finalize = Room_finalize };

Room* new_Room(const char* name) {
    Room* self = (Room*)calloc(1, sizeof(Room));
    if (!self) return NULL;
    Object_Init((Object*)self, &_Room_Class);
    self->name = new_String(name ? name : "lobby");
    self->members = new_ArrayList(16);
    self->owner = NULL;
    self->owner_id = NULL;
    self->topic = NULL;
    self->password_hash = NULL;
    self->is_private = false;
    if (!self->name || !self->members) { RELEASE(self); return NULL; }
    return self;
}

static int Room_indexOf(Room* room, User* user) {
    if(!room || !user) return -1;
    int n = room->members->getSize(room->members);
    for (int i = 0; i < n; i++) {
        RoomUser* ru = (RoomUser*)room->members->get(room->members, i);
        if (ru && ru->user == user) return i;
    }
    return -1;
}

static User* Room_findSuccessor(Room* room, User* leaving) {
    if (!room) return NULL;
    int n = room->members->getSize(room->members);
    for (int i = 0; i < n; i++) {
        RoomUser* ru = (RoomUser*)room->members->get(room->members, i);
        if (ru && ru->user && ru->user != leaving) {
            return ru->user;
        }
    }
    return NULL;
}

bool Room_setOwner(Room* room, User* user) {
    if (!room) return false;
    room->owner = user;
    if (room->owner_id) { RELEASE(room->owner_id); }
    room->owner_id = NULL;
    if (user && user->id) {
        room->owner_id = (String*)RETAIN((Object*)user->id);
    }
    return true;
}

bool Room_isOwner(Room* room, User* user) {
    if (!room || !user || !room->owner_id || !user->id) return false;
    return is_same_str(room->owner_id->c_str(room->owner_id), user->id->c_str(user->id));
}

static void Room_broadcast(Room* room, const char* json_text) {
    if (!room || !json_text) return;
    for (int i = room->members->getSize(room->members) - 1; i >= 0; i--) {
        RoomUser* ru = (RoomUser*)room->members->get(room->members, i);
        if (ru && ru->user && ru->user->conn) {
            HttpConnection_ws_send(ru->user->conn, json_text);
        }
    }
}

int Room_transferOwner(Room* room, User* from, User* to, const char* reason) {
    if (!room || !from || !to) return -1;
    if (Room_indexOf(room, to) < 0) return -1;
    if (!Room_setOwner(room, to)) return -1;

    JSONNode* j = new_JSON_Object();
    if (j) {
        JSONNode* jv;
        jv = new_JSON_String("OWNER_CHANGED");
        if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String(from->nick->c_str(from->nick));
        if (jv) { j->put(j, "from", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String(to->nick->c_str(to->nick));
        if (jv) { j->put(j, "to", (Object*)jv); RELEASE((Object*)jv); }
        if (reason) {
            jv = new_JSON_String(reason);
            if (jv) { j->put(j, "reason", (Object*)jv); RELEASE((Object*)jv); }
        }
        char* s = j->toString(j);
        if (s) { Room_broadcast(room, s); free(s); }
        RELEASE((Object*)j);
    }
    return 0;
}

static void Room_notify(Room* room, const char* type, const char* nick, const char* text) {
    if (!room || !nick || !type) return;
    JSONNode* j = new_JSON_Object();
    if (!j) return;
    JSONNode* jv;
    jv = new_JSON_String(type); if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
    jv = new_JSON_String(nick); if (jv) { j->put(j, "nick", (Object*)jv); RELEASE((Object*)jv); }
    if (text) { jv = new_JSON_String(text); if (jv) { j->put(j, "text", (Object*)jv); RELEASE((Object*)jv); } }
    jv = new_JSON_String(room->name->c_str(room->name)); if (jv) { j->put(j, "room", (Object*)jv); RELEASE((Object*)jv); }
    char* s = j->toString(j);
    if (s) { Room_broadcast(room, s); free(s); }
    RELEASE((Object*)j);
}

static void ToosTalk_finalize(Object* obj) {
    ToosTalk* self = (ToosTalk*)obj;
    if (self->rooms) RELEASE(self->rooms);
    if (self->users) RELEASE(self->users);
}
static const Class _ToosTalk_Class = { .name = "ToosTalk", .size = sizeof(ToosTalk), .finalize = ToosTalk_finalize };

static void internal_leave_room(ToosTalk* self, User* user, bool announce) {
    Room* room = user->room;
    if (!room) return;

    RETAIN((Object*)room);

    User* successor = NULL;
    bool was_owner = Room_isOwner(room, user);
    if (was_owner) {
        successor = Room_findSuccessor(room, user);
    }

    int idx = Room_indexOf(room, user);
    if (idx >= 0) {
        Object* target_ru = room->members->get(room->members, idx);
        room->members->removeResult(room->members, idx);
        if (target_ru) RELEASE(target_ru);
    }
    user->room = NULL;

    if (was_owner) {
        if (successor) {
            if (Room_transferOwner(room, user, successor, "disconnect") == 0) {
                printf("[TT-2] 👑 방장 자동 승계 성공: [%s] %s -> %s (disconnect)\n",
                       room->name->c_str(room->name),
                       user->nick->c_str(user->nick),
                       successor->nick->c_str(successor->nick));
            }
        } else {
            Room_setOwner(room, NULL);
        }
    }

    if (idx >= 0 && announce) {
        Room_notify(room, "LEAVE_ROOM", user->nick->c_str(user->nick), NULL);
    }

    if (room->members->getSize(room->members) == 0) {
        char name_copy[128];
        snprintf(name_copy, sizeof(name_copy), "%s", room->name->c_str(room->name));

        if (self->rooms->get(self->rooms, name_copy) == (Object*)room) {
            if (user->conn && !user->conn->is_closing) {
                JSONNode* j = new_JSON_Object();
                if (j) {
                    JSONNode* jv;
                    jv = new_JSON_String("ROOM_DELETED"); if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
                    jv = new_JSON_String("SYSTEM"); if (jv) { j->put(j, "nick", (Object*)jv); RELEASE((Object*)jv); }
                    jv = new_JSON_String("방의 마지막 멤버가 퇴장하여 방이 삭제되었습니다."); if (jv) { j->put(j, "text", (Object*)jv); RELEASE((Object*)jv); }
                    jv = new_JSON_String(room->name->c_str(room->name)); if (jv) { j->put(j, "room", (Object*)jv); RELEASE((Object*)jv); }
                    char* s = j->toString(j);
                    if (s) { HttpConnection_ws_send(user->conn, s); free(s); }
                    RELEASE((Object*)j);
                }
            }
            self->rooms->remove(self->rooms, name_copy);
            printf("[TT-6] 🗑️ 방 자동 소각: [%s]\n", name_copy);
        }
    }

    RELEASE((Object*)room);
}

static User* impl_addUser(ToosTalk* self, HttpConnection* conn) {
    if (!self || !conn) return NULL;
    char id[32], nick[48];
    snprintf(id, sizeof(id), "u%d", self->next_uid++);
    snprintf(nick, sizeof(nick), "스머프%s", id + 1);

    User* user = new_User(id, nick, conn);
    if (!user) return NULL;
    self->users->put(self->users, id, (Object*)user);
    if (self->users->get(self->users, id) != (Object*)user) {
        RELEASE(user);
        return NULL;
    }
    conn->ws_user_data = user;
    RELEASE(user);
    return user;
}

static void impl_removeUser(ToosTalk* self, User* user) {
    if (!self || !user) return;
    if (user->conn) {
        user->conn->ws_user_data = NULL;
        user->conn = NULL;
    }
    internal_leave_room(self, user, true);
    char id_copy[32];
    snprintf(id_copy, sizeof(id_copy), "%s", user->id->c_str(user->id));
    self->users->remove(self->users, id_copy);
}

static int impl_joinRoom_internal(ToosTalk* self, User* user, const char* room_name, const char* password, bool bypass_auth) {
    if (!self || !user || !room_name || !*room_name) return -1;

    Room* room = (Room*)self->rooms->get(self->rooms, room_name);
    if (!room) {
        send_error_code(user->conn, "ROOM_NOT_FOUND", "존재하지 않는 방입니다.");
        return -1;
    }

    if (user->room == room) return 0;

    if (room->is_private && !bypass_auth) {
        uint64_t current_time = now_monotonic_ms();
        if (user->cooldown_until > current_time) {
            send_error_code(user->conn, "COOLDOWN", "입장 시도 제한. 잠시 후 다시 시도하십시오.");
            return -1;
        }
        if (!password || password[0] == '\0') {
            send_error_code(user->conn, "ROOM_LOCKED", "비밀방입니다. 비밀번호를 입력하십시오.");
            return -1;
        }
        if (!Crypto_Password_Verify(password, room->password_hash->c_str(room->password_hash))) {
            user->failed_attempts++;
            if (user->failed_attempts >= 3) {
                user->cooldown_until = current_time + 10000;
                user->failed_attempts = 0;
            }
            send_error_code(user->conn, "WRONG_PASSWORD", "비밀번호가 일치하지 않습니다.");
            return -1;
        }
        user->failed_attempts = 0;
        user->cooldown_until = 0;
    }

    RoomUser* ru = new_RoomUser(user, room);
    if (!ru) {
        send_error_code(user->conn, "INTERNAL_ERROR", "방 입장 처리 중 메모리 할당에 실패했습니다.");
        return -1;
    }

    internal_leave_room(self, user, true);

    room->members->add(room->members, (Object*)ru);
    RELEASE((Object*)ru);
    user->room = room;

    Room_notify(room, "JOIN_ROOM", user->nick->c_str(user->nick), NULL);
    return 0;
}

static int impl_joinRoom(ToosTalk* self, User* user, const char* room_name, const char* password) {
    return impl_joinRoom_internal(self, user, room_name, password, false);
}

static int impl_createRoom(ToosTalk* self, User* user, JSONNode* j_req) {
    if (!self || !user || !j_req) return -1;
    int ret = -1;
    Room* room = NULL;
    size_t pw_len = 0;

    const char* password = j_req->getStringLen(j_req, "password", &pw_len);
    const char* name = j_req->getString(j_req, "room");

    if (!name || strlen(name) == 0 || strlen(name) > 127) {
        send_error_code(user->conn, "BAD_NAME", "방 이름은 1~127자 이내여야 합니다.");
        goto pw_clean;
    }

    if (self->rooms->get(self->rooms, name)) {
        send_error_code(user->conn, "ROOM_EXISTS", "이미 존재하는 방 이름입니다.");
        goto pw_clean;
    }

    room = new_Room(name);
    if (!room) {
        send_error_code(user->conn, "INTERNAL_ERROR", "방 생성에 실패했습니다.");
        goto pw_clean;
    }

    const char* topic = j_req->getString(j_req, "topic");
    if (topic && strlen(topic) > 0) {
        room->topic = new_String(topic);
        if (!room->topic) {
            send_error_code(user->conn, "INTERNAL_ERROR", "메모리 할당에 실패했습니다 (OOM).");
            RELEASE(room);
            goto pw_clean;
        }
    }

    if (password != NULL) {
        if (pw_len == 0) {
            send_error_code(user->conn, "BAD_PASSWORD", "비밀번호는 공백일 수 없습니다.");
            RELEASE(room);
            goto pw_clean;
        }
        room->is_private = true;
        room->password_hash = Crypto_Password_Hash(password);
        if (!room->password_hash) {
            send_error_code(user->conn, "INTERNAL_ERROR", "암호화 처리 중 오류가 발생했습니다.");
            RELEASE(room);
            goto pw_clean;
        }
    }

    Room_setOwner(room, user);
    self->rooms->put(self->rooms, name, (Object*)room);

    /* 🚨 [클순 마님 패치] 안전한 영역(RELEASE 이전)에서 객체 필드 접근 완료! */
    printf("[TT-2] 👑 방 생성 성공: [%s] (Private: %s)\n", name, room->is_private ? "O" : "X");

    RELEASE(room);
    ret = 0;

    if (impl_joinRoom_internal(self, user, name, NULL, true) != 0) {
        send_error_code(user->conn, "JOIN_FAILED", "방 생성 후 자동 입장에 실패했습니다.");
        self->rooms->remove(self->rooms, name);
        ret = -1;
    }

pw_clean:
    if (password && pw_len > 0) {
        OPENSSL_cleanse((void*)password, pw_len);
    }
    return ret;
}


/* =========================================================
 * [닉네임 중복 체크용 iterate 컨텍스트]
 * ========================================================= */
typedef struct {
    const char* nick;      /* 비교 대상 nick */
    const char* skip_id;   /* 본인 id는 제외 (SET_NICK 재사용 방어) */
    bool        found;
} NickCheckCtx;

static bool nick_check_cb(const char* key, Object* value, void* ctx_ptr) {
    (void)key;
    NickCheckCtx* ctx = (NickCheckCtx*)ctx_ptr;
    User* u = (User*)value;
    if (!u || !u->nick) return true;
    /* skip_id 가 있으면 본인은 건너뜀 */
    if (ctx->skip_id && u->id && is_same_str(u->id->c_str(u->id), ctx->skip_id))
        return true;
    if (is_same_str(u->nick->c_str(u->nick), ctx->nick)) {
        ctx->found = true;
        return false; /* 찾았으면 순회 중단 */
    }
    return true;
}

/* =========================================================
 * NAME_CHECK
 *   C→S: {"type":"NAME_CHECK","nick":"마왕"}
 *   S→C: {"type":"NAME_OK","nick":"마왕"}
 *      | {"type":"ERROR","code":"NICK_TAKEN","text":"..."}
 *   상태 변경 없음 — 조회만
 * ========================================================= */
static void impl_nameCheck(ToosTalk* self, User* user, const char* nick) {
    if (!self || !user || !nick || nick[0] == '\0') {
        send_error_code(user->conn, "BAD_NICK", "닉네임을 입력해 주세요.");
        return;
    }
    size_t nlen = strlen(nick);
    if (nlen < 1 || nlen > 24) {
        send_error_code(user->conn, "BAD_NICK", "닉네임은 1~24자 이내여야 합니다.");
        return;
    }

    NickCheckCtx ctx = { .nick = nick, .skip_id = NULL, .found = false };
    self->users->iterate(self->users, nick_check_cb, &ctx);

    JSONNode* j = new_JSON_Object();
    if (!j) return;
    JSONNode* jv;

    if (ctx.found) {
        jv = new_JSON_String("ERROR");
        if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String("NICK_TAKEN");
        if (jv) { j->put(j, "code", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String("이미 사용 중인 닉네임입니다.");
        if (jv) { j->put(j, "text", (Object*)jv); RELEASE((Object*)jv); }
    } else {
        jv = new_JSON_String("NAME_OK");
        if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
        jv = new_JSON_String(nick);
        if (jv) { j->put(j, "nick", (Object*)jv); RELEASE((Object*)jv); }
    }

    char* s = j->toString(j);
    if (s) { HttpConnection_ws_send(user->conn, s); free(s); }
    RELEASE((Object*)j);
}

/* =========================================================
 * SET_NICK
 *   C→S: {"type":"SET_NICK","nick":"마왕"}
 *   S→C: {"type":"NICK_OK","nick":"마왕"}
 *      | {"type":"ERROR","code":"NICK_TAKEN"|"BAD_NICK"|"ALREADY_IN_ROOM","text":"..."}
 *   - 방 안에서는 변경 불가 (TT-3)
 *   - WELCOME 후 방 입장 전까지만 허용
 * ========================================================= */
static void impl_setNick(ToosTalk* self, User* user, const char* nick) {
    if (!self || !user || !nick || nick[0] == '\0') {
        send_error_code(user->conn, "BAD_NICK", "닉네임을 입력해 주세요.");
        return;
    }

    /* 방 안에서는 변경 불가 */
    if (user->room) {
        send_error_code(user->conn, "ALREADY_IN_ROOM", "방 안에서는 닉네임을 변경할 수 없습니다.");
        return;
    }

    size_t nlen = strlen(nick);
    if (nlen < 1 || nlen > 24) {
        send_error_code(user->conn, "BAD_NICK", "닉네임은 1~24자 이내여야 합니다.");
        return;
    }

    /* 중복 체크 — 본인 현재 nick은 통과 (재설정 허용) */
    NickCheckCtx ctx = {
        .nick    = nick,
        .skip_id = user->id ? user->id->c_str(user->id) : NULL,
        .found   = false
    };
    self->users->iterate(self->users, nick_check_cb, &ctx);

    if (ctx.found) {
        send_error_code(user->conn, "NICK_TAKEN", "이미 사용 중인 닉네임입니다.");
        return;
    }

    /* nick 교체 */
    String* new_nick = new_String(nick);
    if (!new_nick) {
        send_error_code(user->conn, "INTERNAL_ERROR", "메모리 할당 실패 (OOM).");
        return;
    }
    RELEASE(user->nick);
    user->nick = new_nick;

    /* NICK_OK 응답 */
    JSONNode* j = new_JSON_Object();
    if (!j) return;
    JSONNode* jv;
    jv = new_JSON_String("NICK_OK");
    if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
    jv = new_JSON_String(nick);
    if (jv) { j->put(j, "nick", (Object*)jv); RELEASE((Object*)jv); }

    char* s = j->toString(j);
    if (s) { HttpConnection_ws_send(user->conn, s); free(s); }
    RELEASE((Object*)j);
}

/* =========================================================
 * ROOM_LIST — iterate 컨텍스트
 * ========================================================= */
typedef struct {
    JSONNode*   arr;     /* JSON 배열 (rooms[]) */
    ToosTalk*   tt;      /* users HashMap 접근용 */
} RoomListCtx;

static bool room_list_cb(const char* key, Object* value, void* ctx_ptr) {
    (void)key;
    RoomListCtx* ctx = (RoomListCtx*)ctx_ptr;
    Room* room = (Room*)value;
    if (!room || !room->name) return true;

    /* 방장 nick 조회 — owner_id → users HashMap */
    const char* owner_nick = "알수없음";
    if (room->owner_id) {
        const char* oid = room->owner_id->c_str(room->owner_id);
        User* owner_user = (User*)ctx->tt->users->get(ctx->tt->users, oid);
        if (owner_user && owner_user->nick)
            owner_nick = owner_user->nick->c_str(owner_user->nick);
    }

    int member_count = room->members ? room->members->getSize(room->members) : 0;

    JSONNode* entry = new_JSON_Object();
    if (!entry) return true; /* OOM → 이 방만 건너뜀, 순회는 계속 */

    JSONNode* jv;
    jv = new_JSON_String(room->name->c_str(room->name));
    if (jv) { entry->put(entry, "name", (Object*)jv); RELEASE((Object*)jv); }

    jv = new_JSON_String(owner_nick);
    if (jv) { entry->put(entry, "owner", (Object*)jv); RELEASE((Object*)jv); }

    /* count: new_json_number 사용 (JsonValue 직접 생성) */
    Object* jnum = (Object*)new_json_number((double)member_count);
    if (jnum) { entry->put(entry, "count", jnum); RELEASE(jnum); }

    /* is_private: new_json_bool 사용 */
    Object* jbool = (Object*)new_json_bool(room->is_private ? 1 : 0);
    if (jbool) { entry->put(entry, "is_private", jbool); RELEASE(jbool); }

    /* 🚨 password/password_hash 절대 미포함 */

    ctx->arr->add(ctx->arr, (Object*)entry);
    RELEASE((Object*)entry);
    return true; /* 계속 순회 */
}

/* =========================================================
 * ROOM_LIST
 *   C→S: {"type":"ROOM_LIST"}
 *   S→C: {"type":"ROOM_LIST","rooms":[
 *           {"name":"개발방","owner":"마왕","count":3,"is_private":false},
 *           ...
 *         ]}
 * ========================================================= */
static void impl_roomList(ToosTalk* self, User* user) {
    if (!self || !user) return;

    JSONNode* j   = new_JSON_Object();
    JSONNode* arr = new_JSON_Array();
    if (!j || !arr) {
        if (j)   RELEASE((Object*)j);
        if (arr) RELEASE((Object*)arr);
        send_error_code(user->conn, "INTERNAL_ERROR", "OOM");
        return;
    }

    RoomListCtx ctx = { .arr = arr, .tt = self };
    self->rooms->iterate(self->rooms, room_list_cb, &ctx);

    JSONNode* jv = new_JSON_String("ROOM_LIST");
    if (jv) { j->put(j, "type", (Object*)jv); RELEASE((Object*)jv); }
    j->put(j, "rooms", (Object*)arr);
    RELEASE((Object*)arr);

    char* s = j->toString(j);
    if (s) { HttpConnection_ws_send(user->conn, s); free(s); }
    RELEASE((Object*)j);
}

static void impl_chat(ToosTalk* self, User* user, const char* text) {
    (void)self;
    if (!user || !text || !user->room) return;
    Room_notify(user->room, "CHAT", user->nick->c_str(user->nick), text);
}

static void impl_handleMessage(ToosTalk* self, HttpConnection* conn, const char* msg, size_t len) {
    if (!conn || !msg) return;

    User* user = (User*)conn->ws_user_data;
    if (!user) return;

    char* safe_msg = (char*)malloc(len + 1);
    if (!safe_msg) {
        send_error(conn, "OOM: memory allocation failed");
        return;
    }
    memcpy(safe_msg, msg, len);
    safe_msg[len] = '\0';

    JSONNode* j = new_JSON(safe_msg);
    OPENSSL_cleanse(safe_msg, len);
    free(safe_msg);

    if (!j) { send_error(conn, "invalid json"); return; }

    const char* type = j->getString(j, "type");
    if (!type) {
        send_error(conn, "missing type");
        RELEASE((Object*)j); return;
    }

    if (is_same_str(type, "CREATE_ROOM")) {
        impl_createRoom(self, user, j);
    } else if (is_same_str(type, "JOIN_ROOM")) {
        const char* name = j->getString(j, "room");
        size_t pw_len = 0;
        const char* password = j->getStringLen(j, "password", &pw_len);

        self->joinRoom(self, user, name, password);

        if (password && pw_len > 0) {
            OPENSSL_cleanse((void*)password, pw_len);
        }
    } else if (is_same_str(type, "CHAT")) {
        const char* text = j->getString(j, "text");
        if (text && user->room) self->chat(self, user, text);
        else if (!user->room) send_error(conn, "no room");
    } else if (is_same_str(type, "LEAVE_ROOM")) {
        internal_leave_room(self, user, true);
    } else if (is_same_str(type, "SET_NICK")) {
        const char* nick = j->getString(j, "nick");
        if (nick) impl_setNick(self, user, nick);
        else send_error_code(conn, "BAD_NICK", "nick 필드가 없습니다.");
    } else if (is_same_str(type, "NAME_CHECK")) {
        const char* nick = j->getString(j, "nick");
        if (nick) impl_nameCheck(self, user, nick);
        else send_error_code(conn, "BAD_NICK", "nick 필드가 없습니다.");
    } else if (is_same_str(type, "ROOM_LIST")) {
        impl_roomList(self, user);
    } else {
        send_error(conn, "unknown type");
    }
    RELEASE((Object*)j);
}

ToosTalk* new_ToosTalk(HttpServer* server) {
    ToosTalk* self = (ToosTalk*)calloc(1, sizeof(ToosTalk));
    if (!self) return NULL;
    Object_Init((Object*)self, &_ToosTalk_Class);
    self->server = server;
    self->users = new_HashMap(64);
    self->rooms = new_HashMap(16);

    if (!self->users || !self->rooms) {
        RELEASE(self);
        return NULL;
    }

    self->next_uid = 1;
    self->addUser = impl_addUser;
    self->removeUser = impl_removeUser;
    self->joinRoom = impl_joinRoom;
    self->chat = impl_chat;
    self->handleMessage = impl_handleMessage;
    return self;
}

static ToosTalk* g_tt = NULL;
static void tt_on_open(HttpConnection* conn) {
    if (!g_tt) return;
    User* u = g_tt->addUser(g_tt, conn);
    if (u) {
        char hello[160];
        snprintf(hello, sizeof(hello),"{\"type\":\"WELCOME\",\"id\":\"%s\",\"nick\":\"%s\"}",
                 u->id->c_str(u->id), u->nick->c_str(u->nick));
        HttpConnection_ws_send(conn, hello);
    }
}
static void tt_on_message(HttpConnection* conn, const char* msg, size_t len) {
    if (g_tt) g_tt->handleMessage(g_tt, conn, msg, len);
}
static void tt_on_close(HttpConnection* conn) {
    if (!g_tt) return;
    User* u = (User*)conn->ws_user_data;
    if (u) g_tt->removeUser(g_tt, u);
}
void ToosTalk_bind(ToosTalk* tt, HttpServer* server) {
    g_tt = tt;
    server->on_ws_open = tt_on_open;
    server->on_ws_message = tt_on_message;
    server->on_ws_close = tt_on_close;
}