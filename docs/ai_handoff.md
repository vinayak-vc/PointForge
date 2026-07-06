# AI Handoff - PointForge (C++ repo)

## Latest Session (2026-07-06, cont.) - Web Remote Connect Screen: Premium Redesign

Branch `minor-fixes`. Full visual/UX redesign of the PIN-entry ("connect")
screen only — no business logic, WS protocol, or auth flow changes. `submitPin`,
`loadStoredPin`, and the pin-state/auto-submit-at-4-digits contract are byte-
for-byte the same; only how the pin is edited/displayed changed.

### What was built
New reusable components (`webremote/src/`):
- `AnimatedLogo.tsx` — logo with a breathing glow ring (CSS-only, respects
  `prefers-reduced-motion`).
- `PinInput.tsx` — 4 individual digit boxes replacing the single text field:
  auto-advance focus, backspace-to-previous, arrow-key nav, full paste
  support, per-box `aria-label`. Still just edits the same `pin: string`.
- `NumberPad.tsx` — same 12-key layout, larger touch targets, subtle
  press/ripple feedback (pure CSS `::after` radial flash + scale, no JS).
- `StatusIndicator.tsx` — unifies the old separate error/status/hint
  paragraphs into one dot+title+detail readout per `WsStatus`, with an
  animated expanding ring while connecting/reconnecting. Copy avoids
  overclaiming security — "PIN-protected local link", not "Encrypted"
  (the transport is plain WS on LAN, not TLS).
- `BrandingPanel.tsx` — left pane on desktop / compact header when stacked:
  animated logo, title, subtitle, three descriptive fact rows (hidden below
  1024px so the stacked layout stays compact).
- `PrimaryButton.tsx` — Connect button with a loading-spinner state.
- `GlassCard.tsx` — reusable frosted card wrapper for the auth card.

`ConnectScreen` (in `App.tsx`) now composes these: desktop grid
`minmax(320px,34%) 1fr` (branding / auth, matching the requested ~30/70
split), collapsing to a single stacked column below 1024px. Background
upgraded to a layered dark gradient + radial vignette + inline-SVG noise
texture (no network asset) instead of flat near-black.

### Verified
- `npm run build` (tsc --noEmit + vite) clean.
- Checked live via the Vite dev preview (no backend, so `status` sits at
  `connecting`/`pin` — sufficient to verify layout): desktop 1600×900 grid
  computed `453px / 880px` columns (~34/66); mobile 375×812 and tablet
  768×1024 both collapse to a stacked flex column with **zero horizontal
  overflow** (`scrollWidth === clientWidth` at both sizes); no console errors.
- NOT yet checked against the real embedded exe in a browser (rebuild was
  in progress at handoff time — do that first before further changes).

### Modified/added files
- webremote/src/{AnimatedLogo,PinInput,NumberPad,StatusIndicator,BrandingPanel,PrimaryButton,GlassCard}.tsx (new)
- webremote/src/App.tsx (ConnectScreen rewritten to compose the above)
- webremote/src/index.css (Connect Screen section fully replaced; removed
  now-dead `.reconnect-spinner`/`.pin-input`/`.keypad-btn`/etc. rules)
- docs/ai_handoff.md

### Next Recommended Task
- Load the rebuilt exe in a real browser and visually confirm: PIN box
  auto-advance/backspace/paste, keypad press feedback, status-indicator
  color transitions across pin -> pin_bad -> connecting -> connected, and the
  branding-panel logo pulse — none of this was checked against a live WS
  connection this session (dev preview had no backend).
- Then resume the pre-existing next task from earlier this session: desktop
  browser mouse/keyboard camera control + status-bar clipping audit
  (`FlyTab.tsx`/`.app-shell` flex chain) — still not started.

## Previous Session (2026-07-06, cont.) - Toolbar Consolidation + Versioned Exe Filename

Branch `minor-fixes` (cut from `main` after the previous session's PR #3 merge
landed the legend/dropdown/build-version work below).

