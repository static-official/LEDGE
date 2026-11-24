// iOS shim: EGL types used by xr_backend.h declarations only.
#pragma once
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef unsigned int EGLBoolean;
#define EGL_NO_DISPLAY   ((EGLDisplay)0)
#define EGL_NO_CONTEXT   ((EGLContext)0)
#define EGL_NO_SURFACE   ((EGLSurface)0)
