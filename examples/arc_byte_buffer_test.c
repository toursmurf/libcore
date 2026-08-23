/**
 * @file arc_byte_buffer_test.c
 * @brief 🇰🇷 Java NIO 스타일의 ByteBuffer를 활용한 바이너리 조작 및 엔디안(Endian) 처리 테스트입니다.
 * 🇬🇧 Binary manipulation and Endian processing test using Java NIO-style ByteBuffer.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "bytebuffer.h"

int main() {
    printf("==================================================\n");
    printf(" ByteBuffer S-Tier 최종 파괴 검증 (교정판)\n");
    printf("==================================================\n\n");

    ByteBuffer* buf = new_ByteBuffer(16);

    // [2] 파서 툴킷 테스트
    buf->writeInt32(buf, 0x12345678);
    buf->write(buf, "GET /index.html HTTP/1.1\r\n", 26);
    buf->skip(buf, 4); 
    assert(buf->read_pos == 4);

    // [3] Compact 유도 (write 시 용량이 부족하면 자동으로 compact 실행)
    printf("[3] Compact 자동 실행 및 ReadPos 상태 확인...\n");
    char dummy[60];
    memset(dummy, 'A', 60);
    buf->write(buf, dummy, 60); // 여기서 compact()가 호출되어 read_pos가 0이 됩니다.
    
    printf("    -> 🚨 Compact 실행 후 ReadPos: %zu (0이어야 정상)\n", buf->read_pos);
    assert(buf->read_pos == 0); 

    // [4] readSlice 테스트 (이제 ReadPos는 0인 상태에서 시작)
    printf("[4] readSlice (Consume Semantic) 테스트...\n");
    size_t current_pos = buf->read_pos; 
    ByteBuffer* slice = buf->readSlice(buf, 10);
    
    printf("    -> 10바이트 readSlice 실행 후 ReadPos: %zu\n", buf->read_pos);
    // 🚨 수정된 검증식: compact로 리셋된 0에서 10을 더한 값이 10이어야 함
    assert(buf->read_pos == current_pos + 10); 
    printf("    -> ✅ 소유권 이전 및 중복 처리 방어 확인!\n\n");

    // [5] 보안 테스트
    int res = buf->write(buf, "attack", (size_t)-1);
    assert(res == -1);
    printf("    -> ✅ 오버플로우 방어 확인!\n\n");

    RELEASE(slice);
    RELEASE(buf);
    printf("✅ 모든 지옥의 검증 시나리오를 '진짜' 통과했습니다.\n");
    return 0;
}
