#!/bin/bash
# Deploy LEDGE to GitHub: create repo, push main, create v1.1 release with
# the built APKs + IPA attached. Needs a GitHub Personal Access Token.
#
#   GH_TOKEN=ghp_xxx bash scripts/deploy_github.sh <owner> [repo] [private]
#
# Token needs: repo scope (classic) or Contents+Administration (fine-grained).
set -e
BASE=/home/z/my-project
OWNER=${1:?usage: deploy_github.sh <owner> [repo] [private]}
REPO=${2:-LEDGE}
VIS=${3:-public}
[ -n "$GH_TOKEN" ] || { echo "GH_TOKEN env var required"; exit 1; }
API=https://api.github.com
AUTH="Authorization: token $GH_TOKEN"

echo "== create repo $OWNER/$REPO ($VIS) =="
curl -sf -X POST $API/user/repos -H "$AUTH" -H "Accept: application/vnd.github+json" \
  -d "{\"name\":\"$REPO\",\"description\":\"LEDGE — pastel grab-and-fling parkour game (Android + iOS, custom ledui menu)\",\"private\":$( [ "$VIS" = private ] && echo true || echo false )}" \
  > /tmp/repo.json || echo "(repo may already exist)"
sleep 1

echo "== push main =="
cd $BASE
git remote add github https://x-access-token:$GH_TOKEN@github.com/$OWNER/$REPO.git 2>/dev/null || \
  git remote set-url github https://x-access-token:$GH_TOKEN@github.com/$OWNER/$REPO.git
git push github main 2>&1 | grep -vE "remote:|^Receiving|^Resolving|Compressing|^To " | head -5

echo "== release v1.1 =="
RID=$(curl -s $API/repos/$OWNER/$REPO/releases/tags/v1.1 -H "$AUTH" \
  | python3 -c "import sys,json
d=json.load(sys.stdin)
print(d['id'] if isinstance(d,dict) and d.get('id') else '')")
if [ -z "$RID" ]; then
  RID=$(curl -sf -X POST $API/repos/$OWNER/$REPO/releases -H "$AUTH" \
    -d '{"tag_name":"v1.1","name":"LEDGE v1.1","body":"First public build: pastel grab-and-fling parkour.\n\n- `LEDGEVR_v1.1_arm64.apk` — Quest/Android (OpenXR) side-load\n- `LEDGEVR_v1.1_mobile_arm64.apk` — touch Android side-load\n- `LEDGE_unsigned.ipa` — iOS, **unsigned**: sign with your cert (AltStore/Sideloadly/Xcode) before install\n\nMenu UI is ledui, a custom Dear-ImGui-styled immediate-mode library (no vendored ImGui)."}' \
    | python3 -c "import sys,json; print(json.load(sys.stdin)['id'])")
fi
echo "release id: $RID"
for f in LEDGEVR_v1.1_arm64.apk LEDGEVR_v1.1_mobile_arm64.apk LEDGE_unsigned.ipa; do
  echo "== upload $f =="
  curl -sf -X POST "https://uploads.github.com/repos/$OWNER/$REPO/releases/$RID/assets?name=$f" \
    -H "$AUTH" -H "Content-Type: application/octet-stream" --data-binary @$BASE/download/$f | head -c 120
  echo
done
echo "=== DONE: https://github.com/$OWNER/$REPO ==="
