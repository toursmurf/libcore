🇰🇷 Libcore v1.0 예제 파일 가이드 (Examples Directory)

이 디렉토리는 libcore 프레임워크의 각 모듈별 사용법과 아키텍처를 보여주는 34개의 실전 예제 코드를 포함하고 있습니다. 

모든 예제는 ARC(Automatic Reference Counting) 메모리 관리 규칙을 엄격하게 준수하여 작성되었습니다.

🌐 네트워크 & 이벤트 루프 (Network & EventLoop)

arc_echo_server.c / arc_echo_client.c: 기본적인 TCP 에코 서버 및 클라이언트 구현 예제입니다.

arc_udp_server.c / arc_udp_client.c: UDP 소켓을 이용한 데이터 송수신 예제입니다.

arc_unix_server.c / arc_unix_client.c: 프로세스 간 통신(IPC)을 위한 Unix Domain Socket 사용 예제입니다.

arc_reactor_tcp_unix_server.c: EventLoop를 활용하여 TCP와 Unix 소켓을 동시에 처리하는 멀티플렉싱 서버 예제입니다.

arc_reactor_multi_server.c: 다양한 통신 프로토콜을 단일 스레드(EventLoop)에서 논블로킹으로 처리하는 고성능 원자로(Reactor) 패턴 서버 데모입니다.

arc_chat_server.c: EventLoop 기반의 다중 접속 및 브로드캐스팅을 지원하는 TCP 채팅 서버 구현체입니다.

🗄️ 자료구조 & 컬렉션 (Data Structures & Collections)

arc_hashmap_arraylist_hashtable_test.c: 핵심 컬렉션(HashMap, ArrayList, Hashtable)의 데이터 삽입/조회 및 ARC 소유권 관리 통합 테스트입니다.

arc_vector_test.c / arc_list_test.c / arc_linkedlist_test.c: 동적 배열(Vector) 및 이중/단일 연결 리스트의 동작 검증 예제입니다.

arc_queue_test.c / arc_stack_test.c: FIFO 큐(Queue)와 LIFO 스택(Stack) 컬렉션 테스트입니다.

arc_tree_test.c / arc_btree_test.c: 이진 탐색 트리(BST)와 B-트리(BTree)의 데이터 정렬 및 탐색 검증 예제입니다.

arc_byte_buffer_test.c: Java NIO 스타일의 ByteBuffer를 활용한 바이너리 조작 및 엔디안(Endian) 처리 테스트입니다.

arc_ringbuffer_test.c: 멀티스레드 환경에 최적화된 고성능 원형 버퍼(RingBuffer) 통신 예제입니다.

⚙️ 시스템 & 비즈니스 로직 (System & Core Logic)

arc_thread_test.c: POSIX 스레드 래퍼, Mutex, CondVar, 그리고 동시성 처리를 위한 ThreadPool 동작 검증 예제입니다.

arc_app_context_main.c: AppContext를 통한 애플리케이션 생명주기 관리, 의존성 주입(DI), 서비스 레지스트리(Service Registry) 데모입니다.

arc_config_test.c: INI 설정 파일 로드 및 Config 모듈 파싱 테스트입니다.

arc_scheduler_system_monitor.c: Scheduler를 활용하여 주기적으로 시스템 리소스를 모니터링하는 데모입니다.

arc_scheduler_integration_sentinel.c: 고도화된 타이머와 스케줄러를 결합한 감시(Sentinel) 프로세스 예제입니다.

arc_cron_shared_primitive_Integrated.c: 향후 v1.1 업데이트에 포함될 CronScheduler, SharedMemory(IPC), Primitive Wrapper 모듈의 통합 테스트 프리뷰입니다.

📁 파일, 데이터 & 유틸리티 (File, Data & Utilities)

arc_file_system_test.c: Path, File, Directory 객체를 이용한 파일 시스템 조작 및 FileWatcher(inotify) 감시 테스트입니다.

arc_json_test.c: 내장 JSON 파서 및 직렬화/역직렬화(ObjectMapper) 기능 검증 예제입니다.

arc_log_exception_integration_test.c: 파일/콘솔 로거, 비동기 로거(AsyncLogger), 그리고 ErrorCode 기반의 예외 처리(Exception) 통합 데모입니다.

arc_crypto_integration_test.c: OpenSSL 기반의 AES 암복호화, SHA 해시, Base64 인코딩/디코딩 통합 검증 예제입니다.

arc_mysql_test.c: MySQL/MariaDB 클라이언트 연동, SQL 쿼리 실행 및 트랜잭션 롤백/커밋 테스트입니다.

arc_regex_test.c: String 객체 내부의 정규표현식 매칭 기능 테스트입니다.

🚀 종합 데모 및 클라이언트 (Integration & Clients)

all_test_v2.c: libcore 프레임워크의 모든 40여 개 모듈을 엮어 무결성 및 메모리 누수(Valgrind)를 검증하는 가장 거대한 통합 테스트 스위트입니다.

arc_news_crawler.c: 소켓 통신, 문자열 처리, 자료구조를 모두 결합하여 만든 실전형 웹 뉴스 크롤러(Web Crawler) 데모입니다.

kakaotalk.html: ws_protocol (WebSocket) 또는 API 테스트 결과를 웹 브라우저에서 시각적으로 확인하기 위한 카카오톡 단톡방 스타일의 클라이언트 챗팅 페이지입니다.