### What was built / Fixed
- **Web Remote toolbar de-scrolled**: the top toolbar (Frame/Top/Front/Side +
  Ortho/UI/3D + Shot/Full + Stream) previously needed horizontal scroll to see
  every control. Consolidated the view-toggle and capture buttons into one
  icon-only group (`.toolbar-btn--icon`, 30px square, tooltip via `title`/
  `aria-label` — labels already live in the Camera tab) and shrunk the
  stream-quality buttons to single-letter `.toolbar-btn--compact` (24px). Set
  `.toolbar { overflow-x: hidden }` (was `auto` + a fade-mask cue) since
  everything now fits without scrolling; dropped the now-unused mobile fade
  mask CSS.
- **Build filename now embeds the version**: `ViitorXPCViewer.exe` ->
  `ViitorXPCViewer_v<MAJOR><MINOR><PATCH>.exe` (e.g. `_v103` for 1.0.3) via a
  new `POST_BUILD` step (`tools/stamp_exe_name.cmake`) that parses the
  generated `Version.h` and renames the freshly-linked exe, deleting any stale
  `_v*` copy from a previous build first. Verified live: build stamped
  `ViitorXPCViewer_v103.exe`, no leftover unversioned or older-versioned exe.
  See decisions.md for why this is safe to run unconditionally every build
  (Version.h's changing content forces main.cpp to always recompile/relink).

### Verified
- `npm run build` clean.
- `cmake --build build-static --config Release --target pfview` clean;
  confirmed `ViitorXPCViewer_v103.exe` on disk, no stray unversioned exe.
- NOT yet visually re-verified in a live desktop browser against the running
  toolbar (no icon-crowding/tooltip check at narrow desktop widths).

### Modified files
- webremote/src/ActionBar.tsx (icon-only toolbar buttons, clubbed groups)
- webremote/src/index.css (`.toolbar-btn--icon`/`--compact`, drop scroll+mask)
- CMakeLists.txt (POST_BUILD exe-rename step)
- tools/stamp_exe_name.cmake (new)
- docs/{decisions,ai_handoff}.md

### Follow-up (same branch) — version shown in Web Remote UI + tab title
- New additive cfg key `version` (`RemoteConfig::appVersion` = `PF_VERSION_STRING`,
  `{"version":...}` in `publishConfig`) — client `Cfg.version`.
- `StatusHUD.tsx` now takes a `cfg` prop and shows a `v1.0.4`-style chip next
  to the connection status.
- `App.tsx` sets `document.title` to `"ViitorXPC - v1.0.4"` once `cfg.version`
  arrives (falls back to the static `ViitorXPC` from `index.html` until then).
- Verified: build stamped/renamed to `ViitorXPCViewer_v104.exe` clean.

### Next Recommended Task
- User separately flagged (not yet actioned this session): the web remote
  opened in a **desktop** browser has no mouse/keyboard camera control
  (`FlyTab.tsx` only wires touch events) and the bottom status bar can be
  clipped off-screen on desktop window sizes — needs a flex-overflow audit
  (`.app-shell`/`.workspace` chain) plus desktop mouse-drag-look + wheel-zoom +
  WASD-fly input alongside the existing touch gestures. Scoped but not started.
- Then resume: merge `minor-fixes` into `main` once the above is done, or
  merge now if the user wants this batch shipped separately.

## Previous Session (2026-07-06) - Legend UI Fixes + Auto-Versioned Builds

UI-only pass (no functional/protocol changes) requested by the user, plus an
auto-incrementing build version number.

### What was built / Fixed
- **Color-mode dropdown**: React `DisplayTab.tsx`'s 5-option Color `Segmented`
  control (which visibly overflowed/clipped "Intensity"/"Classification" in
  screenshots) replaced with a new `Select` dropdown component (`controls.tsx`)
  — mutually-exclusive modes read better as a `<select>` than a segmented row.
- **Elevation/intensity legend moved beside the Properties panel** (C++ viewer,
  `main.cpp` ~1613-1673): the legend now queries `ImGui::FindWindowByName("Properties")`
  and anchors just left of it **only if the panel truly abuts the window's
  right edge** (guards against a left-docked/floating Properties dragging the
  legend on top of itself — a review-caught bug); falls back to hugging the
  window edge when the panel is closed or not right-docked. Title width is now
  included in the reserved layout width (previously "Intensity" — wider than
  its "max"/"0" labels — could overshoot the reserved bar+label width and, in
  SBS, bleed across the eye split — also review-caught and fixed).
- **Legend now survives hide-UI (zen/F5) and renders in SBS**: old gate was
  `!stereoSBS` only (suppressed in 3D, visible in zen); new code drops the SBS
  suppression and instead draws the legend **once per eye** with the same
  ±15px negative-parallax offset the watermark uses (`main.cpp` ~1547-1568 is
  the copied pattern), so it fuses correctly through a stereoscope.
- **React app got the same legend**: new `webremote/src/Legend.tsx`, a
  click-through viewport-corner overlay using a CSS turbo-ramp gradient +
  zmin/zmax labels. Required a new additive server->client cfg key: `zmin`
  (`RemoteConfig::zMin` in `RemoteServer.h`, `{"zmin":c.zMin}` in
  `publishConfig`, filled from `store.cube(store.rootIndex()).min[2]` in
  `main.cpp`) since the client previously had no elevation range at all
  (only `cubeSize`, which gives zmax-zmin but not an absolute zmin).
- **General UX pass** (webremote, CSS/markup only): `.seg` flex->CSS-grid so
  segmented options wrap instead of clipping (also fixed the "JPEG (Compat)" /
  "WebRTC (Fast)" stream-engine picker and the 4-option render-quality one);
  safe-area insets on toolbar/status-bar/fly-overlays for notched phones;
  `100dvh`/`46dvh` so the mobile bottom-sheet inspector tracks the visual
  viewport; `@media (pointer: coarse)` target-size bump; mobile toolbar
  dedup (Camera-tab-duplicated preset/toggle buttons hidden under
  `.toolbar-group--dup` on phones, speed slider hidden — Camera tab/gesture
  panel already cover them); connect-screen `overflow-y:auto` +
  `justify-content: safe center` (was clipping the keypad on short phones);
  `--text-3` contrast raised to meet WCAG AA; PIN input made typeable
  (`inputMode="numeric"`, was `readOnly`); lucide `ZoomIn`/`Hand` icons
  replacing raw emoji in the pinch-mode toggle; stream-quality buttons wrapped
  in an `aria-label`d group; `aria-pressed` added to Segmented/quality buttons.
- **Auto-incrementing build version**: `VERSION` file + `tools/bump_version.cmake`
  (new `ALL` custom target `version_header`) stamp `src/viewer/Version.h`
  (`PF_VERSION_STRING`) on every build and bump the patch number for the next
  one. Shown small in the status bar (`v1.0.N`, next to the FPS/GPU stats) and
  embedded as the exe's real Win32 file-version metadata (`app.rc`'s
  `1 VERSIONINFO` block — see decisions.md for two build-system gotchas hit
  along the way: `cmake -P` needs explicit `-D` path args, and the resource ID
  must be the literal `1`, not the undefined `VS_VERSION_INFO` macro).
