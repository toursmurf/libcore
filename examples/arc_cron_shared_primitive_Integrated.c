/**
 * @file arc_cron_shared_primitive_Integrated.c
 * @brief 🇰🇷 향후 v1.1 업데이트에 포함될 CronScheduler, SharedMemory(IPC), Primitive Wrapper 모듈의 통합 테스트 프리뷰입니다.
 * 🇬🇧 Integration test preview of CronScheduler, SharedMemory (IPC), and Primitive Wrapper modules to be included in the future v1.1 update.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include "async_file.h"
#include "cron_scheduler.h"
#include "shared_memory.h"
#include "primitive.h"
#include "hashmap.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void test_async_file() {
    AsyncFile* af = new_AsyncFile("/log/raw/trap.log", true);
    af->start(af);

    for (int i = 0; i < 100; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Event %d\n", i);
        af->writeAsync(af, msg, strlen(msg));
    }

    af->stop(af);
    RELEASE((Object*)af);
}

void cron_callback(void* ud) {
    printf("Cron Fired: %s\n", (char*)ud);
}

void test_cron() {
    CronScheduler* cron = new_CronScheduler();
    cron->addCron(cron, "HB", "*/10 * * * * *", cron_callback, "Heartbeat");
    cron->start(cron);

    sleep(15);

    cron->stop(cron);
    RELEASE((Object*)cron);
}

void test_shm() {
    SharedMemory* shm = new_SharedMemory("/libcore_shm", 1024, true);
    shm->lock(shm);
    shm->write(shm, "SHM DATA", 8);
    shm->unlock(shm);

    char buf[64] = {0};

    shm->lock(shm);
    shm->read(shm, buf, sizeof(buf));
    shm->unlock(shm);

    printf("Read SHM: %s (WriteCnt: %u)\n", buf, shm->getWriteCnt(shm));
    RELEASE((Object*)shm);
}

void test_primitive() {
    HashMap* map = new_HashMap(16);

    // [이돌이 패치] 1. 객체를 생성해서 명시적 변수에 담습니다! (Ref = 1)
    Integer* i_val = (Integer*)INT(1001);
    Double* d_val = (Double*)DOUBLE(3.14159);

    // 2. 맵에 넣습니다! (맵이 RETAIN 하면서 Ref = 2 가 됨)
    map->put(map, "id", (Object*)i_val);
    map->put(map, "pi", (Object*)d_val);

    // 3. 정상적으로 값을 꺼내서 씁니다!
    Integer* id = (Integer*)map->get(map, "id");
    printf("Map ID: %d\n", id->intValue(id));

    // [이돌이 패치] 4. 내가 생성했던 원본 소유권(+1)을 직접 반환합니다!!!!
    RELEASE((Object*)i_val);
    RELEASE((Object*)d_val);

    // 5. 맵을 해제합니다!!!! (이때 맵이 자기가 들고 있던 소유권도 다 날림 -> 최종 소멸!)
    RELEASE((Object*)map);
}

int main() {
    printf("--- libcore V1.0 Full Enterprise Test ---\n");
    test_async_file();
    test_shm();
    test_primitive();
    test_cron();

    return 0;
}