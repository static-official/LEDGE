# Building LEDGE for iOS

The iOS port shares the entire game core with the Android build (renderer,
world/physics, Photon realtime + voice, PlayFab, saves, editor, textures,
anim/particle/emote system). Only the platform layer is iOS-specific.

## What maps where

| Android | iOS |
| --- | --- |
| `android_native_app_glue` + EGL (touch_backend) | UIKit + EAGL (`native_ios/ios_backend.mm`) |
| Java `WsBridge` / `HttpBridge` over JNI | `native_ios/ios_host.mm` — URLSessionWebSocketTask / NSURLSession |
| Java `AudioBridge` (MediaPlayer + SoundPool, OGG) | `AVAudioPlayer` playlist + voice pool (AAC — `Resources/*.m4a`) |
| `AAssetManager` texture loading | bundle file loading (`TexLib::setBundleDir`, shared code path) |
| OpenXR shell (`main.cpp`, VR) | not ported — iOS build is the touch game (`main_ios.mm`) |

Shared engine headers use `<GLES3/gl32.h>`, `<android/log.h>`,
`<android/asset_manager.h>`; the iOS target resolves these from
`ledgevr-ios/shim/` (HEADER_SEARCH_PATHS) which map to Apple's OpenGLES and
NSLog — no shared-file changes needed.

## Requirements

- macOS with Xcode 15+ (or 13+, only iOS 13 SDK APIs are used)
- Apple ID (free is fine for a device install; paid developer account for TestFlight/App Store)
- Python 3 only if you regenerate the project (`scripts/gen_xcodeproj.py`)

## Build steps

```bash
# 1. resources are already committed (AAC + textures in ledgevr-ios/Resources)
#    if you regenerate them:
#    for f in ledgevr/app/assets/music/*.ogg ledgevr/app/assets/sfx/*.ogg; do
#      ffmpeg -i "$f" -c:a aac -b:a 128k "ledgevr-ios/Resources/${f%.ogg}.m4a"; done
#    cp ledgevr/app/assets/tex/*.png ledgevr-ios/Resources/tex/

# 2. open the project
open ledgevr-ios/LEDGEVR.xcodeproj

# 3. set a signing team in Signing & Capabilities, plug in a device, Cmd+R.

# command line:
xcodebuild -project ledgevr-ios/LEDGEVR.xcodeproj -scheme LEDGE \
  -configuration Release -destination 'generic/platform=iOS' build
```

## Cross-building the IPA from Linux (no Mac needed)

`scripts/build_ios_ipa.sh` cross-compiles the whole app with the NDK's clang
(target arm64-apple-ios13.0), links with `ld64.lld`, and packages an
**unsigned, signable IPA** using the iPhoneOS16.5 SDK headers
(`toolchain-ios/sdks/`, sparse-cloned from theos/sdks):

```bash
bash scripts/build_ios_ipa.sh     # -> download/LEDGE_unsigned.ipa (~40 MB)
```

The IPA carries `Payload/LEDGE.app` with literal Info.plist values
(CFBundleExecutable=LEDGE, version 1.1). It is unsigned: code-sign it with
Xcode, `codesign`, AltStore, SideStore, Sideloadly, ESign, etc. before install.

iOS-only support files: `shim/` (GLES/EGL/JNI/log shims + JniHost/AAsset
delegation), `native_ios/ios_gl.mm` (GL helpers the shared renderer expects),
`native_ios/main_entry.mm` (UIApplicationMain).

## Notes & limits

- Deploy target is iOS 13.0 (URLSessionWebSocketTask). ES 3.2 shaders need an
  A12-class device or newer (iPhone XR/XS and up) — same class of hardware the
  3.2 pipeline was written for.
- EAGL is deprecated by Apple but fully functional; the renderer is plain
  OpenGL ES, so a Metal migration is optional future work.
- Voice chat joins the same Photon voice WebSocket as the Android/VR builds.
- The engine's PNG decoder is shared; iOS loads the same `tex/*.png` files
  from the bundle.
- Mic recording is not wired up on iOS yet (`micPermission` stays false —
  open-mic voice will transmit nothing; receiving still works). Add the
  NSMicrophoneUsageDescription key + permission prompt when you wire it.