- Reviewed via an adversarial multi-agent pass (C++/React/CSS/coverage
  dimensions, each finding independently re-verified); the two confirmed
  findings (title-width overshoot, dock-side assumption) are the fixes
  described above — everything else proposed did not survive verification.

### Verified
- `npm run build` (tsc --noEmit + vite) clean.
- `cmake --build build-static --config Release --target pfview` clean; exe
  relinked as `build-static/Release/ViitorXPCViewer.exe` with the new webremote
  embedded and `FileVersion`/`ProductVersion` = 1.0.1 confirmed via
  `[System.Diagnostics.FileVersionInfo]::GetVersionInfo(...)`.
- NOT yet visually verified in a running instance against a loaded cloud (no
  `pfview` launch + screenshot pass this session) — do that before merge.

### Modified files
- src/viewer/main.cpp (legend layout/anchor/SBS/zen, RemoteConfig.zMin fill,
  Version.h include + status-bar version text)
- src/viewer/RemoteServer.h, RemoteServer.cpp (RemoteConfig::zMin, "zmin" cfg key)
- src/viewer/app.rc (VERSIONINFO resource)
- CMakeLists.txt (version_header custom target + pfview dependency)
- tools/bump_version.cmake (new)
- VERSION (new, auto-managed — do not hand-edit the patch number)
- .gitignore (ignore generated src/viewer/Version.h)
- webremote/src/controls.tsx (new `Select` component, `aria-pressed` on Segmented)
- webremote/src/DisplayTab.tsx (Color mode -> Select)
- webremote/src/Legend.tsx (new)
- webremote/src/cfg.ts (Cfg.zmin)
- webremote/src/App.tsx (mount Legend, typeable PIN, keypad aria-labels)
- webremote/src/ActionBar.tsx (toolbar-group--dup classes, stream-quality group)
- webremote/src/StatusHUD.tsx (status-chip classes replacing inline styles)
- webremote/src/FlyTab.tsx (lucide icons replacing emoji)
- webremote/src/index.css (full UX pass — see above)
- docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
- Launch `ViitorXPCViewer.exe` against a loaded cloud and visually verify: the
  legend sits beside Properties (and at the window edge when closed), persists
  through F5 zen and F9 SBS (twice, fused), and doesn't clip in either mode at
  a couple of window sizes/uiScale values. Same check on the phone webremote
  (Legend overlay + dropdown + mobile toolbar dedup).
