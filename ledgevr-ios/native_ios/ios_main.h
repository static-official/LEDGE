// LEDGE iOS — entry points exported by main_ios.mm, consumed by GameViewController.mm
#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

void iosMain_init(const char* docsDir, const char* bundleDir);
void iosMain_touch(int phase, uintptr_t id, float x, float y);
void iosMain_resize(void* caeagllayer, int w, int h);
void iosMain_pause();
void iosMain_resume();
// returns true while the app should keep rendering
bool iosMain_frame(bool surfaceReady);
void iosMain_shutdown();

#ifdef __cplusplus
}
#endif
