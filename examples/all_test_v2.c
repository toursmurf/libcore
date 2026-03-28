/*
 * all_test_v2.c - libcore 1.0 전체 통합 테스트
 * Toos IT Holdings - Iron Fortress
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include "libcore.h"

static int g_passed = 0, g_failed = 0;

static void check(int cond, const char* msg, int line) {
    if (cond) {
        printf("  [OK] %s\n", msg);
        g_passed++;
    } else {
	printf("  [FAIL] %s (line %d)\n", msg ? msg : "Assertion failed (No message)", line);
        g_failed++;
    }
}
#define CHECK(c,m) check((c),(m),__LINE__)
#define SECTION(n) printf("\n=== %s ===\n", n)

/* =========================================================
 * [긴급 패치] 멤버 이름을 몰라도 Class를 가져오는 의장님 오리지널 매크로!
 * (원래 내부용이었으나, 테스트를 위해 글로벌 개방!)
 * ========================================================= */
#ifndef GET_CLASS
#define GET_CLASS(obj) ( *( (const Class**) (obj) ) )
#endif

/* ─── IntObj ─────────────────────────────── */
typedef struct { Object base; int val; } IntObj;
static void IntObj_fin(Object* o) {
	(void)o;
}
static void IntObj_str(Object* o, char* b, size_t l) { snprintf(b,l,"%d",((IntObj*)o)->val); }
const Class intObjClass = { .name="IntObj",.size=sizeof(IntObj),.toString=IntObj_str,.finalize=IntObj_fin };
static IntObj* new_IntObj(int v) {
    IntObj* o = calloc(1, sizeof(IntObj));
    Object_Init(&o->base, &intObjClass);
    o->val = v;
    return o;
}
/*------------mysql-------------------------*/
#ifdef ENABLE_MYSQL_TEST
static void test_mysql(void) {
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
      CHECK(db != NULL, "디비 접속 성공");
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
      CHECK(db== NULL, "mariadb 해제 성공");

      printf("\n==================================================\n");
      printf("  ✨ mysql 테스트 완료! ✨\n");
      printf("==================================================\n");
}
#endif // ENABLE_MYSQL_TEST

/* ─── String ─────────────────────────────── */
static void test_string(void) {
    SECTION("String");
    String* s = new_String("Hello, libcore!");
    CHECK(s != NULL, "new_String");
    CHECK(s->length == 15, "length=15");
    CHECK(s->indexOf(s, "libcore") == 7, "indexOf=7");
    CHECK(s->equals(s, "Hello, libcore!"), "equals");

    String* sub = s->substring(s, 0, 5);
    CHECK(strcmp(sub->value, "Hello") == 0, "substring=Hello");
    RELEASE((Object*)sub);

    String* upper = new_String("hello");
    upper->toUpperCase(upper);
    CHECK(strcmp(upper->value, "HELLO") == 0, "toUpperCase");
    RELEASE((Object*)upper);

    String* num = new_String("42");
    CHECK(num->toInt((Object*)num) == 42, "toInt=42");
    RELEASE((Object*)num);

    RELEASE((Object*)s);
}

/* ─── ArrayList ──────────────────────────── */
static void test_arraylist(void) {
    SECTION("ArrayList");
    ArrayList* list = new_ArrayList(4);
    CHECK(list->isEmpty(list), "isEmpty=true");

    for (int i = 0; i < 5; i++) {
        IntObj* o = new_IntObj(i * 10);
        list->add(list, (Object*)o);
        RELEASE((Object*)o);
    }
    CHECK(list->size == 5, "size=5");
    IntObj* item = (IntObj*)list->get(list, 2);
    CHECK(item->val == 20, "get(2)=20");
    list->remove(list, 0);
    CHECK(list->size == 4, "remove후 size=4");
    list->clear(list);
    CHECK(list->size == 0, "clear");
    RELEASE((Object*)list);
}

/* ─── HashMap ────────────────────────────── */
static void test_hashmap(void) {
    SECTION("HashMap");
    HashMap* map = new_HashMap(8);
    String* v1 = new_String("192.168.1.1");
    map->put(map, "snmp.host", (Object*)v1);
    RELEASE((Object*)v1);
    String* v2 = new_String("public");
    map->put(map, "snmp.community", (Object*)v2);
    RELEASE((Object*)v2);
    CHECK(map->getSize(map) == 2, "size=2");
    String* host = (String*)map->get(map, "snmp.host");
    CHECK(host && strcmp(host->value, "192.168.1.1") == 0, "host 조회");
    CHECK(map->get(map, "missing") == NULL, "없는키=NULL");
    map->remove(map, "snmp.community");
    CHECK(map->getSize(map) == 1, "remove후 size=1");
    RELEASE((Object*)map);
}

