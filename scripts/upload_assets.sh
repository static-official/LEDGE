#!/bin/bash
# Upload built binaries to the existing v1.1 release. Skips already-uploaded.
set -e
OWNER=static-official
REPO=LEDGE
BASE=/home/z/my-project
API=https://api.github.com
AUTH="Authorization: token $GH_TOKEN"

RID=$(curl -s $API/repos/$OWNER/$REPO/releases/tags/v1.1 -H "$AUTH" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
echo "release id: $RID"

EXISTING=$(curl -s $API/repos/$OWNER/$REPO/releases/$RID/assets -H "$AUTH" \
  | python3 -c "import sys,json; print(' '.join(a['name'] for a in json.load(sys.stdin)))")
echo "already uploaded: ${EXISTING:-none}"

for f in LEDGEVR_v1.1_arm64.apk LEDGEVR_v1.1_mobile_arm64.apk LEDGE_unsigned.ipa; do
  P=$BASE/download/$f
  [ -s "$P" ] || { echo "MISSING: $P"; exit 1; }
  if echo " $EXISTING " | grep -q " $f "; then echo "have: $f"; continue; fi
  echo "== upload $f ($(du -h $P | cut -f1)) =="
  curl -sf -X POST "https://uploads.github.com/repos/$OWNER/$REPO/releases/$RID/assets?name=$f" \
    -H "$AUTH" -H "Content-Type: application/octet-stream" --data-binary @$P \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('  ok:', d['name'], d['size'], 'bytes,', d['state'])"
done
echo "=== ASSETS UPLOADED ==="
