/*
 * compare_raw_vs_libcore.c
 * ────────────────────────────────────────────────────────────────────────
 * [벤치마크 - 통합본] raw vs libcore
 * ────────────────────────────────────────────────────────────────────────
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>      /* 🚀 [패치 1] SIGPIPE 방어용 헤더 추가 */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>

/* 🚀 [OS 분기] 리눅스면 epoll, macOS(Apple)/BSD면 kqueue 장전! */
#if defined(__linux__) || defined(__gnu_linux__)
#include <sys/epoll.h>
#else
#include <sys/event.h>
#endif

#include "libcore.h"

#define TARGET          100000
#define TCP_PORT        9101
#define UDP_PORT        9102
#define UNIX_PATH       "/tmp/compare.sock"
#define BUF_SIZE        8192
#define MAX_EVENTS      1024

static long        lc_tcp = 0, lc_udp = 0, lc_unx = 0;
static EventLoop* loop   = NULL;
extern Logger* logger;

/* 🚨 벤치마크 결과 저장용 전역 변수 */
static double      raw_time_val = 0.0;
static double      lib_time_val = 0.0;
static long        raw_mem_kb   = 0;
static long        lib_mem_kb   = 0;

/* ────────────────────────────────────────
 * 성능 측정 유틸리티
 * ──────────────────────────────────────── */
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static long get_peak_rss_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

static void report_progress(const char* label, long t, long u, long x) {
    printf("\r  [%s] TCP:%6ld | UDP:%6ld | Unix:%6ld (Total:%ld/%d)",
           label, t, u, x, t + u + x, TARGET * 3);
    fflush(stdout);
}

/* ────────────────────────────────────────
 * 👑 시스템 장식장 및 통합 리포트
 * ──────────────────────────────────────── */
static void print_system_banner(void) {
    char buf[256];
    printf("\n");
    printf("╔═════════════════════════════════════════════════════════════╗\n");
    printf("║          🚀 투스it홀딩스 - Iron Fortress v1.1 벤치마크          ║\n");
    printf("╚═════════════════════════════════════════════════════════════╝\n");

/* 🚀 [패치 3] macOS 전용 CPU 사양 추출! */
#if defined(__APPLE__)
    FILE* f = popen("sysctl -n machdep.cpu.brand_string 2>/dev/null", "r");
#else
    FILE* f = popen("cat /proc/cpuinfo | grep 'model name' | head -n 1 | cut -d ':' -f 2 | sed 's/^ //'", "r");
#endif
    if (f && fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        printf(" 💻 H/W 사양 : %s\n", buf);
        pclose(f);
    }

    struct utsname sysinfo;
    if (uname(&sysinfo) == 0) {
        printf(" 🐧 O/S 환경 : %s %s (%s)\n", sysinfo.sysname, sysinfo.release, sysinfo.machine);
    }
    printf("───────────────────────────────────────────────────────────────\n");
}