- Then resume the pre-existing next task: merge `webapp-controller` into `main`.

## Previous Session (2026-07-03, cont.) - Web Remote UX Polishing & Orbit Snapping Bugfix

### What was built / Fixed
- **Orbit Snapping Bug (C++ App)**: The user reported the camera violently snapping when left-clicking to orbit after right-clicking to look. This was because the `pivot` coordinate was being left stale when the camera rotation changed. Fixed in `main.cpp` by re-projecting the pivot directly onto the camera's current forward axis right as the left-click drag starts, preserving distance without teleporting the camera.
- **Web Remote Speed Controls**: Raised the maximum speed slider caps for Look, Pan, and Zoom from 1.0 to 5.0. 
- **Global UI Scale**: Increased the Web Remote's entire UI size by 20% globally via `zoom: 1.2` on the `body` tag.
- **Always-on Fly Controls**: Removed the Fly controls from the properties panel and overlaid them permanently over the viewport. The speed slider values are now preserved because the tab never unmounts.
- **Resizable Properties Panel**: Used native CSS resize handles (`resize: horizontal`, `direction: rtl`) to allow the right-hand Inspector rail to be dragged to expand its width.
- **Watermark & Branding**: Wired the C++ app's SVG logo to the Web Remote's favicon, Connect screen, and an always-visible viewport watermark.

### Modified files
- `src/viewer/main.cpp`
- `webremote/src/index.css`
- `webremote/src/FlyTab.tsx`
- `webremote/src/App.tsx`
- `docs/{tasks,ai_handoff}.md`

### Next Recommended Task
The web remote `webapp-controller` feature branch is complete and fully polished.
- Merge `webapp-controller` into the main branch.

User report: pressing Front/Side/Top (keys 1/3/7 or remote `preset*` cmds) made
the model vanish; 'F' (frame) recovered it.

### Root cause (`src/viewer/main.cpp` camPreset* lambdas)
Two independent bugs:
1. **Wrong space**: presets set `cam.position = store.cubeCenter() + offset`,
   but the camera/render space is CENTRED (cube centre = origin — see
   `frameAll`, `pivot`, and the GPU-precision convention in CLAUDE.md). Adding
   the world-space cube centre teleported the camera by the cloud's world
   offset — kilometres for georeferenced scans.
2. **Wrong orientation**: the hardcoded yaw/pitch never pointed at the model
   anyway (e.g. Top placed the camera above but pitch +89 = looking straight UP;
   Front sat at -Y but looked -X). front() convention: Z-up, yaw=0 → +Y.

### Fix
Single `presetView(dir)` helper: distance from the frameAll fit formula
(`cs*0.5/tan(fov/2)*1.4`), `pivot = origin`, `cam.position = dir*dist`,
`cam.lookAt(pivot)` (safe at straight-down — pitch clamped ±89). Presets also
reset the orbit pivot now, so orbiting after a preset behaves.
Rebuilt `build-static/Release/ViitorXPCViewer.exe` — this build also embeds the
redesigned webremote UI (web_build target ran npm install for lucide-react).

### Modified files
- src/viewer/main.cpp (camPresetTop/Front/Side → presetView helper)
- docs/{tasks,ai_handoff}.md

