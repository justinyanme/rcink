#pragma once

// to avoid warning
char* strrchr(const char* _s, int _c);
unsigned int strlen(const char* _s);

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define log_debugf(fmt, ...) do { \
    tlk_printf("[DEBUG][%d] [%s] [%s:%d] " fmt "%s", clock_time() / 1000 / sys_tick_per_us, __FUNCTION__, __FILENAME__, __LINE__, ##__VA_ARGS__, fmt[strlen(fmt)-1] == '\n' ? "" : "\n"); \
} while (0)

#define TIME_COST_START() unsigned long start_time = clock_time()
#define TIME_COST_END(label) do { \
    unsigned long end_time = clock_time(); \
    unsigned int time_cost = (end_time - start_time) / CLOCK_16M_SYS_TIMER_CLK_1MS; \
    log_debugf("%s: Time cost = %dms", label, time_cost); \
} while(0)

void _log_verbose_helper(const char *fmt, ...);
#define log_verbosef(fmt, ...) do { \
    _log_verbose_helper("[V][%d] [%s] [%s:%d] " fmt "%s", clock_time() / 1000 / sys_tick_per_us, __FUNCTION__, __FILENAME__, __LINE__, ##__VA_ARGS__, fmt[strlen(fmt)-1] == '\n' ? "" : "\n"); \
} while (0)
