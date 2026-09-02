# libcore WebBoard — 내부 동작 소개

> libcore v1.7.2 기준 | 작성: 2026.08.20

---

## 1. 전체 구조 개요

```
브라우저
  │  HTTP Request
  ▼
HttpServer  (src/http_server.c)
  │  파싱 완료된 HttpRequest 전달
  ▼
Router  (src/router.c)
  │  URL + Method 매칭
  ▼
BoardHandler  (src/board_handler.c)
  │  비즈니스 로직 처리
  │  DB 조회 / 파일 I/O
  ▼
TemplateEngine  (src/board_template_engine.c)
  │  HTML SSR 렌더링
  ▼
HttpResponse → 브라우저
```

libcore는 PHP·Node.js 같은 런타임 없이 **순수 C99**로 HTTP 서버부터 라우터, 템플릿 엔진, DB 연동까지 구현한 네이티브 백엔드 프레임워크입니다.

---

## 2. 서버 기동 흐름 (arc_board_server.c)

서버를 실행하면 `main()` 에서 다음 순서로 초기화합니다.

```
1. Config 로드       app.conf 파싱
                     (base_dir / upload_dir / tpl_dir / DB 설정)

2. Logger 초기화     base_dir + log_file 로 절대경로 합성

3. DBClient 생성     MariaDB 연결

4. PathValidator     파일 경로 보안 검증기 생성

5. BoardHandler 생성 DB + PathValidator + 경로 3종 주입
                     생성자에서 skin(white/dark) DB 조회 및 초기화

6. Router 생성       BoardHandler를 user_ctx 로 등록
                     라우트 13개 등록

7. EventLoop + HttpServer 기동
                     macOS: kqueue 백엔드
                     Linux: epoll 백엔드
```

---

## 3. HTTP 요청 처리 흐름

### 3-1. 네트워크 레이어

```
클라이언트 TCP 접속
  │
EventLoop (kqueue/epoll)
  │  fd 이벤트 감지
  │
HttpServer.recv()
  │  HTTP 헤더 + 바디 파싱
  │  multipart/form-data 파싱 (파일 업로드 시)
  │  application/json 파싱 (코멘트 등)
  │
Router.dispatch()
  │  [Method + Path] 매칭
  ▼
```

### 3-2. 라우트 테이블

| Method | Path | 핸들러                           |
|--------|------|----------------------------------|
| GET | / | 게시글 목록                      |
| GET | /board/list | 게시글 목록                      |
| GET | /board/write | 새글쓰기폼                       |
| POST | /board/write | 글쓰기 저장                      |
| GET | /board/:id | 글상세보기, mode=modify 글수정폼 |
| PUT | /board/:id | 글 수정                          |
| DELETE | /board/:id | 글 삭제                          |
| GET | /board/attach/:id | 첨부파일 다운로드                |
| POST | /board/:id/comment | 코멘트 작성                      |
| PUT | /board/comment/:id | 코멘트 수정                      |
| DELETE | /board/comment/:id | 코멘트 삭제                      |
| POST | /board/skin | 스킨 변경                        |
| POST | /board/limit | 목록 수 변경                     |

---

## 4. BoardHandler 내부 동작

### 4-1. 경로 설계 (base_dir 분리)

배포 환경에 독립적으로 동작하기 위해 경로를 분리합니다.

```
app.conf:
  base_dir   = /home/board/html        ← 절대경로 (환경마다 다름)
  upload_dir = uploads                 ← 상대경로 (DB 저장값)
  tpl_dir    = templates               ← 상대경로

파일 저장 시:
  abs_path = base_dir + "/" + upload_dir + "/" + filename
  DB 저장  = upload_dir + "/" + filename  (상대경로만!)

파일 읽기 시:
  abs_path = base_dir + "/" + saved_name
```

base_dir 하나만 바꾸면 맥북·리눅스 서버 어디서든 동작합니다.

### 4-2. 게시글 목록 (list)

```
1. DB에서 board_list 설정 조회     (페이지당 출력 수, DB 영속)
2. refresh_skin()                  (현재 스킨 확인 + skinDir 갱신)
3. URL에서 page, search_type, keyword 파싱
4. SQL LIKE 쿼리 실행              (검색 + 페이지네이션)
5. JSONNode 컨텍스트 조립          (posts, pages, selected 등)
6. TemplateEngine.renderFile()     (index.html SSR)
7. HTML 응답 전송
```

### 4-3. 게시글 상세보기 + 수정 (detail/modify)

상세보기와 수정은 **같은 URL, 같은 HTML**을 사용합니다.

```
GET /board/27          → mode-read  (readonly 속성)
GET /board/27?mode=modify → mode-modify (입력 가능)

CSS로 분기:
  .mode-read  .modify-only { display: none }
  .mode-modify .read-only  { display: none }
```

