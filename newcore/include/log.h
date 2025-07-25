#ifndef NC_LOG_H
#define NC_LOG_H

#define NC_LOG_INFO 1
#define NC_LOGE 2
#define NC_LOGI 3
#define NC_LOGW 4
#define NC_LOGD 5
#define NC_LOGT 6

void nc_log_set_level(int level);
void nc_log_msg(int level, const char* fmt, ...);

// Function-like logging macros
#define LOGE(...) nc_log_msg(NC_LOGE, __VA_ARGS__)
#define LOGI(...) nc_log_msg(NC_LOGI, __VA_ARGS__)
#define LOGW(...) nc_log_msg(NC_LOGW, __VA_ARGS__)
#define LOGD(...) nc_log_msg(NC_LOGD, __VA_ARGS__)
#define LOGT(...) nc_log_msg(NC_LOGT, __VA_ARGS__)

// For compatibility with code using NC_LOGI, NC_LOGD, etc. as function-like macros
#undef NC_LOGE
#undef NC_LOGI
#undef NC_LOGW
#undef NC_LOGD
#undef NC_LOGT
#define NC_LOGE(...) nc_log_msg(2, __VA_ARGS__)
#define NC_LOGI(...) nc_log_msg(3, __VA_ARGS__)
#define NC_LOGW(...) nc_log_msg(4, __VA_ARGS__)
#define NC_LOGD(...) nc_log_msg(5, __VA_ARGS__)
#define NC_LOGT(...) nc_log_msg(6, __VA_ARGS__)

#endif // NC_LOG_H 