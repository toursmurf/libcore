# ==========================================
# [1] 시스템 의존성 및 버전 자동 감지
# ==========================================
UNAME_S		:= $(shell uname -s 2>/dev/null || echo Windows_NT)
OS_INFO		:= $(shell if [ -f /etc/os-release ]; then grep '^PRETTY_NAME=' /etc/os-release 2>/dev/null | cut -d '"' -f 2; else uname -srm; fi)
KERNEL_INFO	:= $(shell uname -r 2>/dev/null || echo "Unknown")
HAS_URING	:= $(shell pkg-config --exists liburing 2>/dev/null && echo 1 || echo 0)
OPENSSL_VER	:= $(shell pkg-config --modversion openssl 2>/dev/null || echo "Unknown")
# v1.7.2 policy: Linux backend is epoll only
HAS_URING	:= 0

# ==========================================
# MariaDB / MySQL 동적 판별
# ==========================================
MARIADB_VER_RAW	:= $(shell pkg-config --modversion libmariadb 2>/dev/null)
ifeq ($(MARIADB_VER_RAW),)
    MARIADB_STR	= not found
    HAVE_MYSQL	:= 0
else
    MARIADB_STR	= $(MARIADB_VER_RAW)
    HAVE_MYSQL	:= 1
endif

# ==========================================
# PostgreSQL 동적 판별 (Rocky Linux / PGDG 대응)
# ==========================================
POSTGRES_VER_RAW := $(shell pkg-config --modversion libpq 2>/dev/null)
ifeq ($(POSTGRES_VER_RAW),)
    # pkg-config 실패 시 pg_config 직접 찾기 (/usr/pgsql-* 경로 포함)
    PG_CONFIG_EXE := $(shell command -v pg_config 2>/dev/null || ls /usr/pgsql-*/bin/pg_config 2>/dev/null | tail -n 1)
    ifneq ($(PG_CONFIG_EXE),)
        POSTGRES_STR := $(shell $(PG_CONFIG_EXE) --version | sed 's/PostgreSQL //')
        HAVE_PGSQL	:= 1
        PG_INC_DIR	:= $(shell $(PG_CONFIG_EXE) --includedir)
        PG_LIB_DIR	:= $(shell $(PG_CONFIG_EXE) --libdir)
    else
        POSTGRES_STR = not found
        HAVE_PGSQL	:= 0
    endif
else
    POSTGRES_STR = $(POSTGRES_VER_RAW)
    HAVE_PGSQL	:= 1
endif

# ==========================================
# SQLite 동적 판별
# ==========================================
SQLITE_VER_RAW	:= $(shell pkg-config --modversion sqlite3 2>/dev/null)
ifeq ($(SQLITE_VER_RAW),)
    SQLITE_STR	= not found
    HAVE_SQLITE	:= 0
else
    SQLITE_STR	= $(SQLITE_VER_RAW)
    HAVE_SQLITE	:= 1
endif

# ==========================================
# 디렉토리 및 컴파일러 설정
# ==========================================
CC		= gcc
SRC_DIR		= src
INC_DIR		= include
LIB_DIR		= lib
TEST_DIR	= tests
EXAMPLE_DIR	= examples
BIN_DIR		= bin

# ==========================================
# [2] CI 검증 대상 단위 테스트 명시
# ==========================================
CORE_TESTS	= \
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

HEAVY_TESTS	= \
  arc_news_crawler \
  compare_raw_vs_libcore

CI_TESTS	= $(CORE_TESTS) $(HEAVY_TESTS)

# ==========================================
# [3] 공통 CFLAGS 및 LIBS
# ==========================================
CFLAGS		= -Wall -Wextra -Wunused-value -pthread -I$(INC_DIR)
LIBS		= -lcurl -lssl -lcrypto

# ==========================================
# DB backend 컴파일 플래그 주입
# ==========================================
ifeq ($(HAVE_MYSQL),1)
    CFLAGS	+= -DHAVE_MYSQL
endif
ifeq ($(HAVE_PGSQL),1)
    CFLAGS	+= -DHAVE_PGSQL
endif
ifeq ($(HAVE_SQLITE),1)
    CFLAGS	+= -DHAVE_SQLITE
