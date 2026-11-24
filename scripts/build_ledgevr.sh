#!/bin/bash
# LEDGE VR build script — native OpenXR shell (libv40shell.so) + APK packaging
set -e
TC=/home/z/my-project/toolchain
NDK=$TC/android-ndk-r26d
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
PROJ=/home/z/my-project/ledgevr
BT=$TC/android-14           # build-tools r34
AJ=$TC/android-34-ext12/android.jar
OUT=$PROJ/build
rm -rf $OUT/obj $OUT/dex $OUT/classes $OUT/check
mkdir -p $OUT/libs/arm64-v8a $OUT/obj $OUT/dex

SYS="--sysroot=$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot -isystem $NDK/sources/cxx-stl/llvm-libc++/include -isystem $NDK/sources/cxx-stl/llvm-libc++abi/include"
INC="-I$PROJ/native -I$PROJ/native/include_openxr"
FLAGS="-std=c++17 -O2 -fPIC -march=armv8-a -DANDROID -D__ANDROID_API__=26 -fexceptions -frtti -Wno-unused-parameter -Wno-missing-field-initializers"
FLAGS_C="-O2 -fPIC -march=armv8-a -DANDROID -D__ANDROID_API__=26 -Wno-unused-parameter -Wno-missing-field-initializers"

echo "== compiling native =="
SRC="$PROJ/native/main.cpp $PROJ/native/xr_backend.cpp $PROJ/native/renderer.cpp $PROJ/native/world.cpp $PROJ/native/photon.cpp $PROJ/native/save.cpp $PROJ/native/playfab.cpp $PROJ/native/textures.cpp $PROJ/native/jnihost.cpp $PROJ/native/android_native_app_glue.c $PROJ/native/ledui/ledui.cpp"
for f in $SRC; do
  o=$OUT/obj/$(basename $f | tr './' '__').o
  case $f in
    *.c) $CC/aarch64-linux-android26-clang $FLAGS_C $SYS $INC -c $f -o $o ;;
    *)   $CC/aarch64-linux-android26-clang++ $FLAGS $SYS $INC -c $f -o $o ;;
  esac
done

echo "== linking libv40shell.so =="
$CC/aarch64-linux-android26-clang++ -shared $FLAGS $SYS $OUT/obj/*.o -o $OUT/libs/arm64-v8a/libv40shell.so \
  -lEGL -lGLESv3 -landroid -llog -lz -lopenxr_loader -L$PROJ/libs/arm64-v8a -static-libstdc++ \
  -Wl,--no-undefined 2>/dev/null || $CC/aarch64-linux-android26-clang++ -shared $FLAGS $SYS $OUT/obj/*.o -o $OUT/libs/arm64-v8a/libv40shell.so -lEGL -lGLESv3 -landroid -llog -lz -lopenxr_loader -L$PROJ/libs/arm64-v8a -static-libstdc++

echo "== java → dex (R8) =="

java -jar $TC/ecj.jar -source 8 -target 8 -cp $AJ -d $OUT/classes \
  $PROJ/app/java/com/moonydev/ledgevr/MainActivity.java \
  $PROJ/app/java/com/moonydev/ledgevr/WsBridge.java \
  $PROJ/app/java/com/moonydev/ledgevr/HttpBridge.java \
  $PROJ/app/java/com/moonydev/ledgevr/AudioBridge.java
java -cp $TC/r8.jar com.android.tools.r8.R8 --release --min-api 26 --lib $AJ \
  --no-tree-shaking --no-data-resources \
  --pg-conf $PROJ/app/proguard.rules \
  --output $OUT/dex $OUT/classes/com/moonydev/ledgevr/*.class

echo "== resources =="
$BT/aapt2 compile --dir $PROJ/app/res -o $OUT/res.zip
$BT/aapt2 link -I $AJ --manifest $PROJ/app/AndroidManifest.xml -R $OUT/res.zip \
  --min-sdk-version 26 --target-sdk-version 32 --version-code 4 --version-name 1.3 \
  -o $OUT/base.apk --auto-add-overlay

cp -f $PROJ/libs/arm64-v8a/libopenxr_loader.so $OUT/libs/arm64-v8a/
echo "== package =="
cd $OUT && rm -rf $OUT/assets && cp -r $PROJ/app/assets assets 2>/dev/null || mkdir -p assets
$BT/aapt2 link -I $AJ --manifest $PROJ/app/AndroidManifest.xml -R $OUT/res.zip \
  -A $OUT/assets --min-sdk-version 26 --target-sdk-version 32 \
  --version-code 4 --version-name 1.3 -o $OUT/base.apk --auto-add-overlay
zip -qj $OUT/base.apk $OUT/dex/classes.dex
# native libs MUST live under lib/<abi>/ inside the APK (Android ignores libs/)
mkdir -p $OUT/lib/arm64-v8a
cp -f $OUT/libs/arm64-v8a/*.so $OUT/lib/arm64-v8a/
cd $OUT && zip -qr base.apk lib

echo "== sign =="
[ -f $PROJ/ledgevr.keystore ] || keytool -genkeypair -keystore $PROJ/ledgevr.keystore \
  -alias ledgevr -keyalg RSA -keysize 2048 -validity 10000 -storepass ledgevr123 \
  -dname "CN=MoonyDev, O=MoonyDev" >/dev/null 2>&1
$BT/zipalign -f -p 4 $OUT/base.apk $OUT/aligned.apk
$BT/apksigner sign --v4-signing-enabled false --ks $PROJ/ledgevr.keystore --ks-pass pass:ledgevr123 \
  --out /home/z/my-project/download/LEDGEVR_v1.3_arm64.apk $OUT/aligned.apk
$BT/apksigner verify --print-certs /home/z/my-project/download/LEDGEVR_v1.3_arm64.apk
echo "BUILD OK"