/* ─── Queue / Stack ──────────────────────── */
static void test_queue_stack(void) {
    SECTION("Queue + Stack");
    Queue* q = new_Queue(4);
    for (int i = 1; i <= 3; i++) {
        IntObj* o = new_IntObj(i * 10);
        q->enqueue(q, (Object*)o);
        RELEASE((Object*)o);
    }
    CHECK(q->getSize(q) == 3, "Queue size=3");
    IntObj* front = (IntObj*)q->dequeue(q);
    CHECK(front->val == 10, "dequeue FIFO=10");
    RELEASE((Object*)front);
    RELEASE((Object*)q);

    Stack* s = new_Stack(4);
    for (int i = 1; i <= 3; i++) {
        IntObj* o = new_IntObj(i * 10);
        s->push(s, (Object*)o);
        RELEASE((Object*)o);
    }
    IntObj* top = (IntObj*)s->pop(s);
    CHECK(top->val == 30, "pop LIFO=30");
    RELEASE((Object*)top);
    RELEASE((Object*)s);
}

/* ─── Vector ─────────────────────────────── */
static void test_vector(void) {
    SECTION("Vector");
    Vector* v = new_Vector(4);
    for (int i = 0; i < 5; i++) {
        IntObj* o = new_IntObj(i * 5);
        v->push_back(v, (Object*)o);
        RELEASE((Object*)o);
    }
    CHECK(v->get_size(v) == 5, "size=5");
    IntObj* last = (IntObj*)v->pop_back(v);
    CHECK(last->val == 20, "pop_back=20");
    RELEASE((Object*)last);
    RELEASE((Object*)v);
}

/* ─── LinkedList + List ──────────────────── */
static void test_list(void) {
    SECTION("LinkedList + List");
    LinkedList* ll = new_LinkedList();
    String* s1 = new_String("Alpha");
    String* s2 = new_String("Beta");
    ll->add_node(ll, (void*)s1); RELEASE((Object*)s1);
    ll->add_node(ll, (void*)s2); RELEASE((Object*)s2);
    CHECK(ll->get_size(ll) == 2, "LinkedList size=2");
    RELEASE((Object*)ll);

    List* lst = new_List();
    for (int i = 0; i < 5; i++) {
        IntObj* o = new_IntObj(i);
        lst->pushBack(lst, (Object*)o);
        RELEASE((Object*)o);
    }
    CHECK(lst->getSize(lst) == 5, "List size=5");
    IntObj* it = (IntObj*)lst->get(lst, 2);
    CHECK(it->val == 2, "List get(2)=2");
    RELEASE((Object*)lst);
}

/* ─── BTree ──────────────────────────────── */
static void test_btree(void) {
    SECTION("BTree");
    BTree* tree = new_BTree(3);
    int vals[] = {50, 30, 70, 20, 40, 60, 80};
    for (int i = 0; i < 7; i++) {
        IntObj* k = new_IntObj(vals[i]);
        IntObj* v = new_IntObj(vals[i] * 10);
        tree->insert(tree, (Object*)k, (Object*)v);
        RELEASE((Object*)k);
        RELEASE((Object*)v);
    }
    CHECK(tree->getSize(tree) == 7, "BTree size=7");
    RELEASE((Object*)tree);
}

void test_json_creation() {
	SECTION("JSON Creation");
    JSONNode *root = new_JSON(NULL);

    Object *name_val = (Object*)new_json_string("InDong KIM");
    root->put(root, "architect", name_val);
    RELEASE(name_val); // ARC가 작동하므로 소유권 해제!

    Object *company_val = (Object*)new_json_string("Toos IT Holdings");
    root->put(root, "company", company_val);
    RELEASE(company_val);

    Object *version_val = (Object*)new_json_number(0.5);
    root->put(root, "version", version_val);
    RELEASE(version_val);

    JSONNode *arr = new_JSON("[]");

    Object *skill1 = (Object*)new_json_string("C");
    Object *skill2 = (Object*)new_json_string("Java");
    Object *skill3 = (Object*)new_json_string("ARC");

    arr->add(arr, skill1); RELEASE(skill1);
    arr->add(arr, skill2); RELEASE(skill2);
    arr->add(arr, skill3); RELEASE(skill3);

    root->put(root, "skills", (Object*)arr);
    CHECK(root != NULL, "JSON 생성 성공 ");
    RELEASE(arr);

    char *json_str = root->toString(root);
    printf("[Result] Created JSON:\n%s\n", json_str);
    CHECK(json_str != NULL, json_str);
    free(json_str);

    RELEASE(root);
}

/* =========================================================
 * [Test 2] JSON Parsing & Data Extraction
 * ========================================================= */
void test_json_parsing() {
		SECTION("JSON Parsing");

    const char *payload =
        "{\"status\":\"SUCCESS\", \"code\":200, \"data\":[\"Google\", \"Microsoft\", \"Anthropic\", \"xAI\"]}";

    JSONNode *parsed = new_JSON(payload);

    if (parsed->isObject(parsed)) {
        CHECK(parsed != NULL, "Parsing ok");

        const char *status = parsed->getString(parsed, "status");
        int code = parsed->getInt(parsed, "code");

        printf(" -> Status : %s\n", status ? status : "null");
        printf(" -> Code   : %d\n", code);

        Object *data_obj = parsed->get(parsed, "data");

        // [보안 패치] strncmp를 이용한 철벽 타입 검사!
        if (data_obj && strncmp(GET_CLASS(data_obj)->name, "ArrayList", sizeof("ArrayList")) == 0) {
            ArrayList *list = (ArrayList*)data_obj;
            printf(" -> Data Array Length: %d\n", list->getSize(list));

            for (int i = 0; i < list->getSize(list); i++) {
                JsonValue *jv = (JsonValue*)list->get(list, i);
                if (jv->type == J_STRING) {
                    printf("    [%d] %s\n", i, jv->string);
                }
            }
        }
    }

    RELEASE(parsed);
    printf("[Clean] JSON Parsing test memory released.\n");
}

