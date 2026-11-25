#!/bin/bash
# Create release v1.6 (if missing) and upload the three v1.6 binaries.
set -e
OWNER=static-official
REPO=LEDGE
BASE=/home/z/my-project
API=https://api.github.com
AUTH="Authorization: token $GH_TOKEN"
TAG=v1.6
PAYLOAD=$BASE/scripts/release_v16.json

python3 - "$PAYLOAD" <<'PYEOF'
import json, sys
body = """Fix: VR build never submitted a frame (stuck loading in immersive).

- **Quest/VR: the OpenXR session is now actually created** - `XrBackend::createSession()`
  existed but was never called, so the app went immersive, never called
  `xrWaitFrame`/`xrEndFrame`, and Horizon OS kept its loading placeholder up forever.
  It is now called once at startup and retried per frame until the session exists.
- **Quest/VR: the in-VR loading screen now clears** - `G.loading` was never set to
  false in the VR shell (the mobile shell does this in its first-frame init block),
  so even with a live session the pastel "loading..." screen would never go away.
- Version bumps to 1.5 across all three builds.

- `LEDGEVR_v1.6_arm64.apk` - Quest: delete the old install first, then side-load
- `LEDGEVR_v1.6_mobile_arm64.apk` - touch Android (metadata bump only)
- `LEDGE_v1.6_unsigned.ipa` - iOS/iPad (metadata bump only)"""
open(sys.argv[1], 'w').write(json.dumps({"tag_name": "v1.6", "name": "LEDGE v1.6", "body": body}))
PYEOF

RID=$(curl -s "$API/repos/$OWNER/$REPO/releases/tags/$TAG" -H "$AUTH" \
  | python3 -c "import sys,json
raw = sys.stdin.read().strip()
print(json.loads(raw)['id'] if raw else '')")
if [ -z "$RID" ]; then
  RID=$(curl -sf -X POST "$API/repos/$OWNER/$REPO/releases" -H "$AUTH" \
    -H "Content-Type: application/json" -d @$PAYLOAD \
    | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
fi
echo "release id: $RID"

for f in LEDGEVR_v1.6_arm64.apk LEDGEVR_v1.6_mobile_arm64.apk LEDGE_v1.6_unsigned.ipa; do
  P=$BASE/download/$f
  [ -s "$P" ] || { echo "MISSING: $P"; exit 1; }
  echo "== upload $f ($(du -h $P | cut -f1)) =="
  curl -sf -X POST "https://uploads.github.com/repos/$OWNER/$REPO/releases/$RID/assets?name=$f" \
    -H "$AUTH" -H "Content-Type: application/octet-stream" --data-binary @$P \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('  ok:', d['name'], d['size'], 'bytes,', d['state'])"
done
echo "=== RELEASE v1.6 UPLOADED ==="
