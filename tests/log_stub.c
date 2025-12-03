#include <stdarg.h>
#include <stdio.h>

void log_component(const char* comp, const char* fmt, ...) { (void)comp; (void)fmt; }
int log_get_level(void) { return 0; }
void log_msg(const char* fmt, ...) { (void)fmt; }
#define LOG_CDROM_DEBUG(...) do {} while(0)
#define LOG_INTERCONNECT_WARN(...) do {} while(0) 