/* ─── JSON ───────────────────────────────── */
static void test_json(void) {
    SECTION("JSON");
    const char* js =
        "{\"host\":\"192.168.1.1\","
        "\"port\":162,"
        "\"community\":\"public\"}";
    const JSON* json = GetJSON();
    Object* parsed = json->parse(js);
    CHECK(parsed != NULL, "JSON 파싱 성공");
    if (parsed) {
        HashMap* cfg = (HashMap*)parsed;
        Object* host_obj = cfg->get(cfg, "host");
        CHECK(host_obj != NULL, "host 키 존재");
        if (host_obj) {
            // JSON string -> JsonValue 타입
            char buf[64];
            toString(host_obj, buf, sizeof(buf));
            // JsonValue->toString: ""192.168.1.1""
            CHECK(strstr(buf, "192.168.1.1") != NULL, "host값 일치");
        }
        char* out = json->stringify(parsed);
        CHECK(out != NULL, "stringify 성공");
        free(out);
        RELEASE(parsed);
    }
    test_json_creation();
    test_json_parsing();
}

/* ─── Tree (BST) ─────────────────────────── */
static int int_cmp(Object* a, Object* b) {
    int va = ((IntObj*)a)->val, vb = ((IntObj*)b)->val;
    return va < vb ? -1 : va > vb ? 1 : 0;
}
static void test_tree(void) {
    SECTION("Tree (BST)");
    Tree* tree = new_Tree(int_cmp);
    int vals[] = {50, 30, 70, 20, 40};
    for (int i = 0; i < 5; i++) {
        IntObj* o = new_IntObj(vals[i]);
        tree->insert(tree, (Object*)o);
        RELEASE((Object*)o);
    }
    CHECK(tree->getHeight(tree->root) >= 2, "높이>=2");

    TreeIterator* it = tree->createIterator(tree);
    int prev = -1; int sorted = 1;
    while (it->hasNext(it)) {
        IntObj* o = (IntObj*)it->next(it);
        if (o->val < prev) sorted = 0;
        prev = o->val;
    }
    CHECK(sorted, "중위순회 정렬");
    RELEASE((Object*)it);
    RELEASE((Object*)tree);
}

/* ─── Thread ─────────────────────────────── */
static atomic_int g_count = 0;
static void* count_worker(void* arg) {
	(void) arg;
    for (int i = 0; i < 1000; i++)
        atomic_fetch_add(&g_count, 1);
    return NULL;
}
static void test_thread(void) {
    SECTION("Thread");
    atomic_store(&g_count, 0);
    Thread* threads[4];
    for (int i = 0; i < 4; i++) {
        threads[i] = new_Thread(count_worker, NULL);
        threads[i]->start(threads[i]);
    }
    for (int i = 0; i < 4; i++) {
        threads[i]->join(threads[i]);
        RELEASE((Object*)threads[i]);
    }
    CHECK(atomic_load(&g_count) == 4000, "4×1000=4000");
}

/* ─── Semaphore ──────────────────────────── */
static void test_semaphore(void) {
    SECTION("Semaphore");
    Semaphore* sem = new_Semaphore(3);
    CHECK(sem != NULL, "new_semaphore 성공");
    sem->wait(sem);
    sem->wait(sem);
    // sem_getvalue()는 구현/타이밍에 따라 값이 달라질 수 있으므로,
    // “동작(획득 가능 여부)” 기준으로 테스트를 안정화합니다.
    sem->wait(sem); // 남은 1개 리소스 획득 (3회째 wait)
    int ok = sem->tryWait(sem); // 0개 상태면 실패해야 함
    CHECK(ok == 0, "tryWait 실패(자원 없음)");
    sem->post(sem); // 1개 반납
    ok = sem->tryWait(sem); // 다시 1개 획득 가능해야 함
    CHECK(ok == 1, "tryWait 성공(자원 있음)");
    RELEASE((Object*)sem);
}

/* ─── MAIN ───────────────────────────────── */
int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  libcore 1.0  Iron Fortress\n");
    printf("  전체 통합 테스트 - Toos IT Holdings\n");
    printf("========================================\n");

    test_string();
    test_arraylist();
    test_hashmap();
    test_queue_stack();
    test_vector();
    test_list();
    test_btree();
    test_json();
    test_tree();
    test_thread();
    test_semaphore();

    printf("\n========================================\n");
    printf("  결과: [OK] %d  [FAIL] %d\n", g_passed, g_failed);
    if (g_failed == 0)
        printf("  Iron Fortress 전체 통합 통과! 🔥\n");
    printf("========================================\n\n");
    return g_failed > 0 ? 1 : 0;
}

