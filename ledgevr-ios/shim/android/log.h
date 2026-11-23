// iOS shim: route LOGI/LOGE etc. to NSLog.
#pragma once
#import <Foundation/Foundation.h>
#define LOG_TAG "LEDGEVR"
static inline void ledgeLog(NSInteger lvl, const char* fmt, ...) {
    (void)lvl;
    va_list args;
    va_start(args, fmt);
    NSString* s = [[NSString alloc] initWithFormat:[NSString stringWithUTF8String:fmt] arguments:args];
    va_end(args);
    NSLog(@"[%@] %@", (lvl == 3 ? @"E" : lvl == 2 ? @"W" : @"I"), s);
}
// ANDROID_LOG_* levels from android/log.h
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#define LOGI(...) ledgeLog(4, __VA_ARGS__)
#define LOGW(...) ledgeLog(5, __VA_ARGS__)
#define LOGE(...) ledgeLog(6, __VA_ARGS__)
#define LOGD(...) ledgeLog(3, __VA_ARGS__)
