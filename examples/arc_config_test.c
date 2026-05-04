/**
 * @file arc_config_test.c
 * @brief 🇰🇷 INI 설정 파일 로드 및 Config 모듈 파싱 테스트입니다.
 * 🇬🇧 INI configuration file loading and Config module parsing test.
 * @note  This example strictly follows the ARC memory management rules.
 */

#include <stdio.h>
#include <stdlib.h>
#include "object.h"
#include "logger.h"  
#include "config.h"

int main(int argc, char* argv[]) {
   (void) argc;
   (void) argv;
    // 1. Logger 초기화 (가장 먼저 가동)
    Logger* logger = new_Logger(LOG_LEVEL_INFO);
    LOG_INFO(logger, "NMS Core Engine Start...");

    // 2. Config 객체 생성 및 로드
    Config* cfg = new_Config();
    if (cfg->load(cfg, "collector.ini")) {
        LOG_INFO(logger, "collector.ini 설정 파일 로드 성공!");

        // 설정값 추출
        bool trap_enabled = cfg->getBool(cfg, "trap_enabled", false);
        int  trap_port    = cfg->getInt(cfg, "trap_port", 162);
        
        bool syslog_enabled = cfg->getBool(cfg, "syslog_enabled", false);
        int  syslog_port    = cfg->getInt(cfg, "syslog_port", 514);

	const char * db_host = cfg->getString(cfg, "db_host", "localhost");
	const char * db_name = cfg->getString(cfg, "db_name", NULL);
	const char * db_user = cfg->getString(cfg, "db_user", NULL);
	const char * db_pass = cfg->getString(cfg, "db_pass", NULL);
	int  db_port = cfg->getInt(cfg, "db_port", 3306);
	const char * db_charset = cfg->getString(cfg, "db_charset", "utf-8");
	const char *log_lvl = cfg->getString(cfg, "log_level", "ERROR");

        LOG_INFO(logger, "운영 모드 - LOG_LEVEL: %s, Trap(Port: %d, %s) / Syslog(Port: %d, %s)",
		 log_lvl, 
                 trap_port, trap_enabled ? "ON" : "OFF",
                 syslog_port, syslog_enabled ? "ON" : "OFF");

        LOG_INFO(logger, "운영 모드 디비정보 - DB(db_host: %s, db_name: %s, db_user: %s, db_pass: %s, db_port=%d, db_charset: %s)",
		 db_host, db_name, db_user, db_pass, db_port, db_charset); 
        // ==========================================
        // 💡 향후 작업: UDP 162/514 서버 구동 및 EventLoop
        // ==========================================
        
    } else {
        LOG_ERROR(logger, "collector.ini 로드 실패. 기본 설정으로 강제 구동합니다.");
    }

    // 3. 엔진 종료 시퀀스 (🔥 순서 절대 엄수 🔥)
    RELEASE((Object*)cfg);
    LOG_INFO(logger, "Config 객체 메모리 해제 완료. (Valgrind 0 bytes 검증 대기)");

    LOG_INFO(logger, "NMS Core Engine Shutdown.");
    
    // 시스템의 마지막 순간, 로거 최종 소각!!
    RELEASE((Object*)logger); 

    return 0;
}
