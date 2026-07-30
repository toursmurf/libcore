/**
 * @file arc_log_exception_integration_test.c
 * @brief 🇰🇷 파일/콘솔 로거, 비동기 로거(AsyncLogger), 그리고 ErrorCode 기반의 예외 처리(Exception) 통합 데모입니다.
 * 🇬🇧 Integrated demo of File/Console Logger, AsyncLogger, and ErrorCode-based Exception handling.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include "logger.h"
#include "async_logger.h"
#include "exception.h"
#include <stdio.h>
#include <unistd.h>

// ==========================================
// 1. 하위 로직: 파일 읽기 실패 시뮬레이션
// ==========================================
Exception* read_config_file() {
    // 가장 깊은 곳에서 발생한 근본 에러(Root Cause)
    return throw_Exception(ERR_FILE_NOT_FOUND, 0, "config.json 파일을 찾을 수 없습니다.");
}

// ==========================================
// 2. 상위 로직: 하위 에러를 잡아 체이닝(Chaining)
// ==========================================
Exception* initialize_system() {
    Exception* cause = read_config_file();
    
    if (cause) {
        // 하위 에러를 감싸는 새로운 에러 생성 (내부적으로 cause를 RETAIN 함)
        Exception* err = throw_ExceptionCause(ERR_CONFIG, 0, "시스템 초기화 실패: 필수 설정 파일 누락", cause);
        
        // ★ [이돌이 패치] 나(initialize_system)의 소유권을 포기한다!
        // 이제 cause의 생사여탈권은 오직 err 객체만이 가진다!
        RELEASE((Object*)cause); 
        
        return err;
    }
    return NULL;
}

// ==========================================
// 3. 메인 관제탑 (통합 검증)
// ==========================================
int main() {
    printf("=== [libcore v1.0 통합 검증 테스트 시작] ===\n\n");

    // --------------------------------------
    // [Phase 1] 비동기 로거(AsyncLogger) 가동
    // --------------------------------------
    AsyncLogger* alogger = new_AsyncLogger(LOG_LEVEL_DEBUG);

    // 내부(inner) 동기 로거에 파일 출력 설정
    alogger->inner->setLogFile(alogger->inner, "core_integration.log");

    ALOG_INFO(alogger, "비동기 로거 엔진 가동 완료. (백그라운드 스레드 동작 중)");
    ALOG_DEBUG(alogger, "현재 모드: ASYNC (큐 사이즈: 1024, Batch: 100)");

    // --------------------------------------
    // [Phase 2] Exception 체이닝 및 비동기 로깅 통합
    // --------------------------------------
    ALOG_INFO(alogger, "시스템 초기화 시퀀스 진입...");

    Exception* err = initialize_system();

    if (err) {
        ALOG_ERROR(alogger, "치명적 예외 발생!! 스택 트레이스를 분석합니다.");

        // 1. 콘솔에 체이닝된 예외 전체 출력
        printf("\n[Exception Stack Trace]\n");
        err->printStackTrace(err);
        printf("-----------------------\n\n");

        // 2. 로거에 최종 에러 메시지 기록
        ALOG_ERROR(alogger, "예외 추적 완료. 최종 원인: %s", err->getMessage(err));

        // 3. [ARC의 마법] 최상단 예외만 RELEASE 하면 꼬리 물고 전부 해제됨!
        RELEASE((Object*)err);
        ALOG_INFO(alogger, "Exception 객체 및 원인(Cause) 메모리 연쇄 해제 완료.");
    }

    // --------------------------------------
    // [Phase 3] 시스템 종료 및 자원 정리
    // --------------------------------------
    ALOG_INFO(alogger, "모든 테스트 완료. AsyncLogger 소멸 시퀀스 진입.");

    // 잠깐 대기하여 Worker 스레드가 큐에 남은 로그를 다 쓰도록 함
    usleep(100000);

    // [ARC의 마법] 이것만 호출하면 스레드 join -> queue 해제 -> inner 로거 해제 완벽 수행!
    RELEASE((Object*)alogger);

    printf("=== [통합 검증 테스트 종료] ===\n");
    return 0;
}
