#!/usr/bin/env python3
"""Generate LEDGEVR.xcodeproj for the iOS port (deterministic pbxproj writer).
Sources: ledgevr-ios/native_ios/*.mm + shared engine from ../ledgevr/native
(+ vendored imgui). Resources: music/sfx/tex folder references (bundle keeps
subdirectory layout). Frameworks: UIKit, Foundation, OpenGLES, QuartzCore,
AVFoundation, CoreGraphics, GLKit.
"""
import os, itertools, uuid, sys

BASE = "/home/z/my-project"
IOS = f"{BASE}/ledgevr-ios"
NATIVE = f"{BASE}/ledgevr/native"
IMGUI = f"{BASE}/ledgevr/vendor/imgui"
PROJ_DIR = f"{IOS}/LEDGEVR.xcodeproj"

_seq = itertools.count(1)
def uid():
    return uuid.uuid4().hex[:24].upper()

def rel(path):  # repo-root-relative, posix
    return os.path.relpath(path, BASE).replace(os.sep, "/")

# --- file inventory -------------------------------------------------------
SRC_FILES = [
    f"{IOS}/native_ios/main_ios.mm",
    f"{IOS}/native_ios/ios_backend.mm",
    f"{IOS}/native_ios/ios_host.mm",
    f"{IOS}/native_ios/AppDelegate.mm",
    f"{IOS}/native_ios/GameViewController.mm",
    f"{NATIVE}/renderer.cpp",
    f"{NATIVE}/world.cpp",
    f"{NATIVE}/photon.cpp",
    f"{NATIVE}/playfab.cpp",
    f"{NATIVE}/save.cpp",
    f"{NATIVE}/textures.cpp",
    f"{IMGUI}/imgui.cpp",
    f"{IMGUI}/imgui_draw.cpp",
    f"{IMGUI}/imgui_tables.cpp",
    f"{IMGUI}/imgui_widgets.cpp",
]
HDR_FILES = [
    f"{IOS}/native_ios/ios_backend.h", f"{IOS}/native_ios/ios_host.h",
    f"{IOS}/native_ios/ios_main.h", f"{IOS}/native_ios/AppDelegate.h",
    f"{IOS}/native_ios/GameViewController.h",
    f"{NATIVE}/mathlib.h", f"{NATIVE}/world.h", f"{NATIVE}/renderer.h",
    f"{NATIVE}/photon.h", f"{NATIVE}/playfab.h", f"{NATIVE}/save.h",
    f"{NATIVE}/anim.h", f"{NATIVE}/textures.h", f"{NATIVE}/assets.h",
    f"{NATIVE}/log.h",
    f"{IMGUI}/imgui.h", f"{IMGUI}/imconfig.h", f"{IMGUI}/imgui_internal.h",
]
for f in SRC_FILES + HDR_FILES:
    if not os.path.exists(f):
        print("MISSING:", f); sys.exit(1)
IMG_INTERNAL = f"{IMGUI}/imgui_internal.h"
if not os.path.exists(IMG_INTERNAL):
    HDR_FILES = [f for f in HDR_FILES if f != IMG_INTERNAL]

FRAMEWORKS = ["UIKit", "Foundation", "OpenGLES", "QuartzCore",
              "AVFoundation", "CoreGraphics", "GLKit"]
RESOURCE_FOLDERS = ["music", "sfx", "tex"]

# --- build objects ---------------------------------------------------------
objs = {}
def add(key, val):
    objs[key] = val
    return key

# file references + build files
fileref = {}
buildfile = {}
for f in SRC_FILES:
    fid, bid = uid(), uid()
    fileref[f] = fid
    buildfile[f] = bid
    add(fid, {"isa": "PBXFileReference", "fileEncoding": "4",
              "lastKnownFileType": "sourcecode.cpp.objcpp" if f.endswith(".mm") else "sourcecode.cpp.cpp",
              "path": os.path.relpath(f, IOS)})
for f in HDR_FILES:
    fileref[f] = uid()
    add(fileref[f], {"isa": "PBXFileReference", "fileEncoding": "4",
                     "lastKnownFileType": "sourcecode.c.h",
                     "path": os.path.relpath(f, IOS)})
