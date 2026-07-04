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

# 🛠️ 2-B. 빌드 모드 및 Sanitizer 동기화
DEBUG ?= 0
ifeq ($(DEBUG),1)
    MODE_STR = Debug (-O0 -g)
    SANITIZER_STR = ON (ASan, UBSan)
    CFLAGS += -O0 -g -fsanitize=address,undefined
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

.PHONY: all clean test examples

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

clean:
	@rm -f $(SRC_DIR)/*.o
	@rm -f $(LIB_DIR)/libcore.a
	@rm -f $(TEST_DIR)/run_test
	@rm -f $(EXAMPLE_BINS)
	@echo "🧹 클린 완료!"