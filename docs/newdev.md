# newdev.md — Medium-Feature Implementation Plan (living document)

> **How to use this doc (per AGENTS.md):** this is the working plan for the six
> "medium" (1–2 week) features. Whoever picks up a task: set its status here,
> keep the checklists current as you land pieces, and on completion also update
> `tasks.md` (what shipped), `decisions.md` (any major choice), `ai_handoff.md`
> (session record + next task). Statuses: `PLANNED` / `IN PROGRESS` / `DONE` /
> `BLOCKED(reason)`.

**Baseline at planning time:** branch `minor-fixes`, v1.0.13, clean tree.
`webapp-controller` merged (PR #3). Canonical build: `build-static/Release/`
(static single-file exe; VERSION auto-bumps every build; `EmbeddedWeb.h`
regenerates unconditionally into `build-static/generated/` — webremote changes
always need an exe rebuild). All code anchors below were verified against the
tree on 2026-07-06.

## Status board

| # | Feature                              | Status  | Owner | Notes |
|---|--------------------------------------|---------|-------|-------|
| 1 | Parallel indexer (Phase C)           | DONE | branch `parallel-indexer` | 2.9× real-scan phase C; byte-identical output; pftest added; in-app JobQueue smoke ✔ (`--convert` hook) |
| 2 | Multi-client roles (view-only mode)  | DONE | branch `multi-client-roles` | viewer PIN, server-side gating, read-only UI; live-verified driver+viewer side by side |
| 3 | Camera path animation + MP4 export   | DONE | branch `camera-path-export` | CamPath spline + IMFSinkWriter MP4; smoke-verified 1080p30 export (121 frames) via `--export-video` hook |
| 4 | Cross-section / slice export         | DONE | branch `cross-section-slice-export` | real-model smoke: DXF/CSV/PNG exported from `PointForgeCache_direct`; 871,420 pts; PNG visually verified |
| 5 | Annotations from phone               | DONE | branch `annotations-from-phone` | remote/local annotate mode, JSON persistence, synced PC/phone UI |
| 6 | Multi-cloud scene                    | DONE | branch `multi-cloud-scene` (finished on `vxpc/thumbnails`) | SceneCloud vector + scene origin, Scene panel (left dock), remote `clouds` cfg + `cloud_vis` cmd + webremote card; audit fixed 6 scene-space/CSV bugs |
| 7 | GLB (glTF 2.0) export                 | DONE | branch `testing-application` | `GlbWriter` in pfcore (points + `KHR_gaussian_splatting`), File▸Export▸GLB dialog (Ctrl+E): scope/layout/region/decimation/Y-up; `--export-glb` smoke hook; verified dynamic + static single-file on Tikal-13. FBX deferred behind same enum |

**Recommended order = table order.** Rationale: 1–2 are low-coupling and ship
user-visible value fast (1 touches only pfcore, 2 touches only RemoteServer +
webremote). 3–4 reuse infra that is fresh and verified (MFT encoder, clipping,
pickPoint). 5 builds directly on 3/4-era streaming + the existing
tap-to-measure pipeline. 6 is the biggest architectural change (touches
OctreeStore, PointRenderer, camera space, GPU budget) — do it last so the
refactor doesn't destabilise the other five mid-flight.

---

## 1. Parallel indexer (Phase C)  — ~1 week

**Goal:** Phase C builds each chunk subtree on a worker pool; conversion 4–8×
faster on big scans. No format change, no UI change (Jobs panel just gets
faster + progress stays monotonic).

**Verified starting points**
- `src/indexer/OctreeIndexer.cpp` — Phase C loop lines ~298–329 (sequential:
  load chunk file → build LocalNode subtree → `serialize()` appends
  NodeRecords + payload to `octree.bin`); `serialize()` lines ~103–153 (writes
  at 129/134/142, `offset` bumped at 131/136/144, `hierarchy.push_back` 151);
  coarse accumulation `coarse.insert()` ~314; `chunkRoots` map insert ~317;
  progress callback at chunk boundaries ~323–327; cancel check ~299.
- Shared-state hazards (all confirmed): single `FILE*` writes, mutable
  `uint64_t& offset`, `std::vector<NodeRecord> hierarchy` growth,
  `unordered_map chunkRoots`, `coarse` vector appends.

**Plan**
- [x] Phase C job = pure function `buildOne(seq) -> ChunkResult{records,
      blob, coarseSamples, localRootIdx}` (chunk-local indices/offsets; zstd
      per-node inside the job). `serializeLocal` + `appendPayloadToBlob`
      replace the old FILE*-writing `serialize`.
- [x] Worker pool (`min(threads?:hw, chunks, 64)`) + condition variables;
      coordinator drains **in chunk order** → output byte-identical to the
      sequential build (stronger than planned; verified by pftest and by
      sha256 on a real 12.4M-pt LAS).
- [x] All global mutation (fwrite, hierarchy splice, offset/chunkRoots/coarse)
      on the coordinator thread only; zero locks on the build path.
- [x] Progress from coordinator (same 64-chunk cadence); cancel checked by
      workers per chunk AND by the coordinator (also fixed a pre-existing
      FILE* leak on the old cancel path).
- [x] In-flight cap = threads + 2 (bounded parked results).
- [x] `--threads <n>` pfconvert flag, `IndexOptions::threads` (0 = auto),
      Convert dialog Advanced "Indexer threads" (0 shows "auto").
- [x] Bonus: `subsample`'s `std::unordered_set` (heap node per insert —
      serialised all workers on the Windows heap lock) replaced with a flat
      open-addressing `FlatCellSet` — speeds up sequential builds too.