for fw in FRAMEWORKS:
    fileref[fw] = uid()
    buildfile[fw] = uid()
    add(fileref[fw], {"isa": "PBXFileReference", "lastKnownFileType": "wrapper.framework",
                      "name": f"{fw}.framework", "path": f"System/Library/Frameworks/{fw}.framework",
                      "sourceTree": "SDKROOT"})
for res in RESOURCE_FOLDERS:
    fileref[res] = uid()
    buildfile[res] = uid()
    add(fileref[res], {"isa": "PBXFileReference", "lastKnownFileType": "folder",
                       "path": f"Resources/{res}"})
infoRef = uid()
add(infoRef, {"isa": "PBXFileReference", "lastKnownFileType": "text.plist.xml",
              "path": "native_ios/Info.plist"})
appRef = uid()
add(appRef, {"isa": "PBXFileReference", "explicitFileType": "wrapper.application",
             "includeInIndex": 0, "path": "LEDGE.app", "sourceTree": "BUILT_PRODUCTS_DIR"})

# groups (paths are relative to the group location)
groupSources = uid()
add(groupSources, {"isa": "PBXGroup", "children":
                   [buildfile[f] for f in SRC_FILES] + [fileref[f] for f in HDR_FILES],
                   "name": "Sources", "sourceTree": "<group>"})
groupFrameworks = uid()
add(groupFrameworks, {"isa": "PBXGroup", "children":
                      [buildfile[fw] for fw in FRAMEWORKS],
                      "name": "Frameworks", "sourceTree": "<group>"})
groupResources = uid()
add(groupResources, {"isa": "PBXGroup", "children":
                     [buildfile[r] for r in RESOURCE_FOLDERS] + [infoRef],
                     "name": "Resources", "sourceTree": "<group>"})
groupProducts = uid()
add(groupProducts, {"isa": "PBXGroup", "children": [appRef],
                    "name": "Products", "sourceTree": "<group>"})
mainGroup = uid()
add(mainGroup, {"isa": "PBXGroup", "children":
                [groupSources, groupFrameworks, groupResources, groupProducts],
                "sourceTree": "<group>"})

# build phases
srcPhase = uid()
add(srcPhase, {"isa": "PBXSourcesBuildPhase", "buildActionMask": 2147483647,
               "files": [buildfile[f] for f in SRC_FILES], "runOnlyForDeploymentPostprocessing": 0})
resPhase = uid()
add(resPhase, {"isa": "PBXResourcesBuildPhase", "buildActionMask": 2147483647,
               "files": [buildfile[r] for r in RESOURCE_FOLDERS], "runOnlyForDeploymentPostprocessing": 0})
fwPhase = uid()
add(fwPhase, {"isa": "PBXFrameworksBuildPhase", "buildActionMask": 2147483647,
              "files": [buildfile[fw] for fw in FRAMEWORKS], "runOnlyForDeploymentPostprocessing": 0})

# configurations
debugId, releaseId = uid(), uid()
COMMON = {
    "HEADER_SEARCH_PATHS": "$(SRCROOT)/shim $(SRCROOT)/../ledgevr/native $(SRCROOT)/../ledgevr/vendor/imgui $(SRCROOT)/native_ios",
    "CLANG_ENABLE_MODULES": "YES",
    "CLANG_ENABLE_OBJC_ARC": "YES",
    "CLANG_CXX_LANGUAGE_STANDARD": '"gnu++17"',
    "GCC_C_LANGUAGE_STANDARD": '"gnu11"',
    "IPHONEOS_DEPLOYMENT_TARGET": "13.0",
    "TARGETED_DEVICE_FAMILY": "1,2",
    "PRODUCT_BUNDLE_IDENTIFIER": "com.moonydev.ledge.ios",
    "PRODUCT_NAME": '"$(TARGET_NAME)"',
    "MARKETING_VERSION": "1.1",
    "CURRENT_PROJECT_VERSION": "2",
    "CODE_SIGN_STYLE": "Automatic",
    "ENABLE_BITCODE": "NO",
    "GENERATE_INFOPLIST_FILE": "NO",
    "INFOPLIST_FILE": "native_ios/Info.plist",
    "GCC_TREAT_WARNINGS_AS_ERRORS": "NO",
}
add(debugId, {"isa": "XCBuildConfiguration",
              "buildSettings": {**COMMON, "GCC_PREPROCESSOR_DEFINITIONS": '"DEBUG=1"',
                                "DEBUG_INFORMATION_FORMAT": "dwarf",
                                "ONLY_ACTIVE_ARCH": "YES"},
              "name": "Debug"})
