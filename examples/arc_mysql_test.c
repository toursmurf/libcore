#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "hashmap.h"
#include "arraylist.h"
#include "string.h" // 🚀 파일명 변경 반영!

// 의장님 오리지널 RTTI 매크로
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )

int main() {
    printf("==================================================\n");
    printf("  🚀 투스(Toos) IT 홀딩스 - DB 엔진 [17연성 테스트] 🚀\n");
    printf("==================================================\n\n");

    // 🚀 코딩표준 8호 적용 생성자
    DBClient *db = new_DBClient();
    db->setSaveLog(db, 1);

    printf("[1] DB 연결 시도 중...\n");
    if (!db->connect(db)) {
        printf("[FATAL] DB 접속 실패!\n");
        RELEASE((Object*)db);
        exit(1);
    }
    printf(" -> 접속 성공! (Host: %s, Type: %s)\n\n", db->host, db->db_type);

    const char *test_table = "test_oop_log";
    char init_q[1024];
    snprintf(init_q, sizeof(init_q),
        "CREATE TABLE IF NOT EXISTS `%s` ("
        "idx INT AUTO_INCREMENT PRIMARY KEY, "
        "user_name VARCHAR(50), "
        "score INT)", test_table);

    db->sqlQuery(db, init_q);
    db->all_delete_table(db, test_table);

    // =========================================================
    // 💥 [10건 다중 INSERT 테스트]
    // =========================================================
    printf("[2] 데이터 10건 폭격 (INSERT)...\n");
    for (int i = 1; i <= 10; i++) {
        HashMap *insert_data = new_HashMap(16);

        char name_buf[64], score_buf[64];
        snprintf(name_buf, sizeof(name_buf), "User_%02d", i);
        snprintf(score_buf, sizeof(score_buf), "%d", 50 + (i * 5));

        // 🚀 String 및 new_String으로 명칭 통일 완료!
        String *name = new_String(name_buf);
        String *score = new_String(score_buf);

        insert_data->put(insert_data, "user_name", (Object *)name);
        insert_data->put(insert_data, "score", (Object *)score);

        RELEASE(name); // HashMap이 RETAIN 했으므로 로컬 소유권 포기
        RELEASE(score);

        db->insertTable(db, test_table, insert_data);
        RELEASE(insert_data); // 맵 소각 (내부 String들도 연쇄 소각)
    }
    printf(" -> 10건 삽입 완료! (마지막 IDX: %lld)\n\n", db->last_insert_id);

    // =========================================================
    // 🎯 [단건 조회 테스트]
    // =========================================================
    printf("[3] 단건 검색 (최고 점수 유저)...\n");
    HashMap *single_record = db->getRecordFromQuery(db, "SELECT * FROM test_oop_log ORDER BY score DESC LIMIT 1");

    if (single_record) {
        String *name_val = (String *)single_record->get(single_record, "user_name");
        String *score_val = (String *)single_record->get(single_record, "score");
        printf(" -> 🏆 최고 득점자: %s (점수: %s)\n\n", name_val->value, score_val->value);
        RELEASE(single_record);
    }

    // =========================================================
    // 📚 [복수건 조회 테스트]
    // =========================================================
    printf("[4] 복수건 검색 (80점 이상 유저들)...\n");
    ArrayList *multi_records = db->getRecords(db, test_table, "score >= 80", "user_name, score");

    if (multi_records) {
        int list_size = multi_records->getSize(multi_records);
        printf(" -> 총 %d 명의 우수 회원이 검색되었습니다!\n", list_size);

        for (int j = 0; j < list_size; j++) {
            HashMap *row = (HashMap *)multi_records->get(multi_records, j);
            String *n_val = (String *)row->get(row, "user_name");
            String *s_val = (String *)row->get(row, "score");
            printf("    [%d] 이름: %-10s | 점수: %s\n", j + 1, n_val->value, s_val->value);
        }
        RELEASE(multi_records); // 🚀 17연성의 핵심: 리스트 하나만 쏴도 전체 데이터 소각!
    }

    printf("\n[5] DB 연결 종료 및 ARC 최종 소각...\n");
    RELEASE((Object*)db);

    printf("\n==================================================\n");
    printf("  ✨ 17연성 0바이트 신화 완성 대기 중! ✨\n");
    printf("==================================================\n");

    return 0;
}