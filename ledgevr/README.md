# LEDGE VR

A GrabVR-style fan game for **Meta Quest** — native C++ OpenXR (no game engine),
package `moonydev.ledgevr`, by MoonyDev.

- `libv40shell.so` — the whole game: OpenXR session, toon renderer, runtime-compiled
  GLSL ES 3.2 shaders, GrabVR movement, physics, hub + levels, sandbox editor,
  ImGui-style menu, Photon Realtime client (v18 over WebSocket), PlayFab client.
- `libopenxr_loader.so` — OpenXR 1.0.34 loader (Maven artifact).
- Java layer — `MainActivity` (NativeActivity), `WsBridge` (hand-rolled RFC6455
  TLS WebSocket for Photon), `HttpBridge` (HTTPS JSON for PlayFab). R8-minified.

## Build

```
bash scripts/build_ledgevr.sh        # → download/LEDGEVR_v1.0_arm64.apk
```

Requires the toolchain laid out in `BUILD_NOTES.md` (NDK r26d, build-tools r34,
android-34 jar, ecj, R8, OpenXR headers, ImGui 1.91.8, loader .so).

## Controls

| Input | Action |
| --- | --- |
| Left **hamburger** button (three lines, dedicated button — *not* Y) | open/close the menu |
| GRIP (hold) | grab the world / ledges; pull yourself; release to fling |
| Left stick | walk (+strafe setting), snap/smooth turn on right stick |
| Right trigger | point/place/select blocks in the editor, UI pointer |
| X / Y / A / B | free actions (cosmetics, quick emotes, etc.) |

## Menu tabs

Home · Lobbies · People · Levels · Editor · Settings · Cosmetics · **Account**

The **Account** tab is PlayFab (Title ID `1762FA`):
- device sign-in (auto on launch, CustomID persisted in internal storage)
- cloud save (upload / download / apply, PlayFab UserData key `ledgevr_save`)
- leaderboard: stat `Goals` — upload your goals-completed count, view Top 10
- title news, display-name change, re-fetch Photon token
- the Photon token is wired automatically into both Photon clients as
  custom auth (`AuthType=2`, JSON `{"token":...}`, userId = PlayFabId)

## IDs baked in

- Photon realtime AppId: `15f80a97-41e4-4c8b-a887-9b8de6d103ad`
- Photon voice AppId: `b56458c7-e48d-4353-9086-23e822c115cd`
- PlayFab Title ID: `1762FA` (endpoint `https://1762fa.playfabapi.com`)

## Server-side setup needed for online features

Status as of 2026-11-23 — all of these are DONE and verified live end-to-end:

1. **PlayFab API Features**: "Disable player creation using
   Client/LoginWithCustomId" is unchecked. Verified: `LoginWithCustomID
   CreateAccount:true` → 200 OK, new account `DF852608E6CE593F` created and
   named "Z.ai Test". The other three disable boxes stay checked (unused APIs).
2. **PlayFab Add-ons → Photon**: installed, Realtime App ID
   `15f80a97-41e4-4c8b-a887-9b8de6d103ad`. The addon generates a "Photon secret
   key" — that key is ONLY for webhooks, not for authentication.
3. **Photon dashboard → Authentication → Custom Server** with the URL
   `https://1762FA.playfabapi.com/photon/authenticate`: configured and verified.

### How the two systems talk (verified against the live servers)

1. App signs in: `LoginWithCustomID` → session ticket.
2. App calls `GetPhotonAuthenticationToken` with the Realtime App ID → a
   `PhotonCustomAuthenticationToken` (50-char token).
3. App connects to Photon with custom auth: key 217 = AuthenticationType value
   `0` (custom) + key 216 = GET query string
   `username=<PlayFabId>&token=<PhotonToken>`.
4. Photon calls the PlayFab URL above (GET) → `resultCode 1 "Authenticated
   user …"` → Photon returns a secret token → rooms work.
   (Key 214 is POST-data: PlayFab's endpoint 404s POST — never use it.)

Both flows were proven with the real servers: the official
`@photonengine/realtime-core` client AND LEDGE VR's own raw-protocol client
both authenticated with a real token from title 1762FA.

Local/offline play (movement, levels, editor, saves, cosmetics) needs none of this.

## Known gaps

- Voice chat: Photon voice client + transport are ready; the AAudio microphone
  capture / playback layer is not implemented yet.
- No true Quest native activity shading of the system "Oculus" button — the right
  controller has no menu button by design (system home), hence the menu is on the
  left hamburger only.

## Source layout

```
native/   C++ (OpenXR shell, renderer, world, photon, playfab, save, JNI host)
app/java/ Java (MainActivity, WsBridge, HttpBridge)
app/      manifest, res, proguard rules, assets config
scripts/  build_ledgevr.sh, photon_fixture_test.cpp, pf_json_test.cpp
BUILD_NOTES.md   exact build state + every fix, with live-server findings
```