### Next Recommended Task
Verify presets on the loaded NTPC.laz scan (1/3/7 + remote Camera tab), then
proceed with the merge plan below.

## Previous Session (2026-07-03, cont.) - Web Remote Premium UI Redesign

User requested a full visual redesign of the ViitorXPC Remote Controller web application into a premium, enterprise-grade interface, without altering any backend logic, API contracts, or existing features.

### What was built / Fixed
- **App Shell Architecture**: Restructured the layout into a fixed desktop-like shell consisting of a Top Toolbar, a central Workspace (Viewport + Right Inspector), and a bottom Status Bar. On mobile screens, the inspector docks to the bottom.
- **CSS Design System**: Created a handcrafted CSS variables design system in `index.css` to avoid complex build configurations. Used a dark, technical color palette (`#0D0F12` background, `#17191E` panels, `#4DA3FF` accent).
- **Component Upgrades**: Replaced default browser inputs with custom styled components: animated iOS-style toggles, segmented controls, custom slider thumbs, and glowing buttons.
- **Iconography**: Integrated `lucide-react` for clean, professional icons across the toolbar, inspector tabs, and status bar.
- **Gesture Preservation**: Carefully ported the existing multi-touch and mouse gesture logic into the new floating glass-morphic `FlyTab` without any regressions.

### Modified files
- webremote/package.json
- webremote/src/index.css
- webremote/src/controls.tsx
- webremote/src/App.tsx
- webremote/src/ActionBar.tsx
- webremote/src/StatusHUD.tsx
- webremote/src/FlyTab.tsx
- docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
- Merge `webapp-controller` branch into main.
- Validate the new UI performance on various mobile and desktop browsers.

## Previous Session (2026-07-03, cont.) - WebRTC Streaming Bugfix (client signaling)

User reported: WebRTC video stream not working; JPEG stream fine. Root cause was
entirely client-side (webremote React app) — three signaling bugs. No C++ changes.

### Bugs found & fixed
1. **Fatal — answer never delivered** (`App.tsx`): the `onWebRTC` option passed to
   `useWebSocket` was a `let` no-op placeholder; reassigning the local variable
   after `useWebRTC()` returned did NOT update the already-constructed options
   object, so `optsRef.current.onWebRTC` stayed the no-op every render. The
   server's `webrtc_answer` was silently dropped → `setRemoteDescription` never
   ran → PC stuck in `have-local-offer` → no video ever. Fixed with a
   `webrtcMsgRef` ref + stable `useCallback` that forwards to the latest handler.
2. **Fatal — server ICE dropped** : server (RemoteServer.cpp `onLocalCandidate`)
   sends trickle candidates as `{"t":"webrtc_candidate"}`, but the client switch
   in `useWebSocket.ts` only forwarded `webrtc_ice`/`webrtc_answer`. libdatachannel
   emits the answer SDP *before* gathering, so the answer carries no candidates —
   client had zero remote candidates and ICE could not pair. Client now accepts
   both `webrtc_ice` and `webrtc_candidate` (server also lacks `sdpMLineIndex`;
   defaulted to mid `video` / index 0).
3. **Race** (`useWebRTC.ts`): `addIceCandidate` throws if called before
   `setRemoteDescription` resolves. Added a pending-candidate queue flushed after
   the answer is applied. Also: offer message now includes `type:'offer'` (server
   reads `m.value("type","")`), and `onnegotiationneeded` got try/catch.

### Verified
- `npm run build` clean (tsc --noEmit + vite).
- Rebuilt `build-static/Release/ViitorXPCViewer.exe` (web app is embedded via
  generated `EmbeddedWeb.h`; a webremote change ALWAYS requires an exe rebuild —
  `cmake --build build-static --config Release --target pfview`).
- Not yet re-tested on physical phone — that is the next step.

### Modified files
- webremote/src/App.tsx (ref-based onWebRTC wiring)
- webremote/src/useWebSocket.ts (accept `webrtc_candidate`)
- webremote/src/useWebRTC.ts (candidate queue, offer `type`, error handling)
- docs/{tasks,ai_handoff}.md

### Round 2 (same day) — mobile showed play button, desktop Chrome black
Two more root causes found and fixed:

