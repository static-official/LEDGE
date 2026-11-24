// iOS shim: asset manager never used on iOS (bundleDir fallback in TexLib),
// just satisfy declarations in textures.h.
#pragma once
#include <cstddef>
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
enum { AASSET_MODE_BUFFER = 3 };
AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode);
size_t AAsset_getLength(AAsset* asset);
const void* AAsset_getBuffer(AAsset* asset);
void AAsset_close(AAsset* asset);
