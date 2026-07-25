# ==========================================
# [1] 시스템 의존성 및 버전 자동 감지
# ==========================================
# 🛡️ OS 및 커널 정보 자동 감지
OS_INFO := $(shell grep '^PRETTY_NAME=' /etc/os-release 2>/dev/null | cut -d '"' -f 2 || uname -srm)
KERNEL_INFO := $(shell uname -r)

HAS_URING := $(shell pkg-config --exists liburing 2>/dev/null && echo 1 || echo 0)
OPENSSL_VER := $(shell pkg-config --modversion openssl 2>/dev/null || echo "Unknown")
HAS_URING := 0
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

# ==========================================
# 🎯 [2] CI 검증 대상 단위 테스트 명시 (분리 설계)
# ==========================================
# 🛡️ [2-A] Valgrind + ASan 모두 검증할 순수 코어 로직
CORE_TESTS = \
  arc_file_system_test \
  arc_ringbuffer_test \
  arc_tree_test \
  arc_datetime_regex_locale_test \
  arc_vector_test \
  arc_stack_test \
  arc_string_builder_test \
  arc_text_encoder_test \
  arc_json_test \
  arc_config_test \
  arc_crypto_integration_test \
  arc_cron_shared_primitive_Integrated \
  arc_log_exception_integration_test \
  arc_queue_test \
  arc_thread_test \
  arc_http_client_test \
  arc_path_validator_test \
  arc_list_test \
  arc_regex_test \
  arc_linkedlist_test \
  arc_byte_buffer_test \
  arc_btree_test \
  all_test_v2

# 🚧 [2-B] 너무 무거워서 ASan으로만 검증할 녀석들 (네트워크, 벤치마크)
HEAVY_TESTS = \
  arc_news_crawler \
  compare_raw_vs_libcore

# ASan은 가벼우니까 전체 다 돌림
CI_TESTS = $(CORE_TESTS) $(HEAVY_TESTS)

# ==========================================
# [3] 공통 CFLAGS 및 LIBS
# ==========================================
CFLAGS = -Wall -Wextra -Wunused-value -pthread -I/usr/include/mysql -I/usr/include/mysql/mysql -I$(INC_DIR)
LIBS = -L/usr/lib64/ -lmariadb -lcurl -lssl -lcrypto -lrt

# ==========================================
# [4] 환경별 컴파일 옵션 동적 할당
# ==========================================
# 🚀 4-A. IO 백엔드 분기
BACKEND_STR = epoll
ifeq ($(HAS_URING),1)
    CFLAGS += -DHAS_LIBURING
    LIBS += -luring
    BACKEND_STR = io_uring
endif

# 🛠️ 4-B. 빌드 모드 및 Sanitizer 동기화 (투트랙 분리 적용!)
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
$(info  libcore Build Configuration (v1.6 Final) )
$(info =========================================)
$(info  OS Info    : $(OS_INFO))
$(info  Kernel     : $(KERNEL_INFO))
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

.PHONY: all clean clean_bin clean_soft test examples ci check_asan check_valgrind ci-asan ci-valgrind

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

# ----- 🛡️ [신규] CI 실행 전 의존성 검열관 -----
check_asan:
	@echo "🔍 ASan/UBSan 설치 여부 확인..."
	@echo "int main(){return 0;}" | $(CC) -fsanitize=address,undefined -x c - -o /dev/null 2>/dev/null; \
	if [ $$? -ne 0 ]; then \
		echo "❌ [ERROR] libasan, libubsan이 없습니다 (dnf install libasan libubsan)"; \
		exit 1; \
	fi

check_valgrind:
	@echo "🔍 Valgrind 설치 여부 확인..."
	@if ! command -v valgrind > /dev/null; then \
		echo "❌ [ERROR] Valgrind가 없습니다 (dnf install valgrind)"; \
		exit 1; \
	fi