수정 저장(PUT) 시:
```
1. 기존 첨부파일 saved_name 먼저 조회  (old_path 확보)
2. 새 파일 있으면 디스크 저장          (파일명: timestamp_seq.ext)
3. beginTransaction()
4. board_posts UPDATE
5. board_attachments UPDATE or INSERT
6. commit()
7. commit 성공 후 old 파일 물리 삭제   (DB 먼저, 파일 나중)
```

### 4-4. 코멘트 2단 구조

```
depth=0 : 1단 코멘트 (parent_id = NULL)
depth=1 : 2단 답글   (parent_id = 상위 코멘트 id)
depth≥2 : 차단       (3단 이상 금지)

삭제 시:
  depth=0 삭제 → 자식 답글도 cascade soft delete
  depth=1 삭제 → 해당 답글만 soft delete
```

### 4-5. 첨부파일 보안

```
파일명 sanitize:
  1. basename 추출  (경로 탐색 공격 방지)
  2. 허용 확장자 화이트리스트 검증
     .jpg .png .pdf .docx .xlsx .pptx .zip 등
  3. 실패 시 → untitled.bin (화이트리스트에 없어 자동 차단)

저장 파일명:
  {timestamp}_{microsec}_{seq}.{ext}
  → 원본 파일명은 DB(file_name)에만 저장
  → 디스크는 무작위명으로 저장 (충돌 방지)

다운로드:
  Content-Disposition: attachment; filename="원본파일명"
  → 브라우저에서 원본 파일명으로 저장됨
```

---

## 5. BoardTemplateEngine SSR

별도 JavaScript 런타임 없이 **C로 구현한 서버사이드 렌더링** 엔진입니다.

### 지원 문법

```
{{key}}           → 단순 변수 치환 (HTML escape 적용)

{{#array}}        → 배열 루프 시작
  {{key}}         → 루프 내 변수
{{/array}}        → 루프 종료

{{#flag}}         → 조건부 렌더링 (값이 있으면 출력)
{{/flag}}
```

### 동작 예시

```c
// C 서버에서:
JSONNode* resp = new_JSON_Object();
resp->put(resp, "title", new_JSON_String("게시글 제목"));

JSONNode* posts = new_JSON_Array();
// ... 포스트 추가 ...
resp->put(resp, "posts", posts);

html_out = engine->renderFile(engine,
    "templates/skin/white/index.html", resp);
```

```html
<!-- HTML 템플릿에서: -->
<h1>{{title}}</h1>
{{#posts}}
<tr>
  <td>{{no}}</td>
  <td><a href="/board/{{id}}">{{title}}</a></td>
</tr>
{{/posts}}
```

---

## 6. White / Dark 스킨 시스템

```
board_config 테이블:
  cfg_name  = 'skin'
  cfg_value = 'white' | 'dark'

템플릿 디렉터리 구조:
  templates/skin/white/
    index.html
    board_write.html
    board_read.html

  templates/skin/dark/
    index.html
    board_write.html
    board_read.html

스킨 전환:
  POST /board/skin → DB UPSERT + in-memory skinDir 갱신
  → 서버 재시작 없이 즉시 반영
```

---

## 7. ARC 메모리 관리

libcore는 GC 없이 **ARC(Automatic Reference Counting)** 방식으로 메모리를 관리합니다.

```c
/* 생성 */
JSONNode* node = new_JSON_String("hello");

/* 사용 후 해제 */
RELEASE((Object*)node);

/* HashMap/ArrayList에 넣으면 소유권 이전 */
resp->put(resp, "key", (Object*)node);
RELEASE((Object*)node);   /* resp가 소유, 여기서 ref 감소 */

/* resp 해제 시 내부 모든 노드 연쇄 해제 */
RELEASE((Object*)resp);
```

---

## 8. 서버 실행 방법

### 설정 파일 (examples/board/app.conf)

```ini
host       = 0.0.0.0
port       = 8888

db_host    = 127.0.0.1
db_port    = 3306
db_user    = board
db_pass    = your_password
db_name    = board

base_dir   = /your/path/webboard_libcore
upload_dir = examples/board/uploads
tpl_dir    = examples/board/templates
log_file   = logs/board.log
```

### 빌드 및 실행

```bash
# 릴리즈 빌드
make clean examples

# ASan/UBSan 디버그 빌드
make clean examples DEBUG=1 USE_ASAN=1

# 서버 실행
./examples/arc_board_server
```

### 접속

```
http://127.0.0.1:8888/board/list
```

---

## 9. 기술 스택 요약

| 항목 | 내용 |
|------|------|
| 언어 | C99 |
| 네트워크 | 직접 구현 (kqueue/epoll) |
| HTTP | 직접 파싱 |
| 라우터 | libcore Router |
| 템플릿 엔진 | BoardTemplateEngine (C 구현) |
| DB | MariaDB (libmariadb) |
| 메모리 관리 | ARC (RELEASE 매크로) |
| 보안 | PathValidator, 화이트리스트, SQL escape |
| 빌드 | GNU Make |
| 플랫폼 | macOS (kqueue) / Linux (epoll) |
