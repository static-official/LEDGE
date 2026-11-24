#!/bin/bash
# Create release v1.2 (if missing) and upload the three v1.2 binaries.
set -e
OWNER=static-official
REPO=LEDGE
BASE=/home/z/my-project
API=https://api.github.com
AUTH="Authorization: token $GH_TOKEN"
TAG=v1.2

RID=$(curl -s $API/repos/$OWNER/$REPO/releases/tags/$TAG -H "$AUTH" \
  | python3 -c "import sys,json
d=json.load(sys.stdin)
print(d['id'] if isinstance(d,dict) and d.get('id') else '')")
if [ -z "$RID" ]; then
  RID=$(curl -sf -X POST $API/repos/$OWNER/$REPO/releases -H "$AUTH" \
    -d '{"tag_name":"v1.2","name":"LEDGE v1.2","body":"iPad + iOS fixes.\n\n- **iOS/iPad: fixed stuck pastel screen** — renderer/fonts/level now init on the first frame no matter which of (EAGL surface, first frame) arrives first\n- **iPad: real fullscreen** — app now declares iPad device family + full-screen portrait, so iPadOS no longer shrinks it into the iPhone compat window\n- Music (7 tracks) + SFX verified in the iOS bundle; Android unchanged (already immersive fullscreen), version bumps to 1.2\n\n- `LEDGEVR_v1.2_arm64.apk` — Quest/Android (OpenXR) side-load\n- `LEDGEVR_v1.2_mobile_arm64.apk` — touch Android side-load\n- `LEDGE_v1.2_unsigned.ipa` — iOS/iPad, **unsigned**: sign with your cert (AltStore/Sideloadly/Xcode) before install. On iPad: delete the old install first, then re-sign this one."}' \
    | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
fi
echo "release id: $RID"

for f in LEDGEVR_v1.2_arm64.apk LEDGEVR_v1.2_mobile_arm64.apk LEDGE_v1.2_unsigned.ipa; do
  P=$BASE/download/$f
  [ -s "$P" ] || { echo "MISSING: $P"; exit 1; }
  echo "== upload $f ($(du -h $P | cut -f1)) =="
  curl -sf -X POST "https://uploads.github.com/repos/$OWNER/$REPO/releases/$RID/assets?name=$f" \
    -H "$AUTH" -H "Content-Type: application/octet-stream" --data-binary @$P \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('  ok:', d['name'], d['size'], 'bytes,', d['state'])"
done
echo "=== RELEASE v1.2 UPLOADED ==="
