#ifndef TIME_UTILS_H
#define TIME_UTILS_H
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint64_t now_ms(void);
uint64_t now_monotonic_ms(void);
void get_current_timestamp(char* buf, size_t size);
void format_time_str(double elapsed_ms, char* buf, size_t size);
#endif