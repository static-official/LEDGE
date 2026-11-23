// iOS shim: android/log.h → stderr. Native log.h's LOGI/LOGE call
// __android_log_print, so the shim provides it directly.
#pragma once
#include <cstdio>
#include <cstdarg>
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
static inline int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    (void)prio;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[LEDGE] [%s] ", tag);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    return 0;
}