endif

ifeq ($(UNAME_S), Linux)
    LIBS	+= -lrt
    ifeq ($(HAVE_MYSQL),1)
        CFLAGS	+= -I/usr/include/mysql -I/usr/include/mysql/mysql
    endif
    ifeq ($(HAVE_PGSQL),1)
        ifdef PG_INC_DIR
            CFLAGS	+= -I$(PG_INC_DIR)
            LIBS	+= -L$(PG_LIB_DIR)
        else
            CFLAGS	+= $(shell pkg-config --cflags libpq 2>/dev/null || echo "-I/usr/include/postgresql")
            LIBS	+= $(shell pkg-config --libs-only-L libpq 2>/dev/null)
        endif
    endif
    LIBS	+= -L/usr/lib64/
endif

ifeq ($(HAVE_MYSQL),1)
    LIBS	+= -lmariadb
endif
ifeq ($(HAVE_PGSQL),1)
    LIBS	+= -lpq
endif
ifeq ($(HAVE_SQLITE),1)
    LIBS	+= -lsqlite3
endif

# ==========================================
# [4] 환경별 컴파일 옵션
# ==========================================
ifeq ($(UNAME_S), Linux)
    TARGET_OS	= Linux
    ifeq ($(HAS_URING),1)
        CFLAGS	+= -DHAS_LIBURING
        LIBS	+= -luring
        BACKEND_STR = io_uring
    else
        BACKEND_STR = epoll
    endif
else ifeq ($(UNAME_S), Darwin)
    TARGET_OS	= macOS
    BACKEND_STR	= kqueue
    CFLAGS	+= -DLIBCORE_USE_KQUEUE
    BREW_OPENSSL := $(shell brew --prefix openssl 2>/dev/null)
    ifneq ($(BREW_OPENSSL),)
        CFLAGS	+= -I$(BREW_OPENSSL)/include
        LIBS	+= -L$(BREW_OPENSSL)/lib
    endif
    ifeq ($(HAVE_MYSQL),1)
        BREW_MARIADB := $(shell brew --prefix mariadb-connector-c 2>/dev/null)
        ifneq ($(BREW_MARIADB),)
            CFLAGS	+= -I$(BREW_MARIADB)/include/mariadb
            LIBS	+= -L$(BREW_MARIADB)/lib/mariadb -L$(BREW_MARIADB)/lib
        endif
    endif
    ifeq ($(HAVE_PGSQL),1)
        BREW_POSTGRES := $(shell brew --prefix libpq 2>/dev/null)
        ifneq ($(BREW_POSTGRES),)
            CFLAGS	+= -I$(BREW_POSTGRES)/include
            LIBS	+= -L$(BREW_POSTGRES)/lib
        endif
    endif
    ifeq ($(HAVE_SQLITE),1)
        BREW_SQLITE := $(shell brew --prefix sqlite 2>/dev/null)
        ifneq ($(BREW_SQLITE),)
            CFLAGS	+= -I$(BREW_SQLITE)/include
            LIBS	+= -L$(BREW_SQLITE)/lib
        endif
    endif
else ifneq (,$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S))$(findstring CYGWIN,$(UNAME_S)))
    TARGET_OS	= Windows
    BACKEND_STR	= IOCP
    CFLAGS	+= -DLIBCORE_USE_IOCP
    LIBS	+= -lws2_32 -lmswsock
else ifeq ($(OS), Windows_NT)
    TARGET_OS	= Windows
    BACKEND_STR	= IOCP
    CFLAGS	+= -DLIBCORE_USE_IOCP
    LIBS	+= -lws2_32 -lmswsock
else
    TARGET_OS	= $(UNAME_S)
    BACKEND_STR	= Unknown
endif

DEBUG		?= 0
USE_ASAN	?= 0
ifeq ($(DEBUG),1)
    MODE_STR	= Debug (-O0 -g)
    ifeq ($(USE_ASAN),1)
        SANITIZER_STR = ON (ASan, UBSan)
        CFLAGS	+= -O0 -g -fsanitize=address,undefined
    else
        SANITIZER_STR = OFF (Valgrind Ready)
        CFLAGS	+= -O0 -g
    endif
