#!/bin/bash
# Create release v1.3 (if missing) and upload the three v1.3 binaries.
set -e
OWNER=static-official
REPO=LEDGE
BASE=/home/z/my-project
API=https://api.github.com
AUTH="Authorization: token $GH_TOKEN"
TAG=v1.3

RID=$(curl -s $API/repos/$OWNER/$REPO/releases/tags/$TAG -H "$AUTH" \
  | python3 -c "import sys,json
d=json.load(sys.stdin)
print(d['id'] if isinstance(d,dict) and d.get('id') else '')")
if [ -z "$RID" ]; then
  RID=$(curl -sf -X POST $API/repos/$OWNER/$REPO/releases -H "$AUTH" \
    -d '{"tag_name":"v1.3","name":"LEDGE v1.3","body":"iOS rendering fix + landscape fullscreen.\n\n- **iOS/iPad: fixed \"nothing loads\"** — the engine shaders are GLSL `#version 320 es`; Apple GPUs only support OpenGL ES 3.0, and the iOS shader compiler never retargeted them (the Android build did). All shaders now compile as ES 3.0 on iOS — the world, HUD and menu render.\n- **Landscape 16:9 fullscreen everywhere** — iOS now launches and rotates in landscape (matching the Android sensorLandscape behavior); the EAGL surface rebuilds correctly on rotation/resize.\n\n- `LEDGEVR_v1.3_arm64.apk` — Quest/Android (OpenXR) side-load (metadata bump only)\n- `LEDGEVR_v1.3_mobile_arm64.apk` — touch Android side-load (metadata bump only)\n- `LEDGE_v1.3_unsigned.ipa` — iOS/iPad, **unsigned**: sign with your cert (AltStore/Sideloadly/Xcode) before install. Delete the old install first."}' \
    | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
fi
echo "release id: $RID"

for f in LEDGEVR_v1.3_arm64.apk LEDGEVR_v1.3_mobile_arm64.apk LEDGE_v1.3_unsigned.ipa; do
  P=$BASE/download/$f
  [ -s "$P" ] || { echo "MISSING: $P"; exit 1; }
  echo "== upload $f ($(du -h $P | cut -f1)) =="
  curl -sf -X POST "https://uploads.github.com/repos/$OWNER/$REPO/releases/$RID/assets?name=$f" \
    -H "$AUTH" -H "Content-Type: application/octet-stream" --data-binary @$P \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('  ok:', d['name'], d['size'], 'bytes,', d['state'])"
done
echo "=== RELEASE v1.3 UPLOADED ==="
