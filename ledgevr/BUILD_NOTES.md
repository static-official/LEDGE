# BUILD STATUS — ALL COMPILE FIXES DONE, APK BUILDS

Toolchain (re-provisioned this session):
- toolchain/android-ndk-r26d (re-downloaded), toolchain/android-14 (aapt2/apksigner/zipalign),
  toolchain/android-34-ext12/android.jar (from platform.zip), toolchain/ecj.jar, toolchain/r8.jar,
  ledgevr/libs/arm64-v8a/libopenxr_loader.so (byte-identical to Maven loader.aar 1.0.34),
  ledgevr/native/include_openxr (1.0.34 headers), vendor/imgui 1.91.8.

## APK
`bash scripts/build_ledgevr.sh` → `download/LEDGEVR_v1.0_arm64.apk` (signed, zipalign -p 4,
apksigner v1+v2, keystore ledgevr/ledgevr.keystore pass ledgevr123 alias ledgevr).
Verified contents: AndroidManifest, resources.arsc, classes.dex,
libs/arm64-v8a/libv40shell.so + libopenxr_loader.so (both bundled).
Build-script fixes this session: C files get FLAGS_C (no -std=c++17), `-lopenxr_loader`,
stale obj/ cleaned each run, R8 `--pg-conf` + `--lib $AJ`, HttpBridge.java compiled,
libopenxr_loader.so copied into the packaged libs, proguard keeps HttpBridge.

## Fixed this session
1. Hamburger menu button: it is the dedicated LEFT-controller "menu" input
   (/user/hand/left/input/menu/click), NOT the Y button (Y is its own action now).
   Bindings are per-profile now: the touch-profile list no longer suggests
   /user/hand/right/input/menu/click (button does not exist on Quest Touch; one bad
   path makes xrSuggestInteractionProfileBindings reject the WHOLE set). Action-state
   readers implemented (XrActionStateGetInfo carries the action; xrGetActionStatePose
   for connected). assets.h action manifest updated the same way.
2. GLES swapchain: XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR + member `image` (was GL type
   + `texture`).
3. Display refresh rate: local typedef PFN_xrSetDisplayRefreshRateFB_ (not in 1.0.34
   header), both FB calls take XrSession (was instance/systemId).
4. renderer.cpp: duplicate rendererFlushLines tail removed, tint.w (was tint.a),
   const_cast for Mesh::draw, WorldTextRenderer rewritten to ImGui 1.91.8
   (FindGlyphNoFallback/AdvanceX, own program+camPos members, per-command height,
   color.w).
5. main.cpp: vec3→vec4 wraps for kBodyColors, hc multiply, gBodyYawOffset naming.
6. XrActionSuggestedBinding members are {action, binding}.

## PlayFab (NEW) — Title 1762FA
- native/playfab.h/.cpp: async client (one worker thread, copy-out/merge state model),
  endpoint https://1762fa.playfabapi.com, bracket-matched JSON extractor (unit-tested:
  scripts/pf_json_test.cpp — ALL PASS).
- APIs: LoginWithCustomID (+CreateAccount, GetPlayerProfile), UpdateUserTitleDisplayName,
  UpdateUserData/GetUserData (cloud save, key "ledgevr_save"), UpdatePlayerStatistics +
  GetLeaderboard (stat "Goals"), GetTitleNews, GetPhotonAuthenticationToken.
- Transport: Java HttpBridge.post(url, byte[] body, headers) → byte[] "code\nbody"
  (raw bytes both ways, no modified-UTF8 mangling), called from native worker threads
  via JniHost::httpPost (AttachCurrentThread).
- Menu: new "Account" tab (8 tabs): sign-in state, PlayFab ID, device ID, display-name
  set, cloud save upload/download/apply, Goals stat upload + Top-10 leaderboard,
  title news, re-fetch Photon token. Auto-login on app start.
- Photon custom auth (PlayFab Photon addon) — WIRE FORMAT VERIFIED LIVE 2026-11-23,
  authenticated end-to-end against the real master server with a real token:
  * key 217 = AuthenticationType, GpType Byte (0x03), value 0 = custom
    (official realtime-core enum: none=255, custom=0, steam=1, facebook=2,
    oculus=3, ps4=4, xbox=5, viveport=10, switch=11, ps5=12, epic=13).
  * key 216 = GET query string: username=<PlayFabId>&token=<PhotonToken>.
  * key 214 = AuthPostData → Photon sends POST → PlayFab's /photon/authenticate
    answers 404 (GET only). NEVER put the credentials there.
  * Success = op-230 response rc 0 with secret token (key 221) + userId.
  * main.cpp pfApplyAuth() builds "username=<pfid>&token=<tok>" into BOTH
    Photon clients (realtime + voice), userId = PlayFabId.
  * Fixture: scripts/photon_fixture_test.cpp "encode PlayFab addon auth" (161 B,
    exact bytes) — ALL PASS.

## LIVE server checks (from sandbox, 2026-11-23)
- PlayFab 1762FA: LoginWithCustomID CreateAccount:true → **200 OK** (player
  creation now ENABLED — user unchecked the API-Features box). New account
  DF852608E6CE593F created and named "Z.ai Test" (UpdateUserTitleDisplayName 200).
- GetPhotonAuthenticationToken → 200, field PhotonCustomAuthenticationToken
  (50-char token) for Realtime App ID 15f80a97-41e4-4c8b-a887-9b8de6d103ad.
- PlayFab addon auth endpoint: GET .../photon/authenticate?username=&token= →
  200 {"resultCode":1,"message":"Authenticated user ... title 1762fa"};
  without params → 200 resultCode 3 "Failed to parse token"; POST → 404.
- Official @photonengine/realtime-core client: connected + authenticated with
  the same token (auth option field is `auth`, type 'custom', parameters string).
- Our raw-protocol client (variant: 217 Byte(0) + 216 query string): response
  rc 0 with secret token — SAME result. Wire format proven.

## SERVER-SIDE STATE (PlayFab title 1762FA + Photon)
1. PlayFab API Features: player creation via LoginWithCustomId — DONE (unchecked).
2. PlayFab Add-ons → Photon: INSTALLED (see screenshot), Realtime App ID
   15f80a97-…103ad, Photon secret key generated (needed ONLY for webhooks,
   not for auth).
3. Photon dashboard → Authentication → Custom Server: CONFIGURED and verified
   working (404 seen earlier was caused by the client POSTing, not by the URL).
4. Voice chat audio I/O (AAudio mic/playback) remains unimplemented; voice Photon
   client + Java TLS WebSocket are ready.

## Test harness
- scripts/photon_fixture_test.cpp — host g++ build, ALL PASS.
- scripts/pf_json_test.cpp — host g++ build (needs /tmp/pfshim: jni.h + android/log.h
  stubs), ALL PASS.

## LEDGE Mobile (NEW) — touch (non-VR) port
Package `moonydev.ledgevrmobile`, label "LEDGE Mobile". `bash scripts/build_ledgemobile.sh`
→ `download/LEDGEVR_v1.0_mobile_arm64.apk` (same keystore, signed, verified).
Installs alongside the VR APK.

Native (shared stack, new touch shell):
- native/touch_backend.h/.cpp: EGL display/context created once and kept across
  surface loss (APP_CMD_TERM_WINDOW only detaches the surface — GL objects stay
  valid); GLES 3.2 shaders auto-shimmed to `#version 300 es` on phones without 3.2
  (all shaders are plain ES 3.0 GLSL otherwise). lvCompileShader/lvLinkProgram/
  lvMakeFbo live here for the mobile build (xr_backend.cpp still provides them for VR).
- Multi-touch routing: left-bottom zone = virtual joystick (spawn-at-touch),
  other touches = look drag; HUD buttons hit-tested first (menu/lobby/editor quick
  buttons top, JUMP + GRAB L/GRAB R bottom-right, PLACE/SELECT center when editor
  active). Each pointer id tracks its own button. Back key toggles the menu.
- Grab-pull (GrabVR movement on touch): hold GRAB while aiming at a block, drag
  = pull the world (drag down → body up, drag right → body left); release flings
  with grabStrength, clamped to 9 m/s — mirrors VR's hand-delta movement.
- native/main_mobile.cpp: 60 fps single-view loop (70° FOV), ImGui full-screen menu
  straight to the backbuffer (same 8 tabs as VR; base font 26 px for touch),
  PlayFab + Photon wiring identical; HUD drawn with unlit prog + wtext ortho.
- Editor: crosshair aim, PLACE (surface-snapped ghost), SELECT, Delete/Grow/Shrink/
  Rotate from the Editor tab; save/publish/cloud save all shared.

Identity: whatever name the player sets gets ` (MOBILE)` appended for the NETWORK
only — Photon nick and PlayFab UpdateUserTitleDisplayName (so PlayFab managers can
tell touch players in the dashboard). Local save keeps the bare name.

## Shared fixes this round (both builds)
1. world.cpp JParser: array elements now carry the key path (`blocks/0/` instead of
   `0/` — published levels failed to parse at all before), string leaf values are
   unquoted, fromJson reads color comps from path `.../c/` + key `[0..2]`.
2. Editor selection sync: editor helpers use GameWorld::editorSelected but both
   mains kept EditorState::selected — synced at every select/place/menu-ops site
   (Delete/Grow/Shrink/Rotate were silent no-ops before).
3. assets.h VS_WTEXT: uModel was uploaded but never declared/applied — all world
   text (name tags, mirror text) collapsed at the origin. Now applied (also powers
   the HUD text in ortho space).
4. world.h FrameInput.jumpPressed (default false) + world.cpp jump: 6.8 m/s when
   grounded (mobile JUMP button; VR unaffected).
5. Host test scripts/test_mobile_world_host.cpp: jump, pull sign math, editor
   place/delete, raycast, level JSON round trip — 14/14 PASS.

# v1.1 — Massive Content + Animation Update (2026-11-23)
- Deleted LEDGEVR_source.zip + all .idsig artifacts; apksigner now runs with --v4-signing-enabled false.
- Assets bundle (app/assets): music/*.ogg x7 (~31 MB, numpy-synthesized, q6 vorbis), sfx/*.ogg x13, tex/*.png x12 (2048px seamless, PIL).
- Native: anim.h (easings/particles/emotes), textures.h/.cpp (AAssetManager + minimal PNG decoder — paeth returns int, NOT bool; verified pixel-exact vs PIL on all 12).
- renderer: Vertex +uv, toon shader uUseTex/uUvTiling, drawMeshTex, buildRoundedBoxMesh, buildConeMesh.
- world: Block.birth (pop-in), DespawnFx queue, onFx(0 place/1 delete/2 land/3 goal/4 emote), onSound 5=place 6=delete.
- jnihost: AudioBridge JNI (music playlist, next, stop, sfx, volumes, getTrack). Java: AudioBridge.java (SoundPool + MediaPlayer), R8 keep rules added.
- main/main_mobile: emotes (event 61), squash-stretch, hat bob/crown spin, textured rounded blocks, pop/despawn anims, particles, menu slide tween (VR), music controls.
- Manifests + aapt2: versionCode 2, versionName 1.1. APKs: LEDGEVR_v1.1_arm64.apk (47.4 MB), LEDGEVR_v1.1_mobile_arm64.apk (47.0 MB).
- Watch-out: build script packs assets with `rm -rf $OUT/assets` first — plain `cp -r` into an existing dir nests assets/assets.
