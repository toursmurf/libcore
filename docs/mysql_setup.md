## MySQL 연동 빌드
make WITH_MYSQL=1

## 라이브러리 경로 지정
MYSQL_INC=/usr/include/mysql
MYSQL_LIB=/usr/lib/x86_64-linux-gnu

make  WITH_MYSQL=1 \
MYSQL_INC=... \
MYSQL_LIB=...

## 테스트
./examples/arc_mysql_test