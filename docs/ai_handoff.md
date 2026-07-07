# AI Handoff - PointForge (C++ repo)

## Latest Session (2026-07-07, cont.) - Cross-section / slice export DONE (newdev.md #4, branch `cross-section-slice-export`)

Task #4 is **DONE**. Implementation landed, tests pass, and real-model
DXF/CSV/PNG export was smoke-verified against
`C:\UnrealProject\model\PointForgeCache_direct`.

### What was built
- **CPU boxed point traversal** (`src/viewer/OctreeStore.{h,cpp}`):
  `forEachPointInBox(AABB, maxDepth, cancel, visitor)` reuses the existing
  `readNodeInto` decode path (zstd/uncompressed), unpacks points into WORLD
  coordinates, exact-tests each point against the requested clip box, and
  streams matches to a callback. `estimatePointsInBox` provides a cheap
  intersecting-node payload estimate for the UI.
- **DXF writer** (`src/io/DxfWriter.{h,cpp}`, added to pfcore): minimal ASCII
  DXF R12, `POINT` entities grouped by classification layer, plus a R12
  `POLYLINE` outline of the slice box. Projection uses the current clip box's
  thinnest axis; output coordinates are source WORLD coordinates, not centered
  render-space coordinates.
- **Clip-panel export UI** (`src/viewer/main.cpp`): Properties > Clip now has
  "Export slice..." with DXF / PNG / CSV formats. DXF/CSV run as cancellable
  Jobs-panel entries on a background worker and stream directly to file. CSV
  columns are `x,y,z,r,g,b,intensity,classification`.
- **PNG export path**: represented as a Jobs-panel item but executed on the
  render thread because it uses OpenGL. It temporarily aligns an orthographic
  camera to the slice normal, applies the clip box, renders into the existing
  export-sized FBO path, waits for LOD settle up to 240 attempts, EDL-posts,
  encodes PNG with the existing `encodePNG`, then restores camera and clip
  state.
- **Jobs integration**: slice jobs show progress/cancel/reveal in the Jobs
  panel and produce completion/failure/cancel toasts. Running slice exports
  are canceled/joined before loading a different cloud and at shutdown.
- **Smoke hook** (`src/viewer/main.cpp`): `--export-slice <prefix>` loads the
  first positional octree, picks a central metadata slice, exports
  `<prefix>.dxf`, `<prefix>.csv`, and `<prefix>.png`, and exits when all three
  complete. This validates CPU writers and the main-thread GL PNG path without
  manual UI clicks.
- **Test coverage** (`src/tools/pftest/main.cpp`): synthetic cluster test now
  exercises `forEachPointInBox`, verifies all delivered points are inside the
  query box, verifies the returned count, and checks `estimatePointsInBox`
  never undercounts the exact visitor result.

### Verified
- `cmake --build build-static --config Release --target pfview` succeeds and
  produced `build-static/Release/ViitorXPCViewer_v1022.exe` after the smoke
  hook was added.
- `cmake --build build-static --config Release --target pftest` succeeds.
- Focused test run:
  `build-static\Release\pftest.exe --points 200000 --threads 4 --dir build-static\slice_pftest_work`
  PASS.
- Registered CTest:
  `ctest --test-dir build-static -C Release --output-on-failure -R octree_roundtrip`
  PASS.
- Real-model smoke:
  `ViitorXPCViewer_v1022.exe C:\UnrealProject\model\PointForgeCache_direct --export-slice scratch\slice_smoke\PointForgeCache_direct_slice`
  produced:
  - `scratch\slice_smoke\PointForgeCache_direct_slice.dxf` (67,971,198 bytes)
  - `scratch\slice_smoke\PointForgeCache_direct_slice.csv` (42,916,968 bytes)
  - `scratch\slice_smoke\PointForgeCache_direct_slice.png` (497,778 bytes)
- Output validation:
  - PNG signature valid, dimensions **1920×2160**, visually populated with the
    clipped top-down slice.
  - CSV rows: **871,420**.
  - DXF `POINT` entities: **871,420**; header reports `AC1009` (R12) and file
    ends with `EOF`.
  - CSV world coordinate range stayed inside the selected thin slice:
    x `220880..221066`, y `1905890..1906140`, z `314.847..318.784`.

### Modified files
- `CMakeLists.txt`
- `src/io/DxfWriter.{h,cpp}`
- `src/viewer/OctreeStore.{h,cpp}`
- `src/viewer/main.cpp`
- `src/tools/pftest/main.cpp`
- `docs/newdev.md`
- `docs/tasks.md`
- `docs/roadmap.md`
- `docs/project-overview.md`
- `docs/architecture.md`
- `docs/decisions.md`
- `docs/ai_handoff.md`
- `VERSION` was auto-bumped by the normal build-stamp target during the
  `pfview` build.

Pre-existing local modifications still present and not touched intentionally:
`.claude/settings.local.json`, `AGENTS.md`.

### Next Recommended Task
Start newdev.md **#5 Annotations from phone** on a new branch after #4 is
reviewed. Useful #4 follow-ups: add a remote command for slice export if phone
control needs it, and optionally add a true CAD-viewer screenshot check to the
release QA checklist.

## Latest Session (2026-07-07, cont.) - Camera path + MP4 export DONE (newdev.md #3, branch `camera-path-export`)

Two-session feature: previous session implemented it (and its review workflow
died mid-run on a model limit); this session verified completion, re-ran the
smoke, and closed the docs loop. newdev.md #3 = **DONE**.