- [x] Bench (Tikal-13.las, 12.4M pts, 16 threads auto): chunk-depth 3 +
      compress: **4.02s → 1.37s (2.9×)** phase C, outputs byte-identical.
      Finding: with compression off, phase C is dominated by the serial
      ~270 MB payload fwrite (disk-bound; same either mode) — speedup shows
      where CPU dominates, i.e. compressed presets (Balanced/High default to
      compress=on) and deep subtrees. Synthetic pftest: ~2.0–2.4× phase C.
- [x] Test harness: `pftest` target (`src/tools/pftest/main.cpp`) — first
      automated test in the repo. Deterministic synthetic .xyz → sequential
      vs parallel byte-identity (hierarchy/octree/meta) for compress on+off,
      + DFS structural invariants via OctreeStore, + a pickPoint decode check
      through a known synthetic cluster. Wired as CTest `octree_roundtrip`.
      `PF_BUILD_TESTS` option (ON).
- [x] Adversarial review round (3 lenses + refute pass) applied: worker and
      coordinator bodies are exception-safe (a bad_alloc in a chunk build now
      FAILS THE JOB instead of std::terminate-ing the viewer — conversion
      runs in-process via JobQueue); workers always joined before unwinding;
      fwrite short-write detected (disk full); pftest DFS no longer loops on
      a cyclic hierarchy, bounds-checks child indices before dereference, and
      its parallel leg uses fixed threads=8 (never vacuously sequential).

**Risks:** ~~determinism~~ resolved — in-order splice makes output
byte-identical. Phase C2 starts only after workers join. Remaining known
limit: write-bound (uncompressed, huge payload) configs gain little — that's
I/O, not thread count.

**Acceptance:** ✔ same cloud → byte-identical loadable octree; ✔ 2.9× on a
real scan at compressed preset settings (≥3× target expected to hold on
≥100M-pt scans where subtree CPU dominates further); ✔ cancel aborts within
one chunk; ✔ docs updated; ✔ in-app JobQueue path smoked live via the new
`ViitorXPCViewer --convert <scan>` hook (drives the dialog's own
enqueueConvert at startup): Tikal-13.las converted on 16 threads inside the
running viewer, job completed, cloud auto-loaded (912 nodes / 12.4M pts).

---

## 2. Multi-client roles — view-only mode  — ~3–5 days

**Goal:** second device can watch (stream + state/cfg) without being able to
drive. "Boss watches on tablet while you drive."

**Verified starting points**
- `src/viewer/RemoteServer.cpp` — `Client` struct lines ~123–132 (`authed`,
  `badTries`, `stream`, `webrtc`, per-client `pc`/`videoTrack`); PIN gate
  ~616–630 (3 tries → drop); all `move`/`cmd`/`set` handling is gated only by
  `cl.authed`; input is last-write-wins into shared atomics; broadcasts
  already iterate all authed clients; `recountLocked()` ~581–591.
- Webremote: `useWebSocket.ts` hello flow; `App.tsx` gates UI on
  `status === 'connected'`.

**Plan**
- [x] `Client.viewer` flag. Two PINs: driver (existing) + independent viewer
      PIN, both regenerated per start (guaranteed distinct). `hello{pin}`
      matches either → `hello_ok` carries `{"role":"driver"|"viewer"}`.
- [x] Server gating: `move`/`cmd`/`set` from a viewer silently ignored;
      `stream`/`webrtc_*` + `state`/`cfg` broadcasts for both roles.
      `/shot.png` accepts either PIN (read-only by nature).
- [x] Single-driver arbitration explicitly out of scope (last-write-wins
      kept) — recorded in decisions.md.
- [x] Webremote: `role` from `hello_ok` via useWebSocket; viewer role = video
      + status bar + stream controls only (no Fly overlay/gestures, no
      inspector, no move loop, no presets/toggles/speed), "View-only" banner.
- [x] Viewer preferences: "Allow view-only clients" checkbox (persisted,
      default on, applied live via setAllowViewers); driver+viewer PINs and
      viewer count shown in Preferences > Input and the QR dialog.
- [x] Security: viewer PIN always required; allow-viewers off = viewer PIN
      rejected entirely.

**Risks:** broadcast fan-out already holds `clMx` during the whole loop — more
clients amplifies the slow-client stall risk (documented in scout); acceptable
for LAN v1, note as follow-up. Don't touch the JPEG/WebRTC per-client encode
paths (they already handle N clients).

**Acceptance:** ✔ all verified live against the real exe (two concurrent WS
clients): driver role moves camera; viewer's forged `move`×45 + `cmd
bookmark_add` + `set pointSize` all ignored (camera pos, cfg unchanged);
viewer receives JPEG frames + state broadcasts; bad PIN still rejected; both
PINs rotate on restart (distinct by construction). Browser UI as viewer:
"View-only" badge over live video, toolbar reduced to brand+Stream, no
inspector/gestures/move-loop.

---

## 3. Camera path animation + MP4 video export  — ~2 weeks

**Goal:** keyframe camera poses on a timeline, smooth interpolation, offline
render at chosen resolution/fps, encode straight to `.mp4`. Demo/deliverable
feature.

**Verified starting points**
- Poses: `CamBookmark` struct + TSV persistence `main.cpp` ~702–751 — the
  keyframe is literally a bookmark + time.
- Offscreen render: `edlFbo` + `ensureFbo()` ~408–432, `renderPass` lambda
  ~1371, post-process pass ~1565–1583 — the scene already renders to FBO every
  frame at arbitrary size.
- Readback + encode: `readFramebufferRGBA` (132), RGB→NV12 conversion
  `RemoteServer.cpp` ~380–406, MFT encoder `initMF`/`createHardwareEncoder`/
  `configureEncoder`/`processOneOutput`/`pumpAsyncEvents` ~185–505 (hardware
  NVENC verified live on this machine).
- Missing (confirmed): any `IMFSinkWriter` usage, keyframe interpolation,
  fixed-timestep offline loop, export UI.

**Plan**
- Week 1 — engine:
  - [x] `src/viewer/CamPath.h` — `CamKey{t, px/py/pz, yaw, pitch, ortho,
        orthoSize}` + **non-uniform** Catmull-Rom (finite-difference tangents,
        so unevenly spaced key times don't overshoot); yaw unwrapped to the
        shortest way across the four control points; pitch clamped ±89 at
        sample time; ortho is a step channel (previous key wins). Persisted
        per-cloud in AppData `campaths.txt` (TSV, `# pf-campath 1` header,
        same keying as bookmarks).
  - [x] `src/viewer/VideoExporter.{h,cpp}` — self-contained IMFSinkWriter
        wrapper (`MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS`, NV12 in → H.264
        High MP4 out, per-frame-index timestamps so rounding never drifts).
        Owns its COM/MFStartup lifetime (ref-counted, tolerates the app's
        apartment); `abort()` skips Finalize and deletes the partial file.
        NV12 conversion lifted out of RemoteServer into shared
        `src/viewer/Nv12.h` (`rgbToNv12BottomUp`, used by both paths).
  - [x] Offline render loop: incremental in-frame job (not a modal freeze) —
        each viewer frame renders as many settled export frames as fit a
        30 ms budget into an export-sized FBO pair (scene + EDL post), reads
        back RGB, NV12-converts, SinkWriter-writes. A frame is only written
        once its frustum-visible nodes are all GPU-resident (LOD settle, max
        240 retries then written-with-warning). Progress modal + Cancel;
        Esc cancels too; camera saved/restored around the export.
- Week 2 — UX + polish:
  - [x] Properties > "Camera Path" collapsing header: keyframe list
        (per-key DragFloat time — re-sorts on edit-end so mid-drag doesn't
        reshuffle, Go / Set-to-current-view / delete), Add Key at Current
        View (+2 s after last), Clear, duration readout, Scrub slider (moves
        camera live), Preview play/stop, "Export MP4..." dialog (720/1080/
        1440/4K, 24/30/60 fps, 5–100 Mbps slider default 20, Browse via
        `pf::saveFileDialog` — new save-dialog support in FileDialog.{h,cpp},
        size estimate line, guarded Start button).
  - [x] Remote: cfg broadcasts `pathKeys`/`pathDuration`/`pathPlaying`;
        `path_play`/`path_stop` cmds; CameraTab "Camera Path" card with
        Play/Stop (or an author-on-PC hint when <2 keys). Export stays
        PC-only. Streaming `wantFrame()` suspended while an export runs
        (stale backbuffer + encoder contention).
  - [x] Toast on completion with a [Show] button (Explorer /select reveal —
        `Toast.revealPath`), MessageBeep, error/cancel toasts.
  - [x] Bonus: `--export-video <out.mp4>` smoke hook (same spirit as
        `--convert`): synths a 3-key 120° orbit path over the loaded cloud,
        drives the exact Export-dialog path, quits when the export ends.

**Risks (from scout):** render loop is vsync-tied — the export loop must run
its own pump (don't wait for swap; render FBO-only, no SwapWindow needed per
frame — keep processing SDL events so the app stays responsive). CPU NV12
conversion at 4K/60 is heavy (~500 Mpix/s) — acceptable offline; GPU compute
conversion is an optimisation, not v1. COM: SinkWriter created on the main
thread — MFStartup is currently done on encThread; VideoExporter must
MFStartup/Shutdown itself (ref-counted, safe). Streaming while exporting:
disable `wantFrame()` during export (encoder contention + wrong camera).

**Acceptance:** ✔ 3-keyframe path over a real scan (Tikal-13, 12.4M pts)
exports a 1080p30 MP4 via the `--export-video` hook: 121 frames, exit 0,
ffprobe-verified `h264 High 1920x1080 30/1 nb_frames=121` (standard MP4 —
plays in WMP/phone players); ✔ cancel path implemented (Cancel button + Esc →
`abort()` deletes the partial file) — code-verified, not yet exercised live;
✔ path persists per cloud (campaths.txt, loaded at startup); ✔ preview and
export share the same `CamPath::sample` + `renderPass` (identical framing by
construction; export additionally waits for full LOD settle).

---

## 4. Cross-section / slice export (DXF + image)  — ~1.5 weeks

**Goal:** export what the clip box shows: (a) top/front projected 2D slice as
DXF polyline-point layers CAD users can reference, (b) high-res PNG of the
slice, (c) raw points as CSV/XYZ. Clipping planes already exist — this adds
CPU-side extraction + writers.

**Verified starting points**
- Clip state: `clipMin/clipMax` `main.cpp` ~480, Properties Clip UI
  ~2132–2151, shader discard `point.frag` 48–52 (GPU-only — export needs its
  own CPU pass, confirmed no readback path exists).
- Point access: `OctreeStore::pickPoint` ~278–330 = the precedent DFS
  traversal with on-the-fly `PackedPoint` unpack (quantization scale/offset,
  lines ~314–316); `readNodeInto` ~198–222 already handles zstd/uncompressed
  payloads.
- No DXF code anywhere (confirmed).

**Plan**
- [x] `OctreeStore::forEachPointInBox(AABB box, int maxDepth, callback)` —
      new bulk query beside pickPoint: DFS, skip nodes whose cube misses the
      box, `readNodeInto` + unpack + box test per point. Depth cap = LOD
      control for export density (UI: "export density" slider mapping to
      depth). Runs on a worker thread (Jobs-panel job — exports can be GBs).
- [x] `src/io/DxfWriter.{h,cpp}` in pfcore (no GL deps — CMake conventions
      hold): minimal ASCII DXF R12 — `POINT` entities on layers, optional
      slice outline as R12 `POLYLINE`. R12 chosen: universally importable, no
      lib dependency (own writer ≈ 200 lines, same spirit as own PLY parser).
- [x] Slice semantics v1: thin-box slice = current clip box; export projects
      points onto the dominant (thinnest) axis plane → 2D DXF (x,y of the
      other two axes, world coordinates — CAD wants real coords, NOT centred
      space: add cube-centre offset back on export).
- [x] Image export: reuse offscreen FBO path — render with clip applied,
      ortho camera aligned to slice normal, export-sized FBO → `encodePNG`
      (all exists; ~day).
- [x] CSV/XYZ: trivial writer alongside DXF (x y z r g b intensity class).
- [x] UI: Properties > Clip gains "Export slice..." → dialog: format
      (DXF/PNG/CSV), density/depth, output file. Job + toast. Remote cmd
      optional later.

**Risks:** scattered node reads on spinning disks are slow — DFS order is
already file-order-ish (chunk subtrees contiguous); acceptable. Dense clips
can be huge — show live point-count estimate (sum node pointCounts
intersecting box) before export and warn >50M. DXF POINT flood: >1M points
chokes AutoCAD — cap + warn, encourage depth cap.

**Acceptance:** ✔ `pfview` static build clean; ✔ `pftest` extended with exact
boxed-query coverage and CTest `octree_roundtrip` passes; ✔ real model
`C:\UnrealProject\model\PointForgeCache_direct` exported DXF/CSV/PNG through
the startup smoke hook; ✔ DXF R12 header/EOF valid and `POINT` count matches
CSV rows (**871,420**); ✔ CSV world-coordinate range is inside the expected
thin slice (`z=314.847..318.784`); ✔ PNG is valid 1920×2160 and visually
shows the clipped slice.

---

## 5. Annotations from phone  — ~1.5 weeks

**Goal:** tap the remote video → 3D labelled pin at that point on the PC,
rendered into the stream, listed on the phone, persisted per cloud.

**Verified starting points** (this is tap-to-measure generalised)
- Tap → world: `measure_pick` cmd (`main.cpp` ~1251–1259), normalized coords
  → `cam.screenRay` → `store.pickPoint` (~1505–1516); webremote
  `toVideoNormalized` `FlyTab.tsx` 132–151 + `getNaturalSize`
  `VideoLayer.tsx` 39–45.
- In-stream overlay: measure polyline drawn as real GL lines inside
  `renderPass` (~1452–1474, `kLineVertSrc`/`kLineFragSrc` in
  `EmbeddedShaders.h` 168–183) — pins rendered the same way ARE in the video.
- Sync: `measurePts` broadcast in cfg (RemoteServer.h ~45) — same pattern for
  annotations. Persistence: bookmarks TSV in AppData (~718–744) — same
  pattern, but see risk note.
- ImGui labels are PC-only (confirmed) — stream-visible labels need GL.

**Plan**
- [x] `struct Annotation { glm::dvec3 pos; std::string label; float
      color[3]; }` + `std::vector<Annotation>` in main.cpp; persisted
      per-cloud as **JSON** (`annotations.json` beside the octree-keyed
      AppData files — JSON not TSV: labels are free text; include a
      `"version":1`).
- [x] Protocol: cmd `anno_pick{x,y}` (same shape as measure_pick) → server
      picks point, adds annotation with auto label "Pin N", broadcasts; cmd
      `anno_label` with `v=index,text=label` renames; cmd `anno_del{i}`.
      cfg gains `annotations: [{p:[x,y,z], label}]`.
- [x] Viewport rendering: GL point sprite (distinct colour) + leader line in
      `renderPass`, so pins are visible in the phone video stream and captures.
      Full text labels are PC ImGui overlay + phone/PC lists for v1; GL text
      atlas remains a polish follow-up.
- [x] Webremote: Tools tab "Annotations" card — mode toggle (tap = annotate
      vs measure vs fly), list with rename (inline input) + delete + "goto"
      (optional: fly camera to pin via cmd).
- [x] PC UI: Properties section list (rename/delete/goto), annotations
      included in screenshot + video export automatically (they're in the
      render pass).

**Risks:** tap mapping brittleness across JPEG/WebRTC letterboxing (scout
flagged `toVideoNormalized` assumptions) — mitigation: server echoes applied
pick back (`anno_added` with projected screen pos) so the phone can flash a
confirmation marker; `pickPoint` tolerance already screen-space-scaled.
GL_PROGRAM_POINT_SIZE global-state fragility (comment at ~1467) — keep the
same convention. Depth-readback picking NOT used (octree pick is the verified
path).

**Implemented / verified (2026-07-07):** `npm run build`; `cmake --build
build-static --config Release --target pfview`; `ctest --test-dir build-static
-C Release --output-on-failure`. Build output: `ViitorXPCViewer_v1023.exe`.
Manual phone/device smoke still recommended for the stream-label polish path,
but compile/test coverage for protocol, persistence wiring, and embedded web
bundle is green.

**Acceptance:** tap on phone video drops a pin visible in BOTH the PC
viewport and the phone video stream; rename from phone persists across viewer
restart; delete syncs both ways; 20+ pins don't hurt frame rate.

---

## 6. Multi-cloud scene  — ~2 weeks (do last)

**Goal:** ≥2 octrees resident simultaneously, Scene panel (left dock) listing
clouds with visibility toggles; side-by-side comparison workflows.

**Verified starting points**
- Single-cloud assumptions: global `store`/`renderer` (`main.cpp` ~370/382),
  `loadOctree` destroys state (~777–778), centred-space convention (cube
  centre = origin) baked into camera, pivot, presets, bookmarks, measure.
- **Multi-instance already proven:** Unity C API (`PointForgeC.cpp`) runs
  independent `OctreeStore` instances per handle with own worker threads +
  frustum/SSE traversal — the streaming core is multi-instance-safe today.
- Renderer: `PointRenderer.nodes_` keyed by `uint32_t` node index; GPU budget
  + LRU eviction per-renderer.
- Architecture.md §7 defers the Scene panel until multi-cloud exists (ship
  once, not twice).

