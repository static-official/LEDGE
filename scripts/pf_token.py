#!/usr/bin/env python3
"""Get a PlayFab session ticket + Photon custom-auth token (title 1762FA)."""
import json, sys, urllib.request

TITLE = "1762FA"
PHOTON_APP = sys.argv[1] if len(sys.argv) > 1 else "15f80a97-41e4-4c8b-a887-9b8de6d103ad"
CUSTOM_ID = sys.argv[2] if len(sys.argv) > 2 else "ledgevr-probe-1"

def post(path, body, ticket=None):
    req = urllib.request.Request(f"https://{TITLE}.playfabapi.com{path}",
                                 data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    if ticket:
        req.add_header("X-Authorization", ticket)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")

code, login = post("/Client/LoginWithCustomID",
                   {"TitleId": TITLE, "CustomId": CUSTOM_ID, "CreateAccount": True})
print("LoginWithCustomID:", code, json.dumps({k: login.get(k) for k in
      ("status", "errorCode", "errorMessage", "PlayFabId")}))
if code != 200:
    sys.exit(1)
login = login.get("data", login)

ticket = login["SessionTicket"]
pfid = login["PlayFabId"]
code, tok = post("/Client/GetPhotonAuthenticationToken",
                 {"PhotonApplicationId": PHOTON_APP}, ticket=ticket)
print("GetPhotonAuthenticationToken:", code, json.dumps(
      {k: tok.get(k) for k in ("errorCode", "errorMessage", "Token")}))
if code == 200:
    tok = tok.get("data", tok)
    print("token keys:", list(tok.keys()))
    tokval = tok.get("Token") or tok.get("PhotonCustomAuthenticationToken") or tok.get("PhotonApplicationId")
    open("/tmp/pf_creds.json", "w").write(json.dumps({"pfid": pfid, "token": tokval}))
    print("pfid:", pfid)
    print("token saved to /tmp/pf_creds.json (len", len(tokval), ")")

NAME = sys.argv[3] if len(sys.argv) > 3 else "Z.ai Test"
code3, dn = post("/Client/UpdateUserTitleDisplayName", {"DisplayName": NAME},
                 ticket=ticket)
print("UpdateUserTitleDisplayName ->", NAME, ":", code3,
      dn.get("data", {}).get("DisplayName") if code3 == 200 else dn)