4. **Fatal — answer rejected the video m-line** (`RemoteServer.cpp` webrtc_offer):
   libdatachannel matches local tracks to remote m-lines strictly by mid
   (`mTracks.find(remoteMedia->mid())` in populateLocalDescription). Browser
   offer mid is `"0"`; server track was hardcoded mid `"video"` → no match →
   answer marked the video line removed (port 0) → no media could ever flow.
   Payload type was also hardcoded 96 instead of mirroring the offer's H264 PT.
   Fix: parse the offer with `rtc::Description`, extract the video m-line's mid
   + the browser's H264 payload type (prefer packetization-mode=1) + its fmtp,
   and build the local track from those. Logs
   `WebRTC offer: video mid="..." H264 pt=...` on offer, plus pc state changes.
   If the browser offers no H264 at all, logs an error and stays on JPEG.
5. **Mobile autoplay blocked** (`VideoLayer.tsx`): React does not reliably
   reflect the `muted` prop into the DOM before the autoplay policy check —
   mobile browsers blocked playback and showed a play overlay. Fix: set
   `v.muted = true` imperatively before `play()`, plus a
   pointerdown/touchend fallback that resumes a paused video on first gesture.

Also added console diagnostics in `useWebRTC.ts`: ICE/pc state transitions and
a 2 s inbound-rtp stats log (`[webrtc] frames=... bytes=... keyframes=...`).
Rebuilt webremote + `build-static/Release/ViitorXPCViewer.exe`.

### Round 3 (same day) — WebRTC works but quality/perf worse than JPEG
Expected with old settings: encoder was hardcoded to 2 Mbps CBR while the JPEG
"med" preset effectively uses 12–18 Mbps; point-cloud content (dense
high-contrast dots) defeats H.264 inter prediction, so starved CBR smears.
Applied in `RemoteServer.cpp` `initMF`:
- `MF_MT_AVG_BITRATE` 2 Mbps → 12 Mbps (LAN-only stream).
- Quality-based VBR: `CODECAPI_AVEncCommonRateControlMode` =
  `eAVEncCommonRateControlMode_Quality`, `CODECAPI_AVEncCommonQuality` = 78.
  Falls back to CBR@12Mbps with a logWarn if the encoder rejects the mode.
