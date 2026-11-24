#!/bin/bash
# LEDGE toolchain re-download (post-recycle): NDK + build-tools + platform +
# ecj/r8 jars + OpenXR loader (if not in repo) + iPhoneOS SDK. Logs to stdout.
set -e
TC=/home/z/my-project/toolchain
mkdir -p $TC

dl() { # dl <url> <out>
  [ -s "$2" ] && { echo "have: $2"; return 0; }
  echo "get: $1"
  curl -fL --retry 3 --retry-delay 2 -o "$2" "$1" && echo "ok: $2 ($(du -h $2 | cut -f1))"
}

# 1. NDK r26d (~600MB)
dl https://dl.google.com/android/repository/android-ndk-r26d-linux.zip $TC/ndk_r26d.zip
[ -d $TC/android-ndk-r26d ] || unzip -q $TC/ndk_r26d.zip -d $TC/
echo "ndk unzipped"

# 2. build-tools r34 -> android-14
dl https://dl.google.com/android/repository/build-tools_r34-linux.zip $TC/bt34.zip
[ -d $TC/android-14 ] || { mkdir -p $TC/android-14; unzip -q $TC/bt34.zip -d $TC/bt34_tmp && mv $TC/bt34_tmp/*/* $TC/android-14/ && rm -rf $TC/bt34_tmp; }
ls $TC/android-14/aapt2 >/dev/null && echo "build-tools ok"

# 3. platform 34 ext12 -> android.jar
dl https://dl.google.com/android/repository/platform-34-ext12_r01.zip $TC/platform34ext12.zip
[ -f $TC/android-34-ext12/android.jar ] || { mkdir -p $TC/android-34-ext12; unzip -q $TC/platform34ext12.zip -d $TC/p34_tmp && mv $TC/p34_tmp/*/* $TC/android-34-ext12/ && rm -rf $TC/p34_tmp; }
[ -f $TC/android-34-ext12/android.jar ] && echo "android.jar ok"

# 4. ecj + r8
dl https://maven.aliyun.com/repository/public/org/eclipse/jdt/ecj/3.33.0/ecj-3.33.0.jar $TC/ecj.jar
dl https://dl.google.com/android/maven2/com/android/tools/r8/8.3.37/r8-8.3.37.jar $TC/r8.jar

# 5. iPhoneOS16.5 SDK (sparse clone, only that dir)
cd /home/z/my-project/toolchain-ios 2>/dev/null && ls iPhoneOS16.5.sdk >/dev/null 2>&1 || {
  rm -rf /home/z/my-project/toolchain-ios
  git clone --depth 1 --filter=blob:none --sparse https://github.com/theos/sdks.git /home/z/my-project/toolchain-ios 2>&1 | tail -1
  cd /home/z/my-project/toolchain-ios && git sparse-checkout set iPhoneOS16.5.sdk
}
ls /home/z/my-project/toolchain-ios/sdks/iPhoneOS16.5.sdk >/dev/null 2>&1 && echo "ios sdk ok" || echo "ios sdk MISSING"

echo "=== TOOLCHAIN DOWNLOAD COMPLETE ==="
du -sh /home/z/my-project/toolchain /home/z/my-project/toolchain-ios