### What was built (previous session, verified this session)
- **`src/viewer/CamPath.h`** (new): `CamKey{t, px/py/pz, yaw, pitch, ortho,
  orthoSize}` + `CamPath::sample` — non-uniform Catmull-Rom (finite-difference
  tangents; uneven key spacing doesn't overshoot), yaw unwrapped shortest-way,
  pitch clamped ±89, ortho as a step channel. Persisted per-cloud in AppData
  `campaths.txt` (TSV, `# pf-campath 1` header, bookmark-style keying).
- **`src/viewer/VideoExporter.{h,cpp}`** (new): IMFSinkWriter H.264 High MP4
  (hardware transforms enabled, NV12 input, frame-index timestamps so rounding
  never drifts). Owns COM/MFStartup lifecycle; `abort()` skips Finalize and
  deletes the partial file. Non-Windows stub.
- **`src/viewer/Nv12.h`** (new): `rgbToNv12BottomUp` lifted from
  RemoteServer.cpp — shared by the WebRTC stream and the exporter (identical
  BT.601 colour). RemoteServer.cpp now calls the shared helper.
- **main.cpp**: incremental in-frame export loop (export-sized FBO pair +
  EDL post pass; a frame is written only after LOD settle — all frustum
  visible nodes resident, 240-retry cap with warn; 30 ms/frame budget keeps
  the UI live; extra 512-result absorb loop during export); progress modal
  with Cancel, Esc cancels; camera saved/restored; `wantFrame()` suspended
  during export. Properties > "Camera Path" section: key list (DragFloat
  time, re-sort on edit-end, Go/Set/delete), Add Key at Current View, Clear,
  Scrub slider, Preview play/stop, Export MP4 dialog (720p–4K, 24/30/60 fps,
  5–100 Mbps, Browse via new `pf::saveFileDialog`). Toast with [Show]
  Explorer-reveal button (`Toast.revealPath`) + MessageBeep.
  `--export-video <out.mp4>` smoke hook (3-key orbit path, exact dialog
  code path, auto-quit).
- **FileDialog.{h,cpp}**: new `saveFileDialog` (IFileSaveDialog).
- **Remote**: `RemoteConfig` gains `pathKeys/pathDuration/pathPlaying`;
  cmds `path_play`/`path_stop`; webremote CameraTab "Camera Path" card
  (Play/Stop, or author-on-PC hint), `cfg.ts` keys. Export stays PC-only.

### Verified this session
- Build current: v1020 exe postdates all sources; the earlier fprintf format
  warnings (main.cpp campaths save/load) were already fixed — last build log
  clean.
- Re-ran the smoke against v1020: Tikal-13 (912 nodes / 12.4M pts) →
  `--export-video` completed, exit 0, and **ffprobe confirms h264 High
  1920x1080 30 fps nb_frames=121** — a structurally valid, playable MP4.
- Code-reviewed the full diff vs origin/main (VideoExporter lifecycle/error
  paths, Nv12 math, export loop state save/restore, UI guards) — no defects
  found. NOTE: the previous session's adversarial review WORKFLOW died on a
  model limit before reporting; this was a single-lens manual review instead.
- Not exercised live (code-verified only): Cancel/Esc mid-export (abort +
  partial-file delete), phone Play/Stop card, non-ASCII output paths
  (DeleteFileA on abort is ANSI — worst case an orphaned partial file).

### Docs updated (per AGENTS.md loop)
docs/newdev.md (#3 checkboxes + status board DONE + acceptance results),
docs/tasks.md (#3 shipped entry), docs/decisions.md (SinkWriter-not-WebRTC,
in-frame incremental export, LOD settle, Catmull-Rom choices), this file.

### Next Recommended Task
User raises the `camera-path-export` PR. Then newdev.md **#4 Cross-section /
slice export** (branch off main): start with
`OctreeStore::forEachPointInBox` + the DXF R12 writer in pfcore (no GL deps),
then the Clip-panel UI. Optional #3 follow-ups: live-test export Cancel, GPU
NV12 conversion for 4K/60, wide-char DeleteFileW on abort.

## Previous Session (2026-07-07) - Multi-client roles (newdev.md #2, branch `multi-client-roles`)

`parallel-indexer` PR merged; #2 implemented and live-verified.

### What was built
- **Server** (`RemoteServer.{h,cpp}`): second watch-only viewer PIN
  (regenerated each start, guaranteed != driver PIN), `Client.viewer` flag,
  role decided at hello and echoed as `{"role":"driver"|"viewer"}` in
  hello_ok. move/cmd/set from viewers silently ignored — the SERVER is the
  security boundary, UI hiding is just UX. stream/webrtc/state/cfg and
  /shot.png (either PIN) work for both roles. New API: `viewPin()`,
  `setAllowViewers(bool)`, `viewerCount()` (+ stubs). Startup log prints
  both PINs.
- **Viewer prefs** (`main.cpp`): "Allow view-only clients" checkbox
  (persisted `remoteAllowViewers`, applied live), driver+viewer PINs and
  viewer count shown in Preferences > Input and the Connect-phone QR dialog.
- **Webremote**: `useWebSocket` exposes `role`; viewer role renders video +
  status bar + Stream toggle only ("View-only" badge; no Fly overlay, no
  inspector, no move loop, no presets/toggles/speed). `ActionBar` gains
  `readOnly` prop.

### Verified live (real exe, two concurrent WS clients via Playwright)
driver=driver/viewer=viewer roles; viewer's forged move*45 + bookmark_add +
set pointSize all no-ops (camera pos + cfg unchanged); driver move works;
viewer receives JPEG frames + state; bad PIN rejected; browser as viewer
shows the read-only shell over the live Tikal stream (screenshot taken).

### Build gotcha (recurred — recipe in decisions.md)
After merging main, CMakeCache held a RELATIVE VCPKG_OVERLAY_TRIPLETS=./triplets
-> toolset pin silently ignored -> ports rebuilt with MSVC 14.51 -> LNK2019
__std_* against the 14.44 CRT. Fix: wipe build-static/vcpkg_installed,
reconfigure with the ABSOLUTE overlay path.

### Modified files
- src/viewer/RemoteServer.{h,cpp} (roles, viewer PIN, gating, log)
- src/viewer/main.cpp (allow-viewers pref, PIN displays, persistence)
- webremote/src/{useWebSocket.ts,App.tsx,ActionBar.tsx,index.css}
- docs/{newdev,tasks,decisions,ai_handoff}.md

### Next Recommended Task
User raises the `multi-client-roles` PR. Then newdev.md **#3 Camera path
animation + MP4 export** (branch `camera-path-export`): start with
`src/viewer/CamPath.h` keyframes + the offline FBO render loop, MP4 muxing
via IMFSinkWriter (lift the NV12 helper out of RemoteServer into a shared
header first).

## Latest Session (2026-07-06, cont.) - Parallel Indexer implemented (newdev.md #1, branch `parallel-indexer`)

First medium feature from docs/newdev.md landed. Test-first, per the plan.

### What was built
1. **pftest** (`src/tools/pftest/main.cpp`, CMake target under `PF_BUILD_TESTS`,
   CTest `octree_roundtrip`) — the repo's first automated test. Deterministic
   synthetic .xyz (fixed LCG), converts sequential (threads=1) vs parallel,
   asserts hierarchy/octree/meta **byte-identity** for compress on+off, plus
   DFS invariants (reachability, childMask, payload ranges, point counts) via
   OctreeStore (GL-free, pfunity link pattern).
2. **Parallel Phase C** (`OctreeIndexer.cpp`): workers build pure
   `ChunkResult`s (`serializeLocal` + `appendPayloadToBlob` → chunk-local
   records + payload blob + coarse samples); a coordinator drains them **in
   chunk order**, rebases indices/byteOffsets, and owns all global mutation —
   output byte-identical to sequential by construction. In-flight cap
   threads+2; cancel checked by workers + coordinator (old cancel path's
   FILE* leak fixed); "phase C took Xs" timing log added.
3. **FlatCellSet**: `subsample`'s per-insert-allocating `unordered_set`
   serialized workers on the Windows heap lock (threads=16 was SLOWER than
   1). Replaced with a flat open-addressing set — parallel now scales and
   sequential got ~20% faster too.
4. **Config surface**: `IndexOptions::threads` (0=auto), pfconvert
   `--threads`, Convert dialog Advanced "Indexer threads" (0 → "auto").

### Verified
- pftest PASS at 1M/2M/4M/8M points (both compression modes).
- Real scan (Tikal-13.las, 12.4M pts): sha256 of all three output files
  identical between threads=1 and threads=16 at multiple settings.
- Bench: phase C **4.02s → 1.37s (2.9×)** at chunk-depth 3 + compress
  (the Balanced/High preset shape). Finding: uncompressed configs are
  bounded by the serial payload fwrite (disk), not CPU — documented in
  decisions.md.

### Modified files
- src/indexer/OctreeIndexer.{h,cpp} (threads option; parallel Phase C;
  FlatCellSet; serializeLocal/appendPayloadToBlob; ChunkResult)
- src/tools/pfconvert/main.cpp (--threads)
- src/tools/pftest/main.cpp (new)
- src/viewer/main.cpp (Convert dialog "Indexer threads")
- CMakeLists.txt (pftest target, PF_BUILD_TESTS, enable_testing)
- docs/{newdev,tasks,decisions,ai_handoff}.md

### Review round (same session)
Adversarial review (3 lenses, refute pass) confirmed one major defect, fixed
along with test-harness bugs the reviewers spotted:
- **Worker exception → std::terminate**: buildOne allocates heavily (chunk
  load + subtree + blob + zstd bound buffers); a bad_alloc escaping the
  std::thread callable would kill the whole viewer mid-convert (conversion is
  in-process via JobQueue, whose try/catch can't see worker threads). Workers
  and the coordinator drain loop are now wrapped; any failure sets a
  failed/failReason flag, wakes everyone, joins, closes octree.bin and fails
  the job with a logError. fwrite short-writes (disk full) also detected.
- pftest hardening: DFS cycle no longer infinite-loops, child indices
  bounds-checked before dereference, level check de-tautologised (strictly
  increasing), parallel leg fixed at threads=8, and a pickPoint decode check
  through a known synthetic cluster (byte-identity alone can't distinguish
  correct payloads from consistently-corrupt ones).
pftest PASS after all fixes.

### In-app smoke (same session) — DONE
Added a permanent smoke hook: `ViitorXPCViewer --convert <scan>` drives the
Convert dialog's own openConvertDialog+enqueueConvert at startup (same
JobQueue, options, toasts, load-when-done). Live run: Tikal-13.las converted
INSIDE the running viewer on 16 threads (phase C 1.50s, 912 nodes, 12.4M
pts), job succeeded, cloud auto-loaded, process healthy. newdev.md #1 = DONE.
Note: the built exe is version-stamped (`ViitorXPCViewer_v<NNN>.exe`) — glob
for it when scripting.

### Next Recommended Task
User raises the `parallel-indexer` PR. Then branch `multi-client-roles` off
main and start newdev.md **#2 Multi-client roles** (view-only viewer PIN).

## Previous Session (2026-07-06, cont.) - Medium-Feature Implementation Plan (docs/newdev.md)

Planning session — no code changes. Created **docs/newdev.md**: a living,
code-anchored implementation plan for the six mid-term ("1–2 weeks each")
features, with a status board to keep updated as tasks move
(PLANNED/IN PROGRESS/DONE/BLOCKED).

### How the plan was built
A parallel codebase survey (7 scout agents, one per feature area + docs/build
state, each claim then adversarially re-verified against the tree) gathered
file/line anchors, reusable infra, gaps, and risks per feature. Three drifted
anchors were corrected (EmbeddedShaders line refs; EmbeddedWeb.h actually
generates into `build-static/generated/`, not `src/viewer/`).

### Plan contents (docs/newdev.md)
Recommended order + per-feature phased checklists, verified starting points,
risks, and acceptance criteria:
1. **Parallel indexer** — Phase C worker pool; pure-function chunk builds +
   single-writer coordinator; adds the repo's first automated test
   (synthetic .xyz round-trip vs sequential indexer).
2. **Multi-client roles** — view-only role via a second (viewer) PIN;
   server ignores move/cmd/set from viewers; webremote read-only mode.
3. **Camera path + MP4 export** — CamPath keyframes on the bookmark pose,
   Catmull-Rom interpolation, offline FBO render loop, IMFSinkWriter MP4;
   NV12 helper lifted out of RemoteServer.
4. **Cross-section export** — OctreeStore::forEachPointInBox bulk query,
   own DXF R12 writer in pfcore, PNG + CSV, world-coordinate output.
5. **Phone annotations** — generalises the existing tap-to-measure pipeline
   (measure_pick → screenRay → pickPoint); JSON per-cloud persistence;
   GL pins/labels rendered in-pass so they appear in the stream.
6. **Multi-cloud scene** — SceneCloud{store,renderer} vector, scene origin =
   first cloud's cube centre, Scene panel per architecture.md §7; LAST
   because of blast radius.

### Docs updated per AGENTS.md
- docs/newdev.md (new — the plan itself; keep its status board current)
- docs/tasks.md (new "Planned — medium features" section pointing at newdev.md)
- docs/roadmap.md (mid-term goals annotated with newdev.md plan numbers)
- docs/decisions.md (ordering rationale + key approach choices recorded)
- docs/ai_handoff.md (this entry)

### Next Recommended Task
Start newdev.md **#1 Parallel indexer** (lowest coupling, biggest felt win):
begin with the synthetic .xyz generator + sequential round-trip test so the
parallel refactor has a safety net before any threading lands. Update the
newdev.md status board when picking it up.

## Previous Session (2026-07-06, cont.) - Look-Speed Slider Had No Effect (Server-Side Clamp Bug)

User reported the web app's Look sensitivity slider did nothing regardless
of value. Root cause was NOT in the slider or the client's scaling math —
both were already correct.

### Root cause
`RemoteServer.cpp`'s `move` message parser ran `yaw`/`pit` through the same
`ax()` clamp as `f`/`s`/`u` — `[-1, 1]`. That clamp is correct for `f/s/u`,
which are genuine joystick-style axes (a held stick's deflection, bounded by
design). But `yaw`/`pit` are a **raw per-tick rotation delta**
(`dx * lookSpeed` computed client-side in `FlyTab.tsx`), not a bounded axis —
almost any real mouse-drag or touch-swipe pixel delta, once multiplied by
*any* `lookSpeed` value from 0.1 to 5.0, already exceeds 1. The clamp
silently saturated it to the same ±1 either way, so the slider had zero
observable effect on rotation speed — the value it was supposed to control
never survived past the server's parser.

### Fix
Split the move-message parsing: `f`/`s`/`u` keep the `[-1,1]` clamp
(`ax()`); `yaw`/`pit` get a new `rot()` helper that only rejects non-finite
values (NaN/Infinity) and clamps to a generous `±5000` sanity bound —
functionally unclamped for any real input, just guarding against garbage
from a malformed/malicious client. This is server-side only; no client
changes were needed since the client's scaling was already correct — it's
purely a case of the server discarding what the client sent.

### Verified
- `cmake --build ... pfview` clean (`v1.0.13`); embed hash unchanged (no web
  UI change this round, as expected — this is a pure C++ fix).
- NOT yet tested live: haven't confirmed the Look slider now actually changes
  perceived rotation speed against a running exe.

### Modified files
- src/viewer/RemoteServer.cpp (`rot()` helper, `#include <cmath>`)
- docs/ai_handoff.md

### Next Recommended Task
- Live-verify: connect the web app, drag to look at Look sensitivity 0.5
  then 5.0, confirm the rotation rate visibly changes for the same drag
  speed. Also worth double-checking the Zoom slider isn't hit by the same
  clamp-on-a-delta pattern (`f` IS run through `ax()` currently, and zoom
  writes into `moveRef.current.f` the same way `orbit`'s yaw/pit did — if
  zoom's `f` value also regularly exceeds 1 before clamping, the Zoom slider
  could have an identical bug, just not yet reported).
- Still open: the `.app-shell`/`.workspace` flex-overflow audit for the
  in-app status bar clipping.

## Previous Session (2026-07-06, cont.) - Measure Lines Not in Stream + Desktop Controls Didn't Match PC

Two user-reported follow-ups, both real bugs.

### Bug 1: "Measure lines are not appearing in Web App"
Root cause: the web-remote video capture happens on the raw post-EDL
backbuffer **before any ImGui drawing occurs** (`main.cpp` ~1533, comment:
"Backbuffer now holds the post-EDL scene and no UI yet"). The measurement
polyline was drawn entirely via `ImGui::GetForegroundDrawList()`, which only
rasterizes later in the frame — so it never reached the captured frame,
even though the numeric point list (via `cfg.measurePts`) was already
correctly showing in the ToolsTab. Restructuring to a split ImGui
render (draw-some/capture/draw-rest) was considered and rejected as too
invasive/risky for what's needed.

**Fix**: the polyline (lines + point markers) is now drawn as **real GL
geometry**, baked into the same offscreen FBO the point cloud renders to —
inside `renderPass()` itself, right after the octree traversal, so it uses
that eye's exact `vp` matrix (correct for both mono and stereo SBS) and gets
captured by the stream like any other scene content. New tiny shader pair
(`kLineVertSrc`/`kLineFragSrc` in `EmbeddedShaders.h`, `Shader lineShader` +
a small dynamic VBO in `main.cpp`) — position-only, flat colour, drawn with
depth test disabled (matches the old ImGui overlay's always-on-top look).
The ImGui overlay now draws **only** the per-segment distance text labels
(no GL text renderer exists, so labels stay PC-only) — the old
`AddCircleFilled`/`AddCircle`/`AddLine` calls were removed to avoid double-
drawing the same geometry twice.

**Caught while implementing**: `GL_PROGRAM_POINT_SIZE` is already enabled
once globally at init for the point-cloud shader's `gl_PointSize`. My first
draft toggled it off after drawing the measurement points, which would have
broken the *second* eye's point-cloud rendering in stereo SBS mode (state
leaks across the two `renderPass` calls in the same frame). Fixed by not
touching that global state at all — the shader's own `gl_PointSize` write
is all that's needed. Depth-test on/off is contained within the same block
and safe.

### Bug 2: "Windows controls not working... should be same as C++ app"
Follow-up to last session's desktop input work — I had *invented* a mapping
(left-drag=look, right-drag=pan) without checking the native app's actual
scheme first. Checked `main.cpp`'s real `SDL_MOUSEBUTTONDOWN` handlers:
**LMB = orbit** (rotates the camera *position* around a pivot,
`Camera::orbit`), **RMB = free-look** (`Camera::addYawPitch`, no position
change), wheel = zoom, **Q/E = down/up** (not Space/Ctrl), Shift = 5x boost.

Free-look was already replicable remotely (the existing `yaw`/`pit` move
fields feed `addYawPitch` either way) — but **orbit was not**: it needs a
server-maintained pivot point re-established each drag-start, which no
existing remote input carried.

**Fix**:
- New `orbit: 0|1` field on the `move` WS message (`RemoteServer`: new
  `std::atomic<bool> orbit`, parsed alongside f/s/u/yaw/pit/boost, exposed
  via `RemoteServer::orbit()`, PF_WITH_REMOTE-absent stub returns `false`).
- `main.cpp`'s remote-input block now branches: `remote.orbit()` true ->
  on the rising edge (drag start) recompute `pivot = cam.position +
  cam.front()*dist` exactly like the local LMB handler, then call
  `cam.orbit(dYaw, dPit, pivot)` every tick; otherwise (RMB-equivalent) the
  unchanged `cam.addYawPitch(dYaw, dPit)` path. `Camera::orbit()` internally
  calls `addYawPitch` with identical units, so no rescaling was needed.
- `FlyTab.tsx`: **left-drag now sends `orbit:1`** while held (matches LMB);
  **right-drag is free-look** (matches RMB, previously incorrectly mapped
  to "pan" — dropped, since the PC has no mouse-pan at all); LMB is still
  reclaimed for tap-to-measure while the Measure tool is active (matching
  the PC exactly — confirmed RMB free-look correctly keeps working while
  measuring on the PC too, so the client's `measuringRef` early-return was
  narrowed to LMB only, not both buttons). Keyboard: `Space`/`Ctrl`/`C` ->
  **`Q`/`E`** for down/up, matching `main.cpp`'s `SDL_SCANCODE_Q`/`_E`
  exactly. The Pan sensitivity slider is now touch-only (hidden on desktop,
  since there's no mouse gesture left that uses it).
- Touch gestures are unchanged (still simple unified look/pinch, no
  orbit-vs-look distinction — reasonable for a single-finger surface, and
  not what was reported).

### Verified
- `npm run build` and `cmake --build ... pfview` both clean.
- Embedded web hash confirmed matching `webremote/dist/assets/*` (`v1.0.12`).
- NOT yet tested live: neither the GL-rendered polyline (does it actually
  show correctly in a real stream frame, at the right screen position) nor
  the remote-orbit pivot math (does a LMB-drag on the web app orbit around
  the same point a local LMB-drag would) have been checked against a running
  exe with a loaded cloud.

### Modified files
- src/viewer/EmbeddedShaders.h (kLineVertSrc/kLineFragSrc)
- src/viewer/main.cpp (line-shader setup + draw call in renderPass; ImGui
  overlay trimmed to text-labels-only; remote orbit wiring)
- src/viewer/RemoteServer.h, RemoteServer.cpp (orbit field + accessor + stub)
- webremote/src/FlyTab.tsx (LMB=orbit/RMB=look swap, Q/E keys, Pan hidden on desktop)
- webremote/src/App.tsx (orbit field threaded through the 30 Hz move loop)
- docs/ai_handoff.md

### Next Recommended Task
- Live-verify both fixes against a loaded cloud: (1) start Measure from the
  web app, place a couple of points, confirm the yellow polyline is visible
  *in the video stream itself*, not just the PC's own window; (2) LMB-drag
  on the web app (desktop browser) and confirm it orbits around the same
  point a local LMB-drag would, RMB-drag free-looks, Q/E move up/down, wheel
  zooms, Shift boosts.
- Still open from earlier: `.app-shell`/`.workspace` flex-overflow audit for
  the in-app status bar clipping.

## Previous Session (2026-07-06, cont.) - Remote Tap-to-Measure

User asked to check the Measure feature from the web app. Diagnosis: the
Start/Stop/Undo/Clear controls were already correctly wired end-to-end
(traced `cmd('measure')` -> server -> `cfg.tool` echo), but there was **no
way to place a point remotely** — points only ever got added by an LMB click
on the PC's own viewport (`main.cpp:1434`, local mouse ray -> `pickPoint`).
`FlyTab.tsx`'s touch/mouse-drag overlay had zero awareness of measure mode,
so tapping the video just looked/panned the camera; nothing added a point.
User chose to have this built as a real feature rather than just documented.

### What was built
- **Server** (`main.cpp`): new `measure_pick` remote cmd, active only while
  `toolMode == TOOL_MEASURE`. Reuses the existing `RemoteCmd` vec3 payload
  (`v:[nx, ny, 0]`, normalized 0..1) and feeds the exact same `pendingPick`
  path a local LMB click uses (`cam.screenRay` + `store.pickPoint`) — just
  computes `pickX/pickY` as `nx*winW`/`ny*winH` instead of reading the SDL
  mouse position. Since the video stream is a downscaled copy of the same
  window (same aspect), this maps 1:1 onto the local pick math with no new
  ray-casting code.
- **`VideoLayer.tsx`**: `VideoLayerHandle` gains `getNaturalSize()` — reads
  `videoWidth/videoHeight` (WebRTC) or `naturalWidth/naturalHeight` (JPEG
  `<img>`), i.e. the actual decoded frame resolution, not the CSS box.
  Needed to correctly invert `object-fit:contain`'s letterboxing.
- **`FlyTab.tsx`**: new `measuring`/`send`/`getVideoNaturalSize` props.
  `toVideoNormalized(clientX, clientY)` maps a viewport-relative point
  through the letterbox math back to 0..1 video-content coordinates,
  returning `null` if the point falls in a letterbox bar (untappable) or no
  frame has decoded yet. A short tap/left-click (moved less than
  `TAP_MOVE_THRESHOLD`=10px between press and release, tracked separately
  from the continuously-updated look-drag delta) fires `sendMeasurePick`;
  anything longer is treated as a drag as before. While `measuring`, the
  primary pointer's look/pan is suspended entirely (matches the PC's "measure
  mode reclaims LMB" — right-drag pan and wheel zoom stay active). Footer
  hint changes to "Tap the video to place a measurement point".
- **`ToolsTab.tsx`**: added the same hint line under the Start/Stop button.

### Verified
- `npm run build` clean; `cmake --build ... pfview` clean (both TSX and the
  new C++ `measure_pick` branch compile with no errors/warnings).
- Embedded web hash confirmed matching `webremote/dist/assets/*` exactly
  (`v1.0.10`).
- NOT yet tested against a live connection — needs a real loaded cloud +
  phone/desktop browser session to confirm a tap actually lands a point at
  the correct 3D location (the letterbox math is unit-reasoned, not
  measured against a real stream yet).

### Modified files
- src/viewer/main.cpp (`measure_pick` cmd handler)
- webremote/src/VideoLayer.tsx (`getNaturalSize`)
- webremote/src/FlyTab.tsx (tap-to-pick, measuring-aware input)
- webremote/src/App.tsx (wires `measuring`/`send`/`getVideoNaturalSize` into FlyTab)
- webremote/src/ToolsTab.tsx (hint text)
- docs/ai_handoff.md

### Next Recommended Task
- Load the rebuilt exe with a real cloud, enable Measure from the web app,
  and tap the video at a known feature — confirm the placed point lands
  where expected (verifies both the letterbox-inversion math and the
  server's `nx*winW`/`ny*winH` mapping together).
- Test at a non-16:9 window size / narrow inspector width specifically,
  since that's where letterboxing (and thus the correction math) actually
  matters — a coincidentally-matching aspect ratio would pass even with a
  broken letterbox calculation.

## Previous Session (2026-07-06, cont.) - Desktop Camera Input, Dropdown, Slider Audit

Closes the "Windows controls not working from web app via Computer browser"
report, plus a full slider-wiring audit and two smaller UI fixes.

### Root cause of "speed slider does nothing" / "controls not working"
`FlyTab.tsx` only ever wired **touch** events (`onTouchStart/Move/End`) — a
desktop browser has no touch surface, so opening the web remote on a Windows
PC had **zero** camera-movement input at all (mouse-drag did nothing; the
only thing that worked was mouse-clicking the UP/DOWN/BOOST hold buttons,
since those use pointer events). With no baseline movement to scale, the
Speed slider had nothing to visibly multiply — it was never actually broken,
there was just no motion for it to act on.

### Fix — desktop mouse + keyboard input (FlyTab.tsx)
Added a parallel input path, gated on `matchMedia('(pointer: fine)')`
(desktop/mouse), alongside the existing untouched touch path:
- **Left-drag** → look (same yaw/pitch math as 1-finger touch, reusing the
  Look sensitivity slider).
- **Right-drag** → pan (reuses the Pan slider; `stage` gets
  `onContextMenu` suppressed on desktop so the browser context menu doesn't
  interrupt the drag).
- **Wheel** → zoom (reuses the Zoom slider).
- **WASD** → fly forward/back/strafe, **Space/Ctrl/C** → up/down, **Shift**
  → boost — window-level keydown/keyup (ignored while focus is in a text
  input), applied through the same `heldState` ref the UP/DOWN/BOOST buttons
  already used, extended to also cover `f`/`s`.
- Mouse-drag listeners are attached at `window` level (not the stage element)
  so a drag started inside the viewport keeps tracking even if the cursor
  leaves it — matches how a native FPS-style camera control normally feels.
- The "Gesture Sensitivity" panel becomes "Mouse & Keyboard" on desktop, the
  pinch-mode toggle (a touch-only concept) is hidden, and the instruction
  footer text updates to describe the new controls.

### Slider wiring audit (user asked to check all sliders)
Cross-referenced every `Slider`/`Toggle` in `DisplayTab.tsx`, `CameraTab.tsx`,
`ToolsTab.tsx`, and `ActionBar.tsx` against `SettableKey` (`cfg.ts`) and the
server's `set.<key>` handler table (`main.cpp` ~1182-1209). **All of them are
correctly wired end-to-end** — no missing/mismatched keys found. The
`camSpeedMultiplier` path itself is also correct server-side (`set.speed` and
the `speed` one-shot cmd both update it, and it's applied to remote-driven
movement at `main.cpp:1167`) — confirms the "speed does nothing" symptom was
purely the "no movement to scale" root cause above, not a wiring bug.

### Other fixes
- **Property panel rail padding**: `.inspector-tab`/`.inspector-icon-rail`
  widened 48px -> 60px with `padding: 0 6px`, letter-spacing trimmed
  0.05em -> 0.03em — "DISPLAY" (the widest label) was cramped against the
  divider. Verified via a detached-DOM measurement at desktop width: 60px
  box, "DISPLAY" text (42px) comfortably inside the 48px content area.
- **Stream Engine -> dropdown**: the 2-option segmented control
  ("JPEG (Compat)"/"WebRTC (F...") was visibly truncated in the inspector's
  default width. Replaced with the existing `Select` dropdown component
  (same one used for Color mode). Extended `Select` with an optional
  `disabledOptions` array so WebRTC can render as a disabled `<option>`
  instead of a disabled segmented button when the server build lacks it.

### Verified
- `npm run build` clean.
- Structural checks via `preview_eval` (detached DOM, since the dev preview
  has no backend to reach the post-auth app shell): inspector-tab renders at
  60px/9.5px/6px-padding on desktop viewport (was hitting a *different*,
  correct mobile override at the default narrow preview viewport — false
  alarm on first check, resolved by resizing to 1600×900); Select dropdown
  renders with the second option correctly `disabled`.
- Exe rebuild in progress at handoff time — confirm completion + bump before
  further work.
- NOT yet tested against a live connection: the new mouse-drag/wheel/WASD
  path needs a real desktop-browser session against the running exe to
  confirm it actually moves the camera (only verified the code compiles and
  the touch path's existing tests still typecheck).

### Modified files
- webremote/src/FlyTab.tsx (desktop mouse+keyboard input)
- webremote/src/controls.tsx (Select gains `disabledOptions`)
- webremote/src/DisplayTab.tsx (Stream Engine -> Select)
- webremote/src/index.css (inspector-tab/rail padding)
- docs/ai_handoff.md

### Next Recommended Task
- Load the rebuilt exe, open the web remote from a Windows desktop browser,
  and confirm: left-drag look, right-drag pan, wheel zoom, WASD movement,
  Space/Ctrl up-down, Shift boost, and that the Speed slider now visibly
  changes movement rate.
- Then the one item still open from earlier: `.app-shell`/`.workspace`
  flex-overflow audit so the in-app status bar can't be clipped at desktop
  window sizes (unrelated — that's the native viewer's own status bar, not
  the web remote's).

## Previous Session (2026-07-06, cont.) - Build System Bug: Exe Was Embedding a Stale Web Build

User reported the exe didn't reflect the latest webremote changes. Root cause
was in the build system, not the app — every rebuild this session had
silently linked against a **stale** `EmbeddedWeb.h`.

### Root cause
`CMakeLists.txt`'s web-embedding step was `add_custom_command(OUTPUT
EmbeddedWeb.h DEPENDS web_build ...)`. `web_build` (an `ALL` custom target)
reliably reran `npm run build` every invocation, correctly refreshing
`webremote/dist/*` with new content-hashed filenames each time. But the
*downstream* step's `DEPENDS web_build` names a **target**, not a file — the
Visual Studio generator has no file timestamp to compare, so it could not
reliably tell the header needed regenerating and sometimes skipped it. Caught
by comparing mtimes directly: `EmbeddedWeb.h` was stamped ~13:17 in
[Previous Session] while several *later* builds (13:39, 13:41, 14:00...) kept
relinking the exe without ever touching it — every one of those exes silently
shipped an old web build despite the build log showing `npm run build`
succeed each time.

### Fix
Converted `embed_web_header` to a plain `add_custom_target(... ALL)` that
always runs `tools/embed_web.cmake` — the same always-run pattern already
used successfully by `web_build`, `version_header`, and the exe-rename
`POST_BUILD` step (see decisions.md). Verified: rebuilt, and
`EmbeddedWeb.h`'s embedded hashed filenames (`index-BSgfsghI.js`,
`index-DrDHFgQ7.css`) now match `webremote/dist/assets/*` exactly, with
`dist/index.html` (13:17:56) -> `EmbeddedWeb.h` (13:17:57) -> exe link
(13:18:07) in correct sequence.

### Modified files
- CMakeLists.txt (embed_web_header target)
- docs/{decisions,ai_handoff}.md

### Next Recommended Task
- **Important**: every exe built earlier this session (`v101`-`v106`) may
  have shipped a stale web UI depending on exactly when the staleness first
  crept in — only `v107` onward is verified correct. If any of those earlier
  exes were shared/tested, re-verify or just use `v107+`.
- Then resume the still-open items: desktop browser mouse/keyboard camera
  control, and the `.app-shell`/`.workspace` flex-overflow audit for the
  in-app status bar.

## Previous Session (2026-07-06, cont.) - Connect Screen: Centered Pair + Orientation Layout

Follow-up fixes to the premium connect-screen redesign (same branch,
`minor-fixes`), driven by a screenshot showing the branding panel and auth
card stretched to opposite edges of the screen instead of together as a pair.

### What was fixed
- **Centered pair, not edge-stretched**: `.connect-layout` was a `grid`
  with `minmax(320px,34%) 1fr` columns spanning the full viewport width —
  correct proportions, wrong idea (branding pinned far left, card far right).
  Changed to a `flex` row sized to its own content, centered as one unit via
  `.connect-screen`'s own `align-items/justify-content: center` — verified
  393px margins on both sides at 1600px wide (i.e. genuinely centered, not
  just proportional).
- **Logo/title misalignment root cause**: `.brand-logo-pulse` (the glow ring)
  is `position:absolute; inset:-14px`, so its left edge sits 14px outside
  `.brand-logo`'s own box — the icon visually sat left of the title text
  below it. Fixed with `margin-left:14px` on `.brand-logo` so the ring's
  *outer* edge — not the icon image — shares the title's left edge. Also
  widened the logo-to-title gap (`margin: 14px 0 24px 14px`).
- **Landscape/portrait now keyed on `orientation`, not `max-width`**: a wide
  tablet held in landscape now correctly keeps the side-by-side pair (used
  to force-stack under 1024px regardless of orientation); any portrait
  viewport (phone or tablet) stacks branding above the card, centered.
- **Page-level padding**: `.connect-screen` itself now does the
  centering + `padding: clamp(20px,5vh,56px) clamp(16px,4vw,48px)`, so
  nothing (logo, card, keypad, status row) can ever touch the viewport edge
  — verified 29px top/bottom padding and a 28px branding-to-card gap with
  zero overlap at 375×812 (mobile portrait).
- **Found and fixed a real regression while debugging**: `body { zoom: 1.2 }`
  — flagged in an earlier UX audit as a real bug and supposedly removed —
  was still present. `git log -p` showed it was only ever added once and
  never actually removed: my earlier fix must have been silently reverted
  when PR #3's `webapp-controller` -> `main` merge landed an older copy of
  `index.css`. Removed again; also re-applied a second casualty of that same
  merge, the `100dvh` fallback on `html,body,#root` (mobile browser-chrome
  viewport tracking). Spot-checked several other survivors of that fix batch
  (safe-area insets, coarse-pointer sizing, `.toolbar-group--dup`,
  `--text-3` contrast, `.seg` grid) — those did survive the merge intact.

### Verified
- `npm run build` clean.
- Geometry-checked (not screenshot — `preview_screenshot` was rendering a
  tiny mis-scaled thumbnail in this session, contradicted by
  `getBoundingClientRect` showing correct full-size layout; treated as a
  tool artifact, not a real bug) via `preview_eval`: 1600×900 desktop shows
  393px/393px side margins (centered), 72px inter-panel gap, logo-ring left
  edge flush with title left edge, 32px logo-to-title gap; 375×812 mobile
  shows 29px top/bottom padding, 28px vertical gap, zero overlap, and
  `scrollWidth === clientWidth` (no horizontal scroll).
- Exe rebuild was in progress at handoff time — confirm it completed and the
  version bumped before further changes.

### Modified files
- webremote/src/index.css (Connect Screen layout section)
- docs/ai_handoff.md

### Next Recommended Task
- Load the rebuilt exe in a real browser (not just the no-backend dev
  preview) and eyeball the connect screen once with actual PIN
  entry/connect/pin_bad states.
- Then resume the two items still open from earlier this session: desktop
  browser mouse/keyboard camera control, and a flex-overflow audit of
  `.app-shell`/`.workspace` so the in-app status bar can't be clipped at
  desktop window sizes (unrelated to the connect screen — that's the
  post-authentication app shell).

## Previous Session (2026-07-06, cont.) - Web Remote Connect Screen: Premium Redesign

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