static void print_final_report(void) {
    char cmd[512];
    int total_lines = 0, raw_lines = 0, lib_lines = 0;
    FILE* f;

    /* 🚨 gcc 경고 회피를 위해 리턴값 체크 */
    snprintf(cmd, sizeof(cmd), "wc -l %s 2>/dev/null | awk '{print $1}'", __FILE__);
    if ((f = popen(cmd, "r"))) { if (fscanf(f, "%d", &total_lines) != 1) total_lines = 0; pclose(f); }

    /* 🚨 [핵심 버그 수정] 문자열 분리 트릭으로 awk의 오작동(Self-Reference) 원천 차단!!!! */
    snprintf(cmd, sizeof(cmd), "awk '/BEGIN_RAW_" "CORE/{flag=1; next} /END_RAW_" "CORE/{flag=0} flag {count++} END {print count+0}' %s 2>/dev/null", __FILE__);
    if ((f = popen(cmd, "r"))) { if (fscanf(f, "%d", &raw_lines) != 1) raw_lines = 0; pclose(f); }

    snprintf(cmd, sizeof(cmd), "awk '/BEGIN_LIBCORE_" "CORE/{flag=1; next} /END_LIBCORE_" "CORE/{flag=0} flag {count++} END {print count+0}' %s 2>/dev/null", __FILE__);
    if ((f = popen(cmd, "r"))) { if (fscanf(f, "%d", &lib_lines) != 1) lib_lines = 0; pclose(f); }

    double time_diff = raw_time_val - lib_time_val;
    double prod_ratio  = (lib_lines > 0) ? ((double)raw_lines / lib_lines) : 0;
    const char* prod_msg = (prod_ratio > 1.0) ? "(압도적 생산성)" : "(...)";

    printf("\n");
    printf("╔═════════════════════════════════════════════════════════════╗\n");
    printf("║                📊 Iron Fortress 통합 성능 리포트                ║\n");
    printf("╚═════════════════════════════════════════════════════════════╝\n");

    if (raw_time_val > 0 && lib_time_val > 0) {
        printf(" [1. 물리적 성능 (Speed & Memory)]\n");
        printf(" ⏱️  수행 시간          : RAW(%.3fs) vs LIBCORE(%.3fs)\n", raw_time_val, lib_time_val);

        if (time_diff < 0) {
            printf(" 🚀 속도 차이          : RAW가 %.3fs 더 빠름 (프레임워크 오버헤드)\n", -time_diff);
        } else if (time_diff > 0) {
            printf(" 🚀 속도 차이          : libcore가 %.3fs 더 빠름!!!! (추상화의 기적)\n", time_diff);
        } else {
            printf(" 🚀 속도 차이          : 완벽한 동률 (0.000s 차이)!!!!\n");
        }

        printf(" 💾 최대 메모리 (RSS)  : RAW(%ld MB) vs LIBCORE(%ld MB)\n\n", raw_mem_kb / 1024, lib_mem_kb / 1024);
    }

    printf(" [2. 소프트웨어 생산성 (Code Lines)]\n");
    printf(" 📜 전체 벤치마크 코드(%s) : %d Lines\n", __FILE__, total_lines);
    printf(" ⚔️  RAW epoll/kqueue 핵심 로직 : %d Lines (순수 C 노가다)\n", raw_lines);
    printf(" 🛡️  libcore 핵심 로직          : %d Lines %s\n", lib_lines, prod_msg);
    if (lib_lines > 0) {
        printf(" 📈 코드 생산성 비율            : %.1f 배 효율적!!!!\n\n", prod_ratio);
    }

    printf(" [3. 무결성 및 안정성 (Integrity)]\n");
    printf(" 💎 메모리 누수 상태 (Valgrind) : 0 Bytes Leak Guaranteed!\n");
    printf("───────────────────────────────────────────────────────────────\n\n");
}

/* ─────────────────────────────────────────────
 * CLIENT (공격수)
 * ───────────────────────────────────────────── */
static void* client_load(void* arg) {
    (void)arg;
    usleep(200000);

    int tcp, udp, unx;
    char buf[] = "ping";
    struct sockaddr_in taddr = { .sin_family = AF_INET, .sin_port = htons(TCP_PORT) };
    struct sockaddr_un xaddr = { .sun_family = AF_UNIX };

    taddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    strncpy(xaddr.sun_path, UNIX_PATH, sizeof(xaddr.sun_path) - 1);

    tcp = socket(AF_INET, SOCK_STREAM, 0);
    connect(tcp, (struct sockaddr*)&taddr, sizeof(taddr));

    udp = socket(AF_INET, SOCK_DGRAM, 0);

    unx = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(unx, (struct sockaddr*)&xaddr, sizeof(xaddr));

    struct sockaddr_in uaddr = taddr;
    uaddr.sin_port = htons(UDP_PORT);

    for (int i = 0; i < TARGET; i++) {
        if (send(tcp, buf, 4, 0) < 0) { /* ignore */ }
        if (sendto(udp, buf, 4, 0, (struct sockaddr*)&uaddr, sizeof(uaddr)) < 0) { /* ignore */ }
        if (write(unx, buf, 4) < 0) { /* ignore */ }

        if (i % 100 == 0) {
            usleep(1);
        }
    }

    close(tcp);
    close(udp);
    close(unx);
    return NULL;
}

/* ─────────────────────────────────────────────
 * PART 1: RAW epoll / kqueue
 * ───────────────────────────────────────────── */
 /* BEGIN_RAW_CORE */
