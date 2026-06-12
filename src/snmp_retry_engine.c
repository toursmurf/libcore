#include "hashmap.h"
#include "arraylist.h"
#include "string_obj.h"
#include "snmp.h"
#include "snmp_state.h"
#include "logger.h"

// 실행과 판단 전선의 격리를 위한 이중 스택 리스트 컨텍스트
typedef struct {
    uint64_t now_ms;
    ArrayList* retry_list;  // 재전송 후보 리스트 (소유권 이전)
    ArrayList* delete_list; // 자원 삭제 후보 리스트 (Key 문자열 스냅샷)
} TimeoutCtx;

/**
 * 🚨 [구간 1: 판단 전선 (Pure Read-Only 스캔)] - Mutex Lock 점유 상태
 * 🔒 철학: 10년 유지보수를 위한 완벽한 Read-Only 락.
 * 이벤트성 상태(TIMEOUT, RETRYING)를 배제하고 오직 PENDING만 확인!
 */
static bool check_timeout_scan(const char* key, Object* value, void* ctx) {
    TimeoutCtx* t_ctx = (TimeoutCtx*)ctx;
    PendingRequest* req = (PendingRequest*)value;

    // 오직 응답 대기 상태(REQ_PENDING)인 패킷들만 스캔 궤도에 진입
    if (req->state != REQ_PENDING) {
        return true;
    }

    uint64_t age = t_ctx->now_ms - req->send_time_ms;

    if (age > 5000) { // 제국 표준 5초 타임아웃 경보
        if (req->retry_count < 3) {
            // 🚨 [Over-Retain 방어]: ArrayList가 자체 RETAIN을 하므로 포인터만 던짐!
            t_ctx->retry_list->add(t_ctx->retry_list, (Object*)req);
        } else {
            // [판단] 3회 초과자는 영구 삭제 명단 등록
            String* key_obj = new_String(key);
            t_ctx->delete_list->add(t_ctx->delete_list, (Object*)key_obj);
            RELEASE(key_obj);
        }
    }
    return true; // 무중단 스캔 진행
}

/**
 * 🚨 [구간 2: 상태 전이 및 실행 처리 엔진] - 타이머 Tick 엔트리 포인트
 */
void Snmp_RetryEngine_Tick(Snmp* self) {
    if (!self || !self->pending_requests) return;

    // 1. 함수 내부 스택 영역에 고립 컨텍스트 할당 (레이스 컨디션 0%)
    TimeoutCtx ctx;
    ctx.now_ms = get_current_time_ms();
    ctx.retry_list = new_ArrayList(32);
    ctx.delete_list = new_ArrayList(16);

    if (!ctx.retry_list || !ctx.delete_list) {
        if (ctx.retry_list) RELEASE((Object*)ctx.retry_list);
        if (ctx.delete_list) RELEASE((Object*)ctx.delete_list);
        return;
    }

    // 2.[초고속 Read-Only 스캔 가동]
    self->pending_requests->iterate(self->pending_requests, check_timeout_scan, &ctx);

    //이벤트(Action) 실행 후 다시 상태(State)로 회귀
    int retry_count = ctx.retry_list->getSize(ctx.retry_list);
    for (int i = 0; i < retry_count; i++) {
        PendingRequest* req = (PendingRequest*)ctx.retry_list->get(ctx.retry_list, i);
        if (req) {
            bool ok = Snmp_resend_packet(self, req); // 🚨 행동(Action) 실행

            if (ok) {
                // 성공적으로 쐈다면 클럭과 시도 횟수를 갱신하고 다시 PENDING(상태)로 둡니다.
                req->retry_count++;
                req->send_time_ms = ctx.now_ms;
                req->state = REQ_PENDING;

                ALOG_WARN(g_async_logger, "[RETRY ENGINE] ReqID: %u 재전송 완료 (%d/3)",
                          req->request_id, req->retry_count);
            } else {
                // 💣 소켓 에러 등 물리적 전송 실패 시, 행동 불가 판정 -> FAILED(상태) 전이!
                req->state = REQ_FAILED;
                ALOG_ERROR(g_async_logger, "[RETRY FATAL] ReqID: %u 소켓 전송 치명적 실패! REQ_FAILED 전이",
                           req->request_id);
            }
        }
    }

    // 💣최종 타임아웃 낙오 패킷 FAILED 마킹 및 청소
    int delete_count = ctx.delete_list->getSize(ctx.delete_list);
    for (int i = 0; i < delete_count; i++) {
        String* key_str = (String*)ctx.delete_list->get(ctx.delete_list, i);
        if (key_str) {
            PendingRequest* fail_req = (PendingRequest*)self->pending_requests->get(self->pending_requests, key_str->value);
            if (fail_req) {
                fail_req->state = REQ_FAILED;
            }
            self->pending_requests->remove(self->pending_requests, key_str->value);
        }
    }

    if (delete_count > 0) {
        ALOG_ERROR(g_async_logger, "[TIMEOUT ENGINE] 타임아웃 3회 초과. 패킷 %d건 FAILED 마킹 및 자원 반환.", delete_count);
    }

    // 4. 소각 시 내부 객체 자동 RELEASE!
    RELEASE((Object*)ctx.retry_list);
    RELEASE((Object*)ctx.delete_list);
}