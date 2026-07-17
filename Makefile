# ==========================================
# [1] 시스템 의존성 및 버전 자동 감지
# ==========================================
HAS_URING := $(shell pkg-config --exists liburing 2>/dev/null && echo 1 || echo 0)
OPENSSL_VER := $(shell pkg-config --modversion openssl 2>/dev/null || echo "Unknown")

# 🚨 MariaDB 동적 판별 로직
MARIADB_VER_RAW := $(shell pkg-config --modversion libmariadb 2>/dev/null)
ifeq ($(MARIADB_VER_RAW),)
    MARIADB_STR = not found
else
    MARIADB_STR = $(MARIADB_VER_RAW)
endif

CC = gcc
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
TEST_DIR = tests
EXAMPLE_DIR = examples
BIN_DIR = bin

# 🎯 CI 검증 대상 단위 테스트 명시 (무한 대기하는 데몬 서버 제외!)
CI_TESTS = \
  arc_file_system_test  \
  arc_ringbuffer_test \
  arc_tree_test  \
  arc_datetime_regex_locale_test  \
  arc_vector_test   \
  arc_stack_test  \
  arc_string_builder_test   \
  arc_text_encoder_test   \
  arc_json_test  \
  arc_config_test \
  arc_crypto_integration_test  \
  arc_cron_shared_primitive_Integrated \
  arc_log_exception_integration_test  \
  arc_queue_test   \
  arc_thread_test   \
  arc_http_client_test  \
  arc_path_validator_test   \
  arc_list_test  \
  arc_regex_test   \
  arc_linkedlist_test  \
  arc_byte_buffer_test   \
  arc_btree_test   \
  all_test_v2   \
  arc_news_crawler   \
  compare_raw_vs_libcore

# 공통 CFLAGS 및 LIBS
CFLAGS = -Wall -Wextra -Wunused-value -pthread -I/usr/include/mysql -I/usr/include/mysql/mysql -I$(INC_DIR)
LIBS = -L/usr/lib64/ -lmariadb -lcurl -lssl -lcrypto -lrt

# ==========================================
# [2] 환경별 컴파일 옵션 동적 할당
# ==========================================
# 🚀 2-A. IO 백엔드 분기
BACKEND_STR = epoll
ifeq ($(HAS_URING),1)
    CFLAGS += -DHAS_LIBURING
    LIBS += -luring
    BACKEND_STR = io_uring
endif

# 🛠️ 2-B. 빌드 모드 및 Sanitizer 동기화 (투트랙 분리 적용!)
DEBUG ?= 0
USE_ASAN ?= 0

ifeq ($(DEBUG),1)
    MODE_STR = Debug (-O0 -g)
    ifeq ($(USE_ASAN),1)
        SANITIZER_STR = ON (ASan, UBSan)
        CFLAGS += -O0 -g -fsanitize=address,undefined
    else
        SANITIZER_STR = OFF (Valgrind Ready)
        CFLAGS += -O0 -g
    endif
else
    MODE_STR = Release (-O2)
    SANITIZER_STR = OFF
    CFLAGS += -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong
endif

# ==========================================
# 🚀 libcore Build Configuration Banner
# ==========================================
$(info =========================================)
$(info  libcore Build Configuration (v1.5 Final) )
$(info =========================================)
$(info  Compiler   : $(CC))
$(info  Backend    : $(BACKEND_STR))
$(info  OpenSSL    : $(OPENSSL_VER))
$(info  MariaDB    : $(MARIADB_STR))
$(info  Sanitizer  : $(SANITIZER_STR))
$(info  Mode       : $(MODE_STR))
$(info =========================================)
$(info )

# 파일 수집 및 카운팅
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:.c=.o)
OBJ_COUNT = $(words $(OBJS))

EXAMPLE_SRCS = $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS = $(EXAMPLE_SRCS:.c=)
EX_COUNT = $(words $(EXAMPLE_BINS))

.PHONY: all clean clean_bin test examples ci

all: $(LIB_DIR)/libcore.a
	@echo "-----------------------------------------"
	@echo " Objects : $(OBJ_COUNT)"
	@echo " Library : $(LIB_DIR)/libcore.a"
	@echo "-----------------------------------------"

$(LIB_DIR)/libcore.a: $(OBJS)
	@mkdir -p $(LIB_DIR)
	@ar rcs $@ $(OBJS)
	@echo "✅ libcore.a 빌드 완료!"

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@$(CC) $(CFLAGS) -c $< -o $@

test: $(LIB_DIR)/libcore.a
	@mkdir -p $(TEST_DIR)
	@$(CC) $(CFLAGS) $(EXAMPLE_DIR)/all_test_v2.c $(LIB_DIR)/libcore.a $(LIBS) -o $(TEST_DIR)/run_test -lm
	@echo "🔥 테스트 실행!"
	@./$(TEST_DIR)/run_test

# ----- 🚀 예제 컴파일 타겟 -----
examples: $(EXAMPLE_BINS)
	@echo "-----------------------------------------"
	@echo " Examples: $(EX_COUNT) 개 빌드 완료"
	@echo "-----------------------------------------"
	@echo "🚀 모든 예제 컴파일 완벽 성공!"

$(EXAMPLE_DIR)/%: $(EXAMPLE_DIR)/%.c $(LIB_DIR)/libcore.a
	@echo "🛠️  Building 예제: $@"
	@$(CC) $(CFLAGS) $< $(LIB_DIR)/libcore.a $(LIBS) -o $@ -lm

# ----- ⚙️ 궁극의 원클릭(One-Click) 투트랙 CI 파이프라인 -----
ci: clean_bin
	@date +%s > .ci_timer
	@echo "=========================================================="
	@echo " 🚀 [STEP 1] Valgrind 전용 빌드 (ASan OFF) 가동"
	@echo "=========================================================="
	@$(MAKE) clean examples DEBUG=1 USE_ASAN=0 > /dev/null
	@mkdir -p $(BIN_DIR)/valgrind
	@for t in $(CI_TESTS); do install -m 755 $(EXAMPLE_DIR)/$$t $(BIN_DIR)/valgrind/; done

	@echo "=========================================================="
	@echo " 🚀 [STEP 2] ASan 전용 빌드 (ASan ON) 가동"
	@echo "=========================================================="
	@$(MAKE) clean examples DEBUG=1 USE_ASAN=1 > /dev/null
	@mkdir -p $(BIN_DIR)/asan
	@for t in $(CI_TESTS); do install -m 755 $(EXAMPLE_DIR)/$$t $(BIN_DIR)/asan/; done

	@echo "=========================================================="
	@echo " ⚔️ [STEP 3] ASan 런타임 검증 (메모리 침범 쾌속 사냥)"
	@echo "=========================================================="
	@ASAN_PASS=0; ASAN_FAIL=0; \
	for t in $(CI_TESTS); do \
		if ./$(BIN_DIR)/asan/$$t > /dev/null 2>&1; then \
			printf "▶ %-40s \033[32mPASS (ASan)\033[0m\n" $$t; \
			ASAN_PASS=$$((ASAN_PASS+1)); \
		else \
			printf "▶ %-40s \033[31mFAIL (ASan)\033[0m\n" $$t; \
			ASAN_FAIL=$$((ASAN_FAIL+1)); \
		fi; \
	done; \
	echo "✅ ASan 결과 | PASS: $$ASAN_PASS  ❌ FAIL: $$ASAN_FAIL"; \
	if [ $$ASAN_FAIL -ne 0 ]; then exit 1; fi

	@echo "=========================================================="
	@echo " 🛡️ [STEP 4] Valgrind 런타임 검증 (1바이트 누수 정밀 추적)"
	@echo "=========================================================="
	@VAL_PASS=0; VAL_FAIL=0; \
	for t in $(CI_TESTS); do \
		if valgrind --leak-check=full --error-exitcode=1 --quiet ./$(BIN_DIR)/valgrind/$$t > /dev/null 2>&1; then \
			printf "▶ %-40s \033[32mPASS (0 Bytes Leak)\033[0m\n" $$t; \
			VAL_PASS=$$((VAL_PASS+1)); \
		else \
			printf "▶ %-40s \033[31mFAIL (Leak Detected!)\033[0m\n" $$t; \
			VAL_FAIL=$$((VAL_FAIL+1)); \
		fi; \
	done; \
	echo "✅ Valgrind 결과 | PASS: $$VAL_PASS  ❌ FAIL: $$VAL_FAIL"; \
	START=$$(cat .ci_timer); END=$$(date +%s); ELAPSED=$$((END - START)); \
	MIN=$$((ELAPSED / 60)); SEC=$$((ELAPSED % 60)); \
	echo ""; \
	if [ $$MIN -gt 0 ]; then \
		echo "🎉 [Iron Fortress CI 2단계 통합 검증 완료] 총 소요 시간: $$MIN분 $$SEC초 BAAAAAAM!!!!"; \
    else \
		echo "🎉 [Iron Fortress CI 2단계 통합 검증 완료] 총 소요 시간: $$SEC초 BAAAAAAM!!!!"; \
	fi; \
	rm -f .ci_timer; \
	[ $$VAL_FAIL -eq 0 ] || exit 1

# ----- 🧹 클린 타겟 -----
clean_bin:
	@rm -rf $(BIN_DIR)
	@echo "🧹 bin 폴더(격리 구역) 삭제 완료!"

clean: clean_bin
	@rm -f $(SRC_DIR)/*.o
	@rm -f $(LIB_DIR)/libcore.a
	@rm -f $(TEST_DIR)/run_test
	@rm -f $(EXAMPLE_BINS)
	@echo "🧹 전체 클린 완료!"