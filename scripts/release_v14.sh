#!/bin/bash
# Create release v1.4 (if missing) and upload the three v1.4 binaries.
set -e
OWNER=static-official
REPO=LEDGE
BASE=/home/z/my-project
API=https://api.github.com
AUTH="Authorization: token $GH_TOKEN"
TAG=v1.4
PAYLOAD=$BASE/scripts/release_v14.json

python3 - "$PAYLOAD" <<'PYEOF'
import json, sys
body = """Horizon OS immersive-launch fix.

- **Quest/VR: the app now launches immersive** - the manifest was missing
`com.oculus.intent.category.VR` (and used LAUNCHER instead of INFO), so Horizon OS opened it as a
flat 2D panel with a black screen. The activity now declares the VR category + focus-aware
meta-data; the OpenXR native shell (loader init, session) was already correct.
- Version bumps to 1.4 across all three builds.

- `LEDGEVR_v1.4_arm64.apk` - Quest: delete the old install first, then side-load this one
  (it shows under Unknown Sources and launches in VR)
- `LEDGEVR_v1.4_mobile_arm64.apk` - touch Android (metadata bump only)
- `LEDGE_v1.4_unsigned.ipa` - iOS/iPad landscape 16:9 (unchanged from v1.3 except version)"""
open(sys.argv[1], 'w').write(json.dumps({"tag_name": "v1.4", "name": "LEDGE v1.4", "body": body}))
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

for f in LEDGEVR_v1.4_arm64.apk LEDGEVR_v1.4_mobile_arm64.apk LEDGE_v1.4_unsigned.ipa; do
  P=$BASE/download/$f
  [ -s "$P" ] || { echo "MISSING: $P"; exit 1; }
  echo "== upload $f ($(du -h $P | cut -f1)) =="
  curl -sf -X POST "https://uploads.github.com/repos/$OWNER/$REPO/releases/$RID/assets?name=$f" \
    -H "$AUTH" -H "Content-Type: application/octet-stream" --data-binary @$P \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('  ok:', d['name'], d['size'], 'bytes,', d['state'])"
done
echo "=== RELEASE v1.4 UPLOADED ==="