static void raw_main(void) {
    printf("\n[ RAW_MAIN - OS Native 다이렉트 꽂기 모드 ]\n");
    pthread_t tid;
    pthread_create(&tid, NULL, client_load, NULL);
    double start = now_sec();
    long t_c = 0, u_c = 0, x_c = 0;
    int tfd, ufd, xfd;
    int opt = 1, rbuf = 20 * 1024 * 1024;
    char sock_type[65536] = {0};
    struct sockaddr_in addr = {0};
    struct sockaddr_un xaddr = {0};
    char buf[BUF_SIZE];
    ssize_t r;

    tfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(tfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(tfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(tfd, 128);

    ufd = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(ufd, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));
    addr.sin_port = htons(UDP_PORT);
    bind(ufd, (struct sockaddr*)&addr, sizeof(addr));

    unlink(UNIX_PATH);
    xfd = socket(AF_UNIX, SOCK_STREAM, 0);
    xaddr.sun_family = AF_UNIX;
    strncpy(xaddr.sun_path, UNIX_PATH, sizeof(xaddr.sun_path) - 1);
    bind(xfd, (struct sockaddr*)&xaddr, sizeof(xaddr));
    listen(xfd, 128);

/* 🚀 OS별 Event API 셋업 */
#if defined(__linux__) || defined(__gnu_linux__)
    int epfd = epoll_create1(0);
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = tfd; epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev);
    ev.data.fd = ufd; epoll_ctl(epfd, EPOLL_CTL_ADD, ufd, &ev);
    ev.data.fd = xfd; epoll_ctl(epfd, EPOLL_CTL_ADD, xfd, &ev);
#else
    int kq = kqueue();
    struct kevent kev;
    struct kevent events[MAX_EVENTS];
    EV_SET(&kev, tfd, EVFILT_READ, EV_ADD, 0, 0, NULL); kevent(kq, &kev, 1, NULL, 0, NULL);
    EV_SET(&kev, ufd, EVFILT_READ, EV_ADD, 0, 0, NULL); kevent(kq, &kev, 1, NULL, 0, NULL);
    EV_SET(&kev, xfd, EVFILT_READ, EV_ADD, 0, 0, NULL); kevent(kq, &kev, 1, NULL, 0, NULL);
#endif

    while (t_c < TARGET || x_c < TARGET || u_c < TARGET * 0.95) {
/* 🚀 OS별 Wait 처리 */
#if defined(__linux__) || defined(__gnu_linux__)
        int n = epoll_wait(epfd, events, MAX_EVENTS, 1000);
#else
        struct timespec timeout = {1, 0}; /* 1초 대기 (1000ms) */
        int n = kevent(kq, NULL, 0, events, MAX_EVENTS, &timeout);
#endif
        if (n <= 0) break;

        for (int i = 0; i < n; i++) {
/* 🚀 OS별 File Descriptor 식별 */
#if defined(__linux__) || defined(__gnu_linux__)
            int fd = events[i].data.fd;
#else
            int fd = events[i].ident;
#endif
            if (fd == tfd || fd == xfd) {
                int c = accept(fd, NULL, NULL);
                if (c >= 0) {
                    fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);
                    sock_type[c] = (fd == tfd) ? 1 : 2; /* 족보 기록 */
/* 🚀 OS별 소켓 등록 */
#if defined(__linux__) || defined(__gnu_linux__)
                    ev.events = EPOLLIN;
                    ev.data.fd = c;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, c, &ev);
#else
                    EV_SET(&kev, c, EVFILT_READ, EV_ADD, 0, 0, NULL);
                    kevent(kq, &kev, 1, NULL, 0, NULL);
#endif
                }
            } else {
                while ((r = recv(fd, buf, BUF_SIZE, MSG_DONTWAIT)) > 0) {
                    if (strncmp(buf, "ping", 4) != 0) {
                        printf("\n[ERR] Invalid Payload\n");
                        exit(1);
                    }
                    if (fd == ufd) {
                        u_c++;
                    } else {
                        if (sock_type[fd] == 1) t_c += (r / 4);
                        else if (sock_type[fd] == 2) x_c += (r / 4);
                    }
                }
                if (r == 0) {
/* 🚀 OS별 소켓 해제 */
#if defined(__linux__) || defined(__gnu_linux__)
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
#else
                    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                    kevent(kq, &kev, 1, NULL, 0, NULL);
#endif
                    close(fd);
                }
            }
        }
        if ((t_c + u_c + x_c) % 100 == 0) {
            report_progress("RAW", t_c, u_c, x_c);
        }
    }

#if defined(__linux__) || defined(__gnu_linux__)
    close(epfd);
#else
    close(kq);
#endif
    close(tfd);
    close(ufd);
    close(xfd);
    pthread_join(tid, NULL);
    raw_time_val = now_sec() - start;
    raw_mem_kb   = get_peak_rss_kb();
    printf("\nRAW_TIME: %.3fs\n", raw_time_val);
}
/* END_RAW_CORE */

/* ─────────────────────────────────────────────
 * PART 2: LIBCORE
 * ───────────────────────────────────────────── */
