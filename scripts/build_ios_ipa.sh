#!/bin/bash
# LEDGE iOS — cross-compile on Linux: NDK clang 17 (Mach-O arm64) + ld64.lld
# + iPhoneOS16.5 SDK (theos/sdks). Produces an UNSIGNED, signable IPA.
#   scripts/build_ios_ipa.sh            -> download/LEDGE_unsigned.ipa
set -e
BASE=/home/z/my-project
SDK=$BASE/toolchain-ios/sdks/iPhoneOS16.5.sdk
NB=$BASE/toolchain/android-ndk-r26d/toolchains/llvm/prebuilt/linux-x86_64/bin
IOS=$BASE/ledgevr-ios
NAT=$BASE/ledgevr/native
IMG=$BASE/ledgevr/vendor/imgui
OUT=$IOS/build_ios
OBJ=$OUT/obj
APP=$OUT/Payload/LEDGE.app
LOG=$OUT/build.log

CSTD="-std=gnu++17 -nostdinc++ -isystem $SDK/usr/include/c++/v1"
COMMON="-target arm64-apple-ios13.0 -isysroot $SDK -B$NB -O2 -fno-strict-aliasing \
  -fvisibility=default -I$IOS/shim -I$NAT -I$NAT/include_openxr -I$IOS/native_ios"
FWS="-framework UIKit -framework Foundation -framework OpenGLES -framework QuartzCore \
  -framework AVFoundation -framework CoreGraphics -framework GLKit"
LIBS="-lobjc -lc++ -lc++abi -lz"

mkdir -p $OBJ $APP
: > $LOG
fail=0

cc() {
  local src=$1; local out=$2; shift 2
  if ! "$NB/clang++" $COMMON $CSTD "$@" -c "$src" -o "$out" 2>>$LOG; then
    echo "FAILED: $src"; fail=1
  else
    echo "ok: $src"
  fi
}

# ---- engine C++ (shared with Android) ----
for f in renderer world photon playfab save textures; do
  cc $NAT/$f.cpp $OBJ/${f}_cpp.o
done
# ---- ledui (ImGui-styled immediate UI) ----
cc $NAT/ledui/ledui.cpp $OBJ/ledui_cpp.o
# ---- iOS platform layer (ObjC++/ARC) ----
for f in main_entry main_ios ios_gl ios_backend ios_host AppDelegate GameViewController; do
  cc $IOS/native_ios/$f.mm $OBJ/${f}_mm.o -fobjc-arc -fblocks
done
# ---- shims: JniHost delegation + AAsset no-ops ----
cc $IOS/shim/jnihost_impl.cpp $OBJ/shim_jnihost.o
cc $IOS/shim/asset_impl.cpp $OBJ/shim_asset.o

if [ $fail -ne 0 ]; then echo "---- compile errors ----"; tail -60 $LOG; exit 1; fi

# ---- link ----
if ! "$NB/clang++" -target arm64-apple-ios13.0 -isysroot $SDK -fuse-ld=lld -B$NB \
    $CSTD $OBJ/*.o -o $APP/LEDGE $FWS $LIBS -dead_strip 2>>$LOG; then
  echo "---- link errors ----"; tail -40 $LOG; exit 1
fi
echo "link ok"
"$NB/llvm-strip" $APP/LEDGE

# ---- package unsigned IPA ----
# literal plist values (Xcode normally substitutes $(...) — a hand-built .app
# must carry real values or dyld can't resolve CFBundleExecutable)
sed -e 's/\$(EXECUTABLE_NAME)/LEDGE/' \
    -e 's/\$(MARKETING_VERSION)/1.1/' \
    -e 's/\$(CURRENT_PROJECT_VERSION)/2/' \
    -e 's/\$(PRODUCT_BUNDLE_IDENTIFIER)/com.moonydev.ledge.ios/' \
    $IOS/native_ios/Info.plist > $APP/Info.plist
echo -n "APPL????" > $APP/PkgInfo
cp -R $IOS/Resources/music $IOS/Resources/sfx $IOS/Resources/tex $APP/
rm -f $BASE/download/LEDGE_unsigned.ipa
cd $OUT && zip -qry $BASE/download/LEDGE_unsigned.ipa Payload
echo "IPA: $BASE/download/LEDGE_unsigned.ipa ($(du -h $BASE/download/LEDGE_unsigned.ipa | cut -f1))"