**Plan**
- Week 1 — core refactor:
  - [x] `struct SceneCloud { std::unique_ptr<OctreeStore> store;
        std::unique_ptr<PointRenderer> renderer; std::string dir; bool
        visible; glm::dvec3 worldOffset; }` + `std::vector<SceneCloud>
        scene;` — one renderer per cloud (sidesteps node-index collision
        entirely; simpler than composite keys and matches the proven C API
        shape).
  - [x] World space becomes the shared frame: pick the FIRST loaded cloud's
        cube centre as scene origin; each cloud renders with
        `uCubeCenter_i - sceneOrigin` offset added to its model transform
        (positions stay float-relative to their own cube centre on the GPU —
        precision preserved; only the per-cloud offset is double→float once
        per frame, small by construction for co-located survey scans; warn if
        clouds are >100 km apart).
  - [x] Render loop: for each visible cloud → traversal + draw. GPU budget:
        keep per-renderer budgets v1 = total/N (decisions.md note; unified
        LRU heap is a follow-up).
  - [x] `loadOctree` → `addCloud` (File > Open adds; new "Close cloud" in
        Scene panel; "Open" with none loaded behaves exactly as today —
        single-cloud UX unchanged).
  - [x] Camera/pivot/frame-all: frame-all unions all visible cloud AABBs
        (in scene space); presets likewise; bookmarks/measure/clip stay
        scene-space (bookmark file keying: keep per-dir for single cloud;
        multi-cloud sessions key bookmarks by first cloud — document).
- Week 2 — Scene panel + integration:
  - [x] Left "Scene" dock (docked via DockBuilder left split, Window menu
        toggle, Reset Layout aware): cloud rows — visibility checkbox,
        selectable name (sets the ACTIVE cloud, which tools/Properties
        follow), point count, Close button, "+" add; empty state offers
        "Open Cloud...". Per-cloud point-size scale deferred (follow-up).
  - [x] Convert-job completion "Load" adds to scene instead of replacing
        (prompt: replace / add).
  - [x] Remote cfg: `clouds:[{name,pts,visible}]` (RemoteConfig::Cloud) +
        `cloud_vis` cmd (`v=[index,on,0]`, bounds-checked, prompt cfg
        republish); webremote Tools tab "Scene" card (Toggle per cloud,
        shown only when 2+ clouds; load/close stay PC-side). Viewer-role
        clients can't toggle — cmd gate at RemoteServer.cpp:748 covers it.
  - [x] Stats/status bar: total GPU MB + points across clouds (audit fixed:
        the aggregation loop existed but its results were unused — and the
        committed loop didn't compile, see decisions.md).

**Risks:** biggest-blast-radius change — schedule after 1–5 land.
Streaming worker threads scale linearly (N clouds = N workers; cap N at ~4 in
v1, log warning beyond). Measure/clip semantics across clouds: clip box is
scene-global (applies to all clouds) — v1 keeps that, per-cloud clip is
follow-up. EDL/depth: unchanged (single FBO, all clouds draw into it).

**Acceptance:** load two real scans side by side; fly between them; toggle
visibility; frame-all frames both; measure a distance ACROSS clouds; GPU
budget respected (no VRAM blowup); single-cloud workflows byte-identical UX.
Status: code-complete — render/pick/frame-all iterate all visible clouds,
budget split total/N, single-cloud smoke green (`--export-video` +
`--export-slice` on Tikal-13, CTest pass). A live two-cloud side-by-side
session (the full acceptance walk) has NOT been run yet — no second
converted real scan was staged; do that walk before the PR merges.
Audit note (2026-07-07): the backend commit `1d68eaf` shipped non-compiling
status-bar code and five scene-space bugs (slice box / annotation goto /
overlay projection / CSV precision) — all fixed; see decisions.md.

---

## Cross-cutting notes

- **Build:** every feature lands behind the existing build chain (VERSION
  auto-bump, exe rename, EmbeddedWeb regen). Webremote changes (features 2,
  5, 6) always require the exe rebuild.
- **pfcore purity:** DxfWriter + indexer threading live in pfcore — no
  SDL/GL/ImGui (CMake enforces). VideoExporter/CamPath are viewer-only.
- **Testing:** still no test suite. Feature 1 is the forcing function —
  add the long-suggested synthetic `.xyz` generator + octree round-trip test
  to validate the parallel indexer against the sequential one
  (byte-comparable point counts + spot-checked node payloads).
- **Docs loop per AGENTS.md:** on each task status change here, update
  `tasks.md`; major choices → `decisions.md`; session end → `ai_handoff.md`
  (modified files + next task).