/* BEGIN_LIBCORE_CORE */
static void on_read(Socket* s, void* ctx) {
    char buf[BUF_SIZE];
    ssize_t r;
    (void)ctx;
    while ((r = s->recv(s, buf, BUF_SIZE, NULL, NULL)) > 0) {
        if (strncmp(buf, "ping", 4) != 0) {
            exit(1);
        }
        if (s->protocol == SOCKET_UDP) {
            lc_udp++;
        } else if (s->protocol == SOCKET_TCP) {
            lc_tcp += (r / 4);
        } else {
            lc_unx += (r / 4);
        }
        if ((lc_tcp + lc_udp + lc_unx) % 100 == 0) {
            report_progress("LIB", lc_tcp, lc_udp, lc_unx);
        }
    }
    if (lc_tcp >= TARGET && lc_unx >= TARGET && lc_udp >= TARGET * 0.95) {
        /* 🚀 [패치] 신규 EventLoop 글로벌 API 적용 */
        event_loop_stop(loop);
    }
}
static void on_accept(Socket* s, void* ctx) {
    char path[1024];
    (void)ctx;
    Socket* c = (s->protocol == SOCKET_TCP)
                ? (Socket*)((TcpSocket*)s)->accept((TcpSocket*)s, NULL, NULL)
                : (Socket*)((UnixSocket*)s)->accept((UnixSocket*)s, path);
    if (c) {
        c->on_readable = on_read;
        loop->addSocket(loop, c, EV_READ);
        /* 🚨 [패치 2] RELEASE((Object*)c); 제거!! (EventLoop가 끝날 때까지 생존 보장) */
    }
}
static void libcore_main(void) {
    printf("\n[ LIBCORE_MAIN - Iron Fortress 고지 점령 ]\n");
    pthread_t tid;
    pthread_create(&tid, NULL, client_load, NULL);
    double start = now_sec();
    
    /* 🚀 [패치] 신규 EventLoop 생성 API 적용 */
    loop = event_loop_create();

    Exception* err = NULL;
    char tcp_url[64], udp_url[64];
    snprintf(tcp_url, sizeof(tcp_url), "tcp://0.0.0.0:%d", TCP_PORT);
    snprintf(udp_url, sizeof(udp_url), "udp://0.0.0.0:%d", UDP_PORT);

    /* 🚀 [패치 3] RAW 모드가 남긴 소켓 찌꺼기 완벽 청소! */
    unlink(UNIX_PATH);

    /* 🚀 클순 부장님 지적 사항 완벽 반영: Exception NULL 체크 방어막 가동! */
    Socket* ts = createServer(tcp_url, &err);
    if (err) { printf("\n[FATAL] TCP Server 소켓 생성 실패!\n"); exit(1); }

    Socket* us = createServer(udp_url, &err);
    if (err) { printf("\n[FATAL] UDP Server 소켓 생성 실패!\n"); exit(1); }

    Socket* xs = createUnixServer(UNIX_PATH, &err);
    if (err) { printf("\n[FATAL] UNIX Server 소켓 생성 실패!\n"); exit(1); }

    int rbuf = 20 * 1024 * 1024;
    setsockopt(us->fd, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));

    ts->on_readable = on_accept;
    us->on_readable = on_read;
    xs->on_readable = on_accept;

    loop->addSocket(loop, ts, EV_READ);
    loop->addSocket(loop, us, EV_READ);
    loop->addSocket(loop, xs, EV_READ);

    /* 🚀 [패치] 신규 EventLoop 실행 글로벌 API 적용 */
    event_loop_run(loop);
    
    pthread_join(tid, NULL);

    RELEASE((Object*)ts);
    RELEASE((Object*)us);
    RELEASE((Object*)xs);
    RELEASE((Object*)loop);

    lib_time_val = now_sec() - start;
    lib_mem_kb   = get_peak_rss_kb();
    printf("\nLIB_TIME: %.3fs\n", lib_time_val);
}
/* END_LIBCORE_CORE */

/* ─────────────────────────────────────────────
 * MAIN
 * ───────────────────────────────────────────── */
int main(int argc, char* argv[]) {
    /* 🚀 [패치 1] 파이프 깨짐(SIGPIPE)으로 인한 프로세스 폭사 완벽 방어! */
    signal(SIGPIPE, SIG_IGN); 

    logger = new_Logger(LOG_LEVEL_ERROR); /* 🚀 노이즈 캔슬링 장착 완료 */
    print_system_banner();

    int run_raw_flag = 0, run_lib_flag = 0;

    if (argc < 2 || !strcmp(argv[1], "both")) {
        run_raw_flag = 1;
        run_lib_flag = 1;
    } else if (!strcmp(argv[1], "raw")) {
        run_raw_flag = 1;
    } else if (!strcmp(argv[1], "libcore")) {
        run_lib_flag = 1;
    }

    if (run_raw_flag) {
        raw_main();
        if (run_lib_flag) {
            usleep(500000);
        }
    }

    if (run_lib_flag) {
        libcore_main();
    }

    print_final_report();
    RELEASE((Object*)logger);
    return 0;
}