add(releaseId, {"isa": "XCBuildConfiguration",
                "buildSettings": {**COMMON, "GCC_PREPROCESSOR_DEFINITIONS": '""',
                                  "VALIDATE_PRODUCT": "YES"},
                "name": "Release"})
projDebug, projRelease = uid(), uid()
BASE_SETTINGS = {
    "AlwaysSearchUserPaths": "NO",
    "CLANG_ANALYZER_NONNULL": "YES",
    "CLANG_ENABLE_OBJC_ARC": "YES",
    "CLANG_CXX_LANGUAGE_STANDARD": '"gnu++17"',
    "GCC_C_LANGUAGE_STANDARD": '"gnu11"',
    "SDKROOT": "iphoneos",
}
add(projDebug, {"isa": "XCBuildConfiguration", "buildSettings": BASE_SETTINGS, "name": "Debug"})
add(projRelease, {"isa": "XCBuildConfiguration", "buildSettings": BASE_SETTINGS, "name": "Release"})
confList = uid()
add(confList, {"isa": "XCConfigurationList",
               "buildConfigurations": [releaseId, debugId],
               "defaultConfigurationIsVisible": 0, "defaultConfigurationName": "Release"})
projConfList = uid()
add(projConfList, {"isa": "XCConfigurationList",
                   "buildConfigurations": [projRelease, projDebug],
                   "defaultConfigurationIsVisible": 0, "defaultConfigurationName": "Release"})

# target + project
target = uid()
add(target, {"isa": "PBXNativeTarget",
             "buildConfigurationList": confList,
             "buildPhases": [srcPhase, fwPhase, resPhase],
             "buildRules": [],
             "dependencies": [],
             "name": "LEDGE",
             "productName": "LEDGE",
             "productReference": appRef,
             "productType": "com.apple.product-type.application"})
projObj = uid()
add(projObj, {"isa": "PBXProject",
              "buildConfigurationList": projConfList,
              "compatibilityVersion": "Xcode 13.0",
              "developmentRegion": "en",
              "hasScannedForEncodings": 0,
              "knownRegions": ["en", "Base"],
              "mainGroup": mainGroup,
              "productRefGroup": groupProducts,
              "projectDirPath": "",
              "projectRoot": "",
              "targets": [target]})

# --- serialize ---------------------------------------------------------------
def fmt(v, ind=0):
    pad = "\t" * ind
    if isinstance(v, dict):
        if not v:
            return "{}"
        inner = []
        for k, val in v.items():
            inner.append(f"{pad}\t{k} = " + fmt(val, ind + 1) + ";")
        return "{\n" + "\n".join(inner) + "\n" + pad + "}"
    if isinstance(v, list):
        if not v:
            return "( )"
        inner = [f"{pad}\t{fmt(x, ind + 1)}" for x in v]
        return "(\n" + ",\n".join(inner) + "\n" + pad + ")"
    if isinstance(v, bool):
        return "YES" if v else "NO"
    if isinstance(v, int):
        return str(v)
    s = str(v)
    # quote when: starts with a dot/slash, contains spaces/parens, not a plain id
    if s.startswith('"') or s.startswith("$"):
        return s
    if any(c in s for c in ' \t()":*,') or s[:1].isdigit() and "_" in s:
        return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
    if s.startswith(".") or s.startswith("/"):
        return '"' + s + '"'
    return s

out = ["// !$*UTF8*$!", "{", "\tarchiveVersion = 1;", "\tobjectVersion = 56;",
       "\tclasses = {}", "\tobjects = {"]
for key in objs:
    o = objs[key]
    out.append(f"\t\t{key} = {fmt(o, 2)};")
out.append("\t};")
out.append(f"\trootObject = {projObj};")
out.append("}")
os.makedirs(PROJ_DIR, exist_ok=True)
open(f"{PROJ_DIR}/project.pbxproj", "w").write("\n".join(out) + "\n")
print(f"pbxproj written: {len(objs)} objects, {len(SRC_FILES)} sources, {len(RESOURCE_FOLDERS)} resource folders")
