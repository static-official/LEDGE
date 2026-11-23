// LEDGE iOS shim — AAsset API no-ops. The am_ branch in textures.cpp never
// runs on iOS (bundleDir path is used), but the calls need symbols to link.
#include "android/asset_manager.h"

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
    (void)mgr; (void)filename; (void)mode;
    return nullptr;
}
size_t AAsset_getLength(AAsset* asset) { (void)asset; return 0; }
const void* AAsset_getBuffer(AAsset* asset) { (void)asset; return nullptr; }
void AAsset_close(AAsset* asset) { (void)asset; }