# ----- ⚡ [타겟 1] 쾌속 사냥 (ASan/UBSan 전용 - 전체 테스트 25개) -----
ci-asan: check_asan clean_soft
	@date +%s > .ci_asan_timer
	@echo "=========================================================="
	@echo " 🚀 [ASan 모드] 빌드 및 메모리 침범 쾌속 사냥 가동"
	@echo "=========================================================="
	@rm -rf $(BIN_DIR)/asan
	@mkdir -p $(BIN_DIR)/asan
	@$(MAKE) clean_soft examples DEBUG=1 USE_ASAN=1 > /dev/null || (echo "❌ ASan용 빌드 실패!" && exit 1)
	@for t in $(CI_TESTS); do install -m 755 $(EXAMPLE_DIR)/$$t $(BIN_DIR)/asan/; done
	@ASAN_PASS=0; ASAN_FAIL=0; \
	for t in $(CI_TESTS); do \
		printf "▶ %-40s " $$t; \
		if ./$(BIN_DIR)/asan/$$t > /dev/null 2>&1; then \
			printf "\033[32m[PASS] ASan\033[0m\n"; \
			ASAN_PASS=$$((ASAN_PASS+1)); \
		else \
			printf "\033[31m[FAIL] ASan Error\033[0m\n"; \
			ASAN_FAIL=$$((ASAN_FAIL+1)); \
		fi; \
	done; \
	echo "✅ ASan 결과 | PASS: $$ASAN_PASS  ❌ FAIL: $$ASAN_FAIL"; \
	START=$$(cat .ci_asan_timer); END=$$(date +%s); ELAPSED=$$((END - START)); \
	echo "⏱️ ASan 소요 시간: $$ELAPSED초"; \
	rm -f .ci_asan_timer; \
	if [ $$ASAN_FAIL -ne 0 ]; then exit 1; fi

# ----- 🛡️ [타겟 2] 정밀 수색 (Valgrind 전용 - 무거운 놈들 제외 코어만) -----
ci-valgrind: check_valgrind clean_soft
	@date +%s > .ci_val_timer
	@echo "=========================================================="
	@echo " 🛡️ [Valgrind 모드] 빌드 및 1바이트 누수 정밀 추적"
	@echo "=========================================================="
	@rm -rf $(BIN_DIR)/valgrind
	@mkdir -p $(BIN_DIR)/valgrind
	@$(MAKE) clean_soft examples DEBUG=1 USE_ASAN=0 > /dev/null || (echo "❌ Valgrind용 빌드 실패!" && exit 1)
	@for t in $(CORE_TESTS); do install -m 755 $(EXAMPLE_DIR)/$$t $(BIN_DIR)/valgrind/; done
	@VAL_PASS=0; VAL_FAIL=0; \
	for t in $(CORE_TESTS); do \
		printf "▶ %-40s " $$t; \
		if valgrind --leak-check=full --error-exitcode=1 --quiet ./$(BIN_DIR)/valgrind/$$t > /dev/null 2>&1; then \
			printf "\033[32m[PASS] 0 Bytes Leak\033[0m\n"; \
			VAL_PASS=$$((VAL_PASS+1)); \
		else \
			printf "\033[31m[FAIL] Leak Detected!\033[0m\n"; \
			VAL_FAIL=$$((VAL_FAIL+1)); \
		fi; \
	done; \
	echo "✅ Valgrind 결과 | PASS: $$VAL_PASS  ❌ FAIL: $$VAL_FAIL"; \
	START=$$(cat .ci_val_timer); END=$$(date +%s); ELAPSED=$$((END - START)); \
	MIN=$$((ELAPSED / 60)); SEC=$$((ELAPSED % 60)); \
	echo "⏱️ Valgrind 소요 시간: $$MIN분 $$SEC초"; \
	rm -f .ci_val_timer; \
	if [ $$VAL_FAIL -ne 0 ]; then exit 1; fi

# ----- 👑 [타겟 3] 풀코스 (전체 검증) -----
ci:
	@date +%s > .ci_total_timer
	@echo "=========================================================="
	@echo " 👑 [Iron Fortress] 전체 통합 CI 파이프라인 가동"
	@echo "=========================================================="
	@$(MAKE) ci-asan
	@$(MAKE) ci-valgrind
	@START=$$(cat .ci_total_timer); END=$$(date +%s); ELAPSED=$$((END - START)); \
	MIN=$$((ELAPSED / 60)); SEC=$$((ELAPSED % 60)); \
	echo ""; \
	echo "🎉 [Iron Fortress CI 전체 통합 검증 완벽 통과] 총 소요 시간: $$MIN분 $$SEC초 BAAAAAAM!!!!"; \
	rm -f .ci_total_timer

# ----- 🧹 클린 타겟 -----
clean_bin:
	@rm -rf $(BIN_DIR)
	@echo "🧹 bin 폴더(격리 구역) 삭제 완료!"

clean_soft:
	@rm -f $(SRC_DIR)/*.o
	@rm -f $(LIB_DIR)/libcore.a
	@rm -f $(TEST_DIR)/run_test
	@rm -f $(EXAMPLE_BINS)

clean: clean_bin clean_soft
	@echo "🧹 전체 클린 완료!"
