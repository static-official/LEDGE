// iOS shim: opaque JNI types — declarations only; nothing JNI runs on iOS.
#pragma once
typedef int jint;
typedef long long jlong;
typedef signed char jbyte;
typedef unsigned char jboolean;
typedef struct _jobject* jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jbyteArray;
typedef jobject jmethodID;
typedef struct _JavaVM JavaVM;
typedef struct _JNIEnv JNIEnv;