else
    MODE_STR	= Release (-O2)
    SANITIZER_STR = OFF
    CFLAGS	+= -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong
endif

# ==========================================
# Banner
# ==========================================
$(info =========================================)
$(info  libcore Build Configuration (v1.7.2))
$(info =========================================)
$(info  Target OS  : $(TARGET_OS))
$(info  OS Info    : $(OS_INFO))
$(info  Kernel     : $(KERNEL_INFO))
$(info  Compiler   : $(CC))
$(info  Backend    : $(BACKEND_STR))
$(info  OpenSSL    : $(OPENSSL_VER))
$(info  MariaDB    : $(MARIADB_STR))
$(info  PostgreSQL : $(POSTGRES_STR))
$(info  SQLite     : $(SQLITE_STR))
$(info  HAVE_MYSQL : $(HAVE_MYSQL))
$(info  HAVE_PGSQL : $(HAVE_PGSQL))
$(info  HAVE_SQLITE: $(HAVE_SQLITE))
$(info  Sanitizer  : $(SANITIZER_STR))
$(info  Mode       : $(MODE_STR))
$(info =========================================)
$(info )

SRCS		= $(wildcard $(SRC_DIR)/*.c)
OBJS		= $(SRCS:.c=.o)
OBJ_COUNT	= $(words $(OBJS))
EXAMPLE_SRCS	= $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS	= $(EXAMPLE_SRCS:.c=)
EX_COUNT	= $(words $(EXAMPLE_BINS))

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
	$(CC) $(CFLAGS) -c $< -o $@

test: $(LIB_DIR)/libcore.a
	@mkdir -p $(TEST_DIR)
	@$(CC) $(CFLAGS) $(EXAMPLE_DIR)/all_test_v2.c $(LIB_DIR)/libcore.a $(LIBS) -o $(TEST_DIR)/run_test -lm
	@echo "🔥 테스트 실행!"
	@./$(TEST_DIR)/run_test

examples: $(EXAMPLE_BINS)
	@echo "-----------------------------------------"
	@echo " Examples: $(EX_COUNT) 개 빌드 완료"
	@echo "-----------------------------------------"
	@echo "🚀 모든 예제 컴파일 완벽 성공!"

$(EXAMPLE_DIR)/%: $(EXAMPLE_DIR)/%.c $(LIB_DIR)/libcore.a
	@echo "🛠️  Building 예제: $@"
	$(CC) $(CFLAGS) $< $(LIB_DIR)/libcore.a $(LIBS) -o $@ -lm

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
	START=$$(cat .ci_asan_timer); \
	END=$$(date +%s); \
	ELAPSED=$$((END - START)); \
	echo "⏱️ ASan 소요 시간: $$ELAPSED초"; \
	rm -f .ci_asan_timer; \
	if [ $$ASAN_FAIL -ne 0 ]; then exit 1; fi

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
	START=$$(cat .ci_val_timer); \
	END=$$(date +%s); \
	ELAPSED=$$((END - START)); \
	MIN=$$((ELAPSED / 60)); \
	SEC=$$((ELAPSED % 60)); \
	echo "⏱️ Valgrind 소요 시간: $$MIN분 $$SEC초"; \
	rm -f .ci_val_timer; \
	if [ $$VAL_FAIL -ne 0 ]; then exit 1; fi

ci:
	@date +%s > .ci_total_timer
	@echo "=========================================================="
	@echo " 👑 전체 통합 CI 파이프라인 가동"
	@echo "=========================================================="
	@$(MAKE) ci-asan
	@$(MAKE) ci-valgrind
	@START=$$(cat .ci_total_timer); \
	END=$$(date +%s); \
	ELAPSED=$$((END - START)); \
	MIN=$$((ELAPSED / 60)); \
	SEC=$$((ELAPSED % 60)); \
	echo ""; \
	echo "🎉 [Iron Fortress CI 전체 통합 검증 완벽 통과] 총 소요 시간: $$MIN분 $$SEC초 BAAAAAAM!!!!"; \
	rm -f .ci_total_timer

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