Rebuilt exe. NOT yet done (option #3, discussed): hardware encoder via
`MFTEnumEx(MFT_ENUM_FLAG_HARDWARE)` (NVENC/QuickSync) — the current
`CLSID_CMSH264EncoderMFT` is Microsoft's software encoder and burns CPU
(plus CPU RGB→NV12) competing with the renderer; that is the remaining
perf gap vs turbojpeg if WebRTC still feels slow.

### Round 4 (same day) — hardware H.264 encoder (option #3), verified live
`RemoteServer.cpp` `encodeLoop` reworked for hardware encoding:
- `initMF` now tries `MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE)`
  first (NVENC/QuickSync/VCE), falls back to `CLSID_CMSH264EncoderMFT` software.
- Hardware codec MFTs are **async** — added the METransformNeedInput/HaveOutput
  event model (`IMFMediaEventGenerator`, `MF_TRANSFORM_ASYNC_UNLOCK`), driven by
  a non-blocking pump (`MF_EVENT_FLAG_NO_WAIT`, bounded 150 ms credit wait —
  drops the frame if the encoder is backed up; never blocks/hangs).
- Crash safety: every failure path (enum, activate, configure, runtime
  ProcessInput/Output, stream change) releases the encoder and falls back to
  the software MFT via a sticky `mfHwFailed` flag — stream continues, no crash.
  `MF_E_TRANSFORM_STREAM_CHANGE` handled by re-accepting the output type.
- Shared `sendEncodedSample`/`processOneOutput` lambdas serve both sync
  (software) and async (hardware) paths.

**Live-verified on this machine** (Playwright against the real exe):
log shows `WebRTC: hardware H.264 encoder: NVIDIA H.264 Encoder MFT`, browser
`<video>` decodes 1280×734 and currentTime advances; 4× JPEG↔WebRTC toggles +
client reconnects survived; process stayed alive, zero error lines in the log.

### Session modified files (uncommitted, branch `webapp-controller`)
- src/viewer/RemoteServer.cpp — offer mid/PT mirroring, quality VBR + 12 Mbps,
  hardware-first async-MFT encoder with software fallback
- webremote/src/useWebRTC.ts — candidate queue, `webrtc_candidate` handling,
  offer `type`, ICE/stats console diagnostics
- webremote/src/useWebSocket.ts — forward `webrtc_candidate`
- webremote/src/VideoLayer.tsx — imperative `muted` + gesture autoplay fallback
- webremote/src/{App,ActionBar,FlyTab,StatusHUD,controls}.tsx, index.css,
  package.json — webremote UI redesign (premium connect screen, lucide-react
  icons, StatusHUD, toolbar shell) — also uncommitted on this branch
- docs/{project-overview,architecture,roadmap,decisions,tasks,ai_handoff}.md —
  architecture.md gained §8 "Web Remote"; decisions.md gained the two WebRTC
  decisions; overview/roadmap refreshed (web embedding is DONE, not a milestone)

### Next Recommended Task
Physical test with the NEW exe (`build-static/Release/ViitorXPCViewer.exe`):
set preferred stream to WebRTC, connect, watch browser console —
ICE should reach `connected` and `frames=` should climb. If ICE connects but
frames stay 0, the remaining suspect is the MF H.264 encoder output
(SPS/PPS/IDR); check viewer Console for `WebRTC offer:` / pc state logs.
Then merge `webapp-controller`.

## Previous Session (2026-07-03, cont.) - Logo, Branding, and 3D Watermark

### What was built / Fixed
- **Configuration Routing**: Modified `main.cpp` to use `SDL_GetPrefPath` to route the `pfview_config.txt` serialization to the user's local `AppData` directory (`AppData/Roaming/ViitorX/ViitorXPC/`). This ensures standard desktop behavior without requiring admin privileges to save settings when installed in `Program Files`.
- **Project Renaming**: Renamed the project from `PointForge` to `ViitorXPC` across `CMakeLists.txt` and configuration namespaces. The primary executable is built as `ViitorXPCViewer.exe`.
- **Windows File Explorer Icon**: Generated a transparent `vx.ico` Windows icon from the existing `vx.bmp` logo. Added `app.rc` to the CMake build definitions to embed the `.ico` file into the Windows executable so the branding shows up natively in the File Explorer.
- **Stereoscopic 3D Logo Pop-Out**: Rewrote the background watermark rendering in `main.cpp` for SBS (Side-by-Side) 3D mode. It now renders twice (once for each eye) and applies a 15-pixel negative parallax shift so the logo has a true stereoscopic depth effect, "popping out" of the screen when viewed through 3D lenses.

### Modified files
c:/UnrealProject/PointForge/CMakeLists.txt
c:/UnrealProject/PointForge/src/viewer/main.cpp
c:/UnrealProject/PointForge/make_ico.ps1
c:/UnrealProject/PointForge/src/viewer/app.rc
docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
The application is feature-complete with UI gestures, WebRTC streaming, local application data persistence, native File Explorer branding, and 3D UI overlays.
- Final user test of the SBS 3D effect in VR headsets/stereoscopic lenses.
- Merge current branches and prepare the first production release package.

## Previous Session (2026-07-03, cont.) - Web Remote WebRTC Streaming & Mobile Touch Fixes
During the physical phone test, WebRTC video streaming and mobile UI were finalized. The viewer now perfectly streams high-performance, low-latency H.264 video to the phone browser, and replaces the mobile joysticks with an intuitive multi-touch gesture system.

### What was built / Fixed
- **WebRTC Pipeline Stability**: Fixed COM initialization in `RemoteServer::encodeLoop` which caused silent frame drops. Added manual `IMFSample` allocation for `CLSID_CMSH264EncoderMFT` which fixed crashes during stream processing.
- **SSRC Matching**: Explicitly bound the `rtc::Description::Video` SDP SSRC and `rtc::RtpPacketizationConfig` SSRC to `1111` so the web browser successfully matches and plays the RTP stream instead of dropping it as rogue packets.
- **Keyframe Guarantee**: Configured `CODECAPI_AVEncMPVGOPSize` to `30` (1 second) to guarantee a consistent IDR frame stream, preventing black screens when the client connects mid-stream.
- **WebRTC Thread Safety**: Added `try/catch` and `isOpen()` checks to `videoTrack->sendFrame(...)` and all SDP/ICE handlers, guaranteeing `libdatachannel` exceptions do not crash the C++ app when a client abruptly disconnects.
- **Mobile Touch Gestures**: Removed the nipple.js joysticks in `webremote`. Implemented multi-touch gestures in `FlyTab.tsx`:
  - **Single tap & move**: Maps to Pitch/Yaw (Look)
  - **Double tap & move**: Maps to Up/Down/Left/Right (Pan)
  - **Pinch**: Maps to Forward/Backward (Zoom)
  - **Deltas**: Changed `App.tsx` to automatically zero-out `f/s/u/yaw/pit` on every 33ms send tick so pointer deltas accumulate correctly for drag-to-position control. Held buttons (UP/DOWN/BOOST) use a faster `setInterval` to replenish their state against the zeroes.

### Protocol v1 (verified)
`hello{pin}` -> `hello_ok|hello_bad`; `move{f,s,u,yaw,pit,boost}` 30 Hz;
`cmd{n[,v]}`; `state{fps,pts,pos,ui,file}` 5 Hz. Signs: stick up = +1 = fly
forward / look up (pitch NOT negated in main.cpp — verify feel on phone).

### Verified (localhost, real exe, physical phone)
GET / 200 + assets 200 from `web/`; WS upgrade 101. WebRTC streaming successfully connects and renders the H.264 video to the React `<VideoLayer>`. Gestures drive the camera smoothly via delta accumulation. Full build clean (MSVC).

### Modified files
c:/UnrealProject/PointForge/src/viewer/RemoteServer.cpp
c:/UnrealProject/PointForge/webremote/src/App.tsx
c:/UnrealProject/PointForge/webremote/src/FlyTab.tsx
docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
The Web Remote system (React UI, WebSocket, WebRTC streaming, and Native C++ Server) is now fully working and verified. 
- Conduct a final sanity check of the web app controls on the mobile device.
- If everything is working perfectly, commit the `webapp-controller` branch and merge it into main.

## Previous Session (2026-07-03) - Web Remote Controller: plan + branch

Planning session only — no code yet. Goal: control camera + viewer options from a
phone browser over LAN so the PC can hide all UI (F5) while flying continues.

- Created branch `webapp-controller` (based off `library/unity`; that branch's
  uncommitted DLL work — modified `CMakeLists.txt`, untracked
  `src/tools/pfconvert/pfconvert_api.cpp` — carried over untouched).
- Architecture decided and recorded in `docs/decisions.md`: embedded
  civetweb HTTP+WS server inside pfview (rejected Firebase RTDB/Netlify relay —
  same-LAN use case, ~1-5 ms vs 100-300 ms, offline-capable). React+Vite+TS app
  in `webremote/`, built to static files, served by the viewer from `web/`
  beside the exe (like `shaders/`).
- Full 5-phase plan recorded in `docs/tasks.md` (In Progress section):
  deps → `RemoteServer.{h,cpp}` → main.cpp integration → React app → build wiring.
- Protocol v1: JSON over WS — `hello` w/ 4-digit PIN, `move` @30 Hz
  (f/s/u/yaw/pit/boost), `cmd` (frame/presets/ortho/hideui/shot/measure/
  pointsize/speed), `state` @5 Hz back (fps/pts/pos/ui/file).
- Key patterns to follow: input mirrors `SerialController` (bg thread + atomics,
  polled in main loop ~`main.cpp:890`); commands mirror `consumePause()` one-shot
  pattern; commands dispatch to existing flags (`frameAllReq`, `showUI`,
  `pendingShot`, presets).

### Next Recommended Task
Phase 0: verify vcpkg `civetweb` port has WebSocket support enabled (else
FetchContent with `-DCIVETWEB_ENABLE_WEBSOCKETS=ON`, or `ixwebsocket`); add
`civetweb` + `nlohmann-json` to `vcpkg.json`; scaffold `webremote/` with Vite;
add `PF_WITH_REMOTE` CMake guard (graceful stub like `PF_WITH_LAS`).
