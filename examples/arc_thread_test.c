#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // sleep 함수용
#include "libcore.h"

// 🛠️ 스레드가 백그라운드에서 실행할 실제 작업 (Runnable)
void* worker_task(void* arg) {
    String* task_name = (String*)arg;
    char buf[128];
    toString((Object*)task_name, buf, sizeof(buf));

    printf("  [Worker] '%s' 백그라운드 작업 시작...\n", buf);

    // 복잡한 연산을 시뮬레이션하기 위해 1초 대기
    sleep(1);

    printf("  [Worker] '%s' 백그라운드 작업 완료!\n", buf);

    return NULL; // 이번 테스트에서는 단순 상태 확인용이므로 NULL 반환
}

int main() {
    printf("=== [투스 IT 홀딩스] Thread 1.0 ARC 생명주기 통합 테스트 ===\n\n");

    // =========================================================
    // [시나리오 1] Join: 끝날 때까지 기다렸다가 수거하기
    // =========================================================
    printf("[1] 'Join' 패턴 테스트 시작 (동기 대기)\n");
    String* arg1 = new_String("DB_Backup_Task");
    Thread* t1 = new_Thread(worker_task, (void*)arg1);

    t1->start(t1);
    printf(" - 메인: t1을 시작했습니다. 끝날 때까지 대기(join)합니다.\n");

    t1->join(t1); // 스레드 종료 대기
    printf(" - 메인: t1 작업이 무사히 종료되었습니다.\n");

    // 메인이 소유권을 가졌으므로 명시적 해제
    RELEASE_NULL(t1);
    RELEASE_NULL(arg1);

    // =========================================================
    // [시나리오 2] Fire-and-Forget: 쏘고 잊어버리기 (ARC 마법 확인)
    // =========================================================
    printf("\n[2] 'Fire-and-Forget' 패턴 테스트 시작 (ARC 자가 소각)\n");
    String* arg2 = new_String("Log_Flush_Task");
    Thread* t2 = new_Thread(worker_task, (void*)arg2);

    t2->start(t2);
    printf(" - 메인: t2를 시작하자마자 바로 RELEASE(t2)를 호출하여 버립니다!\n");

    // 🚨 [핵심] 메인에서 참조 카운트를 깎아버림!
    // 하지만 t2의 start 내부에서 RETAIN을 걸었기 때문에 객체가 죽지 않아야 함!
    RELEASE_NULL(t2);

    printf(" - 메인: t2를 버렸지만, 백그라운드 작업은 스스로 살아남아 완료되어야 합니다.\n");

    // 메인 프로세스가 먼저 종료되면 OS가 백그라운드 스레드들을 강제로 다 죽여버리므로,
    // 테스트 환경을 위해 메인이 조금 늦게 퇴근하도록 2초 대기합니다.
    sleep(2);

    // 인자 해제
    RELEASE_NULL(arg2);

    printf("\n=== 테스트 종료: Valgrind 결과를 확인하십시오 ===\n");
    return 0;
}
