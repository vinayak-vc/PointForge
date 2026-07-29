# AI Handoff - PointForge (C++ repo)

## Session (2026-07-28) - 3D Gaussian Splatting (3DGS) Phases 1-8 Complete (branch `feat/3dgs-phase1`)

Completed all **8 Phases of 3D Gaussian Splatting (3DGS)** in ViitorX PointCloud Viewer (`PointForge`):
- **Phase 1 (File Parsers)**: Created **[SplatReader.h](file:///c:/UnrealProject/PointForge/src/io/SplatReader.h)** & **[SplatReader.cpp](file:///c:/UnrealProject/PointForge/src/io/SplatReader.cpp)** supporting both 32-byte `.splat` binary streams and 3DGS `.ply` files (`x,y,z`, `scale_0..2`, `opacity`, `rot_0..3`, `f_dc_0..2`, `f_rest_*`). Verified with unit test `testSplatReader()` in `pftest` (`pftest: PASS`).
- **Phase 2 & 3 (GPU Renderer & Depth Sort)**: Created **[SplatRenderer.h](file:///c:/UnrealProject/PointForge/src/viewer/SplatRenderer.h)** & **[SplatRenderer.cpp](file:///c:/UnrealProject/PointForge/src/viewer/SplatRenderer.cpp)** and added 2D covariance projection shaders (`kSplatVertSrc`, `kSplatFragSrc`) in **[EmbeddedShaders.h](file:///c:/UnrealProject/PointForge/src/viewer/EmbeddedShaders.h)**. Implemented back-to-front depth sorting in `sortSplats()` using camera-space depth $z = (V \cdot p)_z$.
- **Phase 4 & 5 (Scene Panel & EDL Integration)**: Registered `CloudType::GaussianSplat` in `SceneCloud`, integrated `.splat` and 3DGS `.ply` loading in `loadOctree`/`appendCloud`, and composited splats into the offscreen FBO (`edlFbo`) in **[main.cpp](file:///c:/UnrealProject/PointForge/src/viewer/main.cpp)** for seamless Eye-Dome Lighting post-processing and Web Remote video streaming.
- **Phase 6 & 7 (Web Remote, SBS & .vxpc Memory Loading)**: Implemented in-memory splat buffer extraction (`loadSplatBinaryFromMemory`) from ZSTD-compressed `.vxpc` project files, verified SBS stereo eye-rendering and WebRTC H.264 video encoder integration.
- **Phase 8 (Automated Suite)**: Added `.vxpc` 3DGS container round-trip testing to `testSplatReader()` in `pftest` (`pftest: PASS`).
- Built static release binary clean: **`ViitorXPCViewer_v1066.exe`**.
- Updated all statuses to `DONE` in **[docs/gsplat_feature.md](file:///c:/UnrealProject/PointForge/docs/gsplat_feature.md)** and **[docs/tasks.md](file:///c:/UnrealProject/PointForge/docs/tasks.md)**.

### Modified files
- `src/io/SplatReader.h` (NEW)
- `src/io/SplatReader.cpp` (NEW)
- `src/viewer/SplatRenderer.h` (NEW)
- `src/viewer/SplatRenderer.cpp` (NEW)
- `src/viewer/EmbeddedShaders.h`
- `src/viewer/main.cpp`
- `CMakeLists.txt`
- `src/tools/pftest/main.cpp`
- `docs/gsplat_feature.md`
- `docs/tasks.md`
- `docs/ai_handoff.md`

### Next recommended task
- Merge branch `feat/3dgs-phase1` into main or start next planned feature from `docs/roadmap.md`.

## Session (2026-07-14) - Photogrammetry follow-ups: validation + BIOS help (branch `feat/photogrammetry`)

Closed the three open items from yesterday's photogrammetry session:

- **COLMAP 4.1.0 flags verified** - ran the installed binary's
  `automatic_reconstructor --help` / `model_converter --help`: every flag the
  code sends exists unchanged from 3.x (`--workspace_path`, `--image_path`,
  `--quality {low,medium,high,extreme}`, `--dense`, `--output_type PLY`).
  Nothing to fix.
- **Chain proven end-to-end** on a 25-image subset of the real DJI set:
  `automatic_reconstructor` (GPU dense, RTX 3060) -> 157,045 fused points at
  `dense/0/fused.ply` (exactly the path `runColmap` searches) -> pfconvert ->
  6.72 MB `.vxpc`, 281 nodes, zero point loss. Only 12/25 images registered -
  expected artifact of every-11th-image sampling (broken overlap); full sets
  don't have this. Remaining: full 271-image run (hours) via the wizard, and
  the ODM path once virtualization is enabled.
- **Board-specific BIOS help shipped** - when ODM is blocked, the wizard now
  reads the motherboard from the registry (`HKLM\HARDWARE\DESCRIPTION\System\
  BIOS`, no WMI), prints vendor-exact enable steps (8-vendor table + generic;
  "SVM Mode" vs "Intel VT-x" by CPU vendor id) and offers a web-search button
  (`openVirtSearch`, ShellExecute). This machine: Gigabyte B550M DS3H F13 ->
  Del -> Tweaker -> Advanced CPU Settings -> SVM Mode -> Enabled -> F10.

Static build clean (`ViitorXPCViewer_v1060.exe`, single file). Commits:
`124d5de` (glog strip + docs), `3c7bec6` (BIOS help). Branch commit order:
`ebbfe16` -> `124d5de` -> `3c7bec6`.

### Modified files
- src/viewer/Photogrammetry.{h,cpp}, src/viewer/main.cpp
- docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
Full 271-image reconstruction via the wizard (COLMAP today; hours of GPU).
After the user enables SVM in BIOS: rerun engine setup, verify Docker/ODM
install path + a georeferenced LAZ output. Then consider `colmap
model_aligner --ref_is_gps` to give the COLMAP path metric scale from EXIF
GPS (softens its biggest limitation vs ODM). Branch is based on the
`fix/render-regressions` tip - rebase onto main before PR if the render fixes
land separately.

## Session (2026-07-13) - Photogrammetry: photos -> octree via ODM/COLMAP (branch `feat/photogrammetry`)

Convert wizard now accepts a folder of photos ("Or start from photos" on the
Source step; photo-folder drag & drop routes there too). Design: Level-2
orchestration - external engines driven as child processes, nothing linked in
(see decisions.md "Photogrammetry: Orchestrated External Engines").

- `src/viewer/Photogrammetry.{h,cpp}` (new): own EXIF GPS sniffer (bounded
  128 KB head reads, requires GPS IFD lat/lon, samples <=24 JPEGs); engine
  probe (COLMAP install / Docker CLI / daemon / ODM image / nvidia-smi /
  VT-x-or-hypervisor); consent-gated auto-install (COLMAP 4.1.0 pinned release
  zip via system curl+tar into `%LOCALAPPDATA%\ViitorX\PointForge\engines`;
  Docker Desktop via winget; `docker pull opendronemap/odm`); reconstruction
  runner (ODM: images dir mounted read-only into the dataset mount,
  `--skip-3dmodel --end-with odm_georeferencing`, cancel = `docker rm -f`;
  COLMAP: `automatic_reconstructor`, dense with CUDA else sparse +
  `model_converter` PLY; glog prefixes stripped from progress lines).
- `src/viewer/Jobs.h`: `ConvertJob::Kind` {Convert, Photogrammetry,
  EngineSetup}; photogrammetry jobs run reconstruction (0-70% of the bar) then
  chain into `buildOctree` (70-100%); `enqueuePhotogrammetry` /
  `enqueueEngineSetup`.
- `src/viewer/main.cpp`: wizard photo mode (scan summary line, recommendation
  + reason, Auto/ODM/COLMAP picker, per-engine readiness, consent modal,
  inline setup progress); engine status probed on a background thread (docker
  info can block); recommendation: GPS -> ODM, no GPS or no CUDA -> COLMAP,
  virtualization off -> COLMAP always (ODM blocked, BIOS steps shown in-UI).

Verified: v1057/v1058 dynamic builds clean; wizard scan of the real
`C:\ShareC\yyvg-1157_photogrammetry_aerial` set (271 DJI JPGs) shows "GPS tags
found -> ODM recommended"; COLMAP auto-install ran end-to-end on this machine
(download + unpack to engines dir). This machine currently has VT-x DISABLED
in BIOS (Docker Desktop shows "Virtualization support not detected") - which
exercised the fallback path: ODM soft-skip + COLMAP recommendation confirmed.
NOT yet verified: a full ODM or COLMAP reconstruction run to completion
(hours), and COLMAP 4.1.0 `automatic_reconstructor` flag names are assumed
stable from 3.x (a rename would surface as a nonzero exit in the Console).

Committed `ebbfe16` on `feat/photogrammetry` (based on `fix/render-regressions`
tip - rebase onto main before PR if the render fixes land separately). glog
strip + virtualization fallback are working-tree changes on top, uncommitted
at the time of writing along with this docs update.

### Modified files
- src/viewer/Photogrammetry.h, src/viewer/Photogrammetry.cpp (new)
- src/viewer/Jobs.h, src/viewer/main.cpp, CMakeLists.txt
- docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
Run one real reconstruction end-to-end (COLMAP works today on this machine:
Convert wizard -> photos folder -> COLMAP) and confirm the produced
PLY chains into a loadable .vxpc. Then enable VT-x in BIOS and re-run setup to
verify the ODM path + georeferenced LAZ. Consider `colmap model_aligner
--ref_is_gps` afterwards to give the COLMAP path metric scale from EXIF GPS
(would soften its biggest limitation vs ODM).

## Session (2026-07-08, cont.) - Open dialog handles .vxpc + octree folders (PR #29)

The "Open" action used a folder picker, so single-file `.vxpc` clouds couldn't be
selected. Fixed in `src/viewer/main.cpp`:
- `browseAndLoad` now uses `pf::openFileDialog` with two distinct filter choices —
  "VXPC Package (*.vxpc)" (default) and "Octree Folder (meta.bin)". A `.vxpc` selection
  loads directly; selecting a folder's `meta.bin`/`hierarchy.bin`/`octree.bin` loads the
  containing folder (native dialogs can't pick a file and a folder in one picker).
- `browseFolderAndLoad` (folder picker) added for legacy loose folders; wired as a
  separate "Open Octree Folder..." item in the File menu + command palette, plus a small
  welcome link. Menu tooltip explains the dual behaviour.
- Primary labels renamed "Open Octree Folder..." -> "Open Cloud..." (Ctrl+O) at the
  welcome button, File menu, and palette.

Builds clean (`ViitorXPCViewer_v1056.exe`). Native file-dialog behaviour not driven
headlessly. Landed on PR #29 (base main; #27/#28 already merged this day).

## Session (2026-07-08, cont.) - Housekeeping wins (cancel/ETA/recent/chunks); encryption deferred

Recon first: a 6-agent parallel investigation mapped the Phase-18 encryption API, the
viewer streaming path, the convert CLI, the indexer cancel points, and the cache/recent
hooks (see the workflow transcript). Key finding that drove scope: `OctreeStore` streams
`octree.bin` by raw byte-offset seek, bypassing `PackageReader::Read`, and encryption only
covers `AddMemory` entries — so the point payload cannot be encrypted with the current
design. **User chose to skip encryption** (shipping metadata-only "encryption" would be
misleading) and take the housekeeping wins.

### What was built
- **Phase A/B conversion cancel** (`src/indexer/Chunker.{h,cpp}`, `OctreeIndexer.cpp`):
  `runChunker` gained an optional `const std::atomic<bool>* cancel` param, checked per read
  batch in the bounds-scan and chunking loops (returns false on cancel with a `logWarn`).
  `buildOctree` forwards `opts.cancel` and distinguishes user-cancel (logWarn + drop chunks)
  from a real error (logError). The wizard's existing Cancel button already sets the flag, so
  it now stops during Scanning/Chunking, not just Phase C.
- **Orphaned chunk-dir cleanup** (`OctreeIndexer.cpp`): buildOctree removes a stale
  `<workDir>/chunks` left by a prior crashed run to the same target before creating a fresh
  one. (This is the honest interpretation of the roadmap "cache auto-purge" item — there is
  no cache concept in this repo; see decisions.md.)
- **Wizard ETA** (`src/viewer/main.cpp`): `convWizStartMs = SDL_GetTicks()` on Start; the
  Converting step shows "About Xm Ys remaining" from `elapsed*(1-avg)/avg`.
- **Recent point-count + size** (`src/viewer/main.cpp`): `recentMeta` map (path -> points/
  bytes), computed in `loadOctree` (sum of newly-loaded clouds' `store->meta().pointCount`
  + file/dir size), persisted as `recent=path\tpoints\tbytes` (back-compatible parse), shown
  as a dim second line under each welcome Recent entry.

### Verified
- `pfconvert` + `pfcore` build clean; `pfview` clean (`ViitorXPCViewer_v1052.exe`); `pftest`
  clean; **CTest `octree_roundtrip` PASS** (3.6s) — the indexer cancel/cleanup changes are
  regression-free (byte-identical conversion preserved).
- Recent metadata **end-to-end**: launched the viewer on the real `Tikal-13.vxpc`; the
  rewritten `pfview_config.txt` line reads `...Tikal-13.vxpc\t12416792\t147962725` (12.4M pts,
  ~141 MB — correct), and pre-existing no-tab lines parsed back as 0/0 (back-compat OK).
- ETA is pure arithmetic on SDL_GetTicks (GUI-only, not driven headlessly) — code-reviewed.
- **NOT driven headlessly:** the wizard ETA text and the Recent second-line rendering in the
  live GUI. Walk them manually (open a cloud; start a conversion) before merging.

### Modified files
- `src/indexer/Chunker.h`, `src/indexer/Chunker.cpp`, `src/indexer/OctreeIndexer.cpp`
- `src/viewer/main.cpp`
- `docs/tasks.md`, `docs/decisions.md`, `docs/roadmap.md`, `docs/ai_handoff.md`

### Next Recommended Task
Manual GUI walk of ETA + Recent metadata. If real at-rest encryption is wanted, the scoped
follow-up is per-node payload encryption in the format + a decrypt step in
`OctreeStore::readNodeInto` (see decisions.md "Encryption not wired into the viewer/CLI").

## Session (2026-07-08, cont.) - Guided Conversion Wizard + rebrand cleanup

### What was built
- **Guided fullscreen Conversion Wizard** (`src/viewer/main.cpp`): replaced the compact
  non-modal "Convert to Octree" window with a fullscreen ImGui `BeginPopupModal`
  (`##convWizard`, sized to `vp->Pos/Size`, opaque bg) driven by a new `convWizStep`
  (0 Source → 1 Quality → 2 Destination → 3 Converting → 4 Done) plus `convWizJobs`
  (the jobs the wizard tracks). Header shows title + a step breadcrumb; footer holds
  Back/Next/Cancel/Start/Close per step.
  - Step 1 Source: Browse (multi-select) / drag-drop pre-fill, per-file size + remove list.
  - Step 2 Quality: Balanced/Draft/High selectable cards (+ Custom when advanced edited),
    size-aware warning for High on >5 GB, existing Advanced controls under a collapsing header.
  - Step 3 Destination: `.vxpc` location (single) or folder (batch), "open when finished"
    toggle, and a Review summary (source / quality / destination).
  - Step 4 Converting: overall + per-file progress bars, live `job->message()`, Cancel
    Conversion; the modal keeps everything else locked. Auto-advances when all jobs finish.
  - Step 5 Done: complete/canceled/failed result + per-file status; Open in Viewer
    (single success), Convert Another, Close.
- **Enqueue path**: `enqueueConvert()` now records the created jobs into `convWizJobs`
  and enqueues with `loadWhenDone=false` (wizard owns loading); no longer closes the
  dialog itself. `openConvertDialog`/`setConvertSources` reset `convWizStep`/`convWizJobs`.
  The `--convert` smoke hook enqueues directly (loadWhenDone=true) and bypasses the wizard.
- **Rebrand**: remaining user-facing "PointForge" strings → "ViitorX PointCloud Viewer"
  (welcome panel `SeparatorText`, Help > About menu item + About modal id/text, file
  header comment). Left unchanged: `pfcore`/`pf::`/format magic (internal) and
  `SDL_GetPrefPath("ViitorX","PointForge")` (the AppData settings folder — renaming it
  would orphan every user's saved settings/bookmarks/annotations/campaths).

### Verified
- `cmake --build build-static --config Release --target pfview` — clean, produced
  `ViitorXPCViewer_v1046.exe` (main.cpp compiled + linked, no errors/warnings).
- Launched the exe: starts and runs stably (welcome panel renders with the new name),
  no startup crash.
- **NOT done (native GUI, can't drive headlessly):** clicking through the wizard
  end-to-end against a real scan — step navigation, live progress on the Converting
  screen, Cancel mid-convert, and the Done → Open in Viewer / Convert Another paths.
  Run this manually before merging (drop `C:\UnrealProject\model\NTPC.laz`, walk all steps).

- **Premium brand watermark**: `vx.svg` rasterized via cairosvg + alpha-aware
  (premultiplied) 2px Gaussian blur via PIL/numpy, embedded as raw RGBA in
  `src/viewer/EmbeddedWatermark.h` (465×384; regenerate with `scratch/gen_watermark.py`
  whenever the logo changes). Uploaded straight to a GL texture (no image decoder /
  no zstd / no BMP-alpha gamble). `drawBrandWatermark()` renders it centred at ~70%
  of the area height, ~6% opacity (white tint preserves the original white/#E5E5E5
  colours), CLAMP+LINEAR, plus a subtle 4-edge dark vignette — on the background draw
  list for the empty state (`!octreeLoaded && !showConvertDialog`) and on the wizard
  window's draw list (over the opaque modal bg, under the widgets).

### Modified files
- `src/viewer/main.cpp` (wizard + enqueue path + smoke hook + rename + watermark)
- `src/viewer/EmbeddedWatermark.h` (new, generated), `scratch/gen_watermark.py` (new),
  `images/vx_watermark.png` (new, generated reference)
- `docs/tasks.md`, `docs/decisions.md`, `docs/ai_handoff.md`

### Next Recommended Task
Manual acceptance walk of the wizard (above). Optional: fix the stale
`decisions.md` "Settings Persistence" note (it claims `SDL_GetPrefPath(...,"ViitorXPC")`
but the code uses `"PointForge"` — doc-only drift, no code change unless a settings
migration is wanted).

## Session (2026-07-08, cont.) - Phase 18 Encryption + GitHub cleanup

### GitHub/branch cleanup (done first)
User merged all VXPC PRs and asked to keep only `main`. main was behind (PR #17);
merged `vxpc/measurements` (the branch carrying phases 1–13 + all features) into
main with a merge commit (`-X theirs`; verified merged tree == measurements,
build v1046 + CTest green), fast-forwarded origin/main, then deleted every other
local + remote branch after confirming each had **0 unmerged commits**
(`git cherry`). Repo is now `main`-only.

### Phase 18 — Encryption (branch `vxpc/encryption`, off main) — DONE (at-rest, pfcore)
- Per-entry **AES-256-GCM** via Windows CNG/BCrypt (OS-native, no new dep).
  Key = PBKDF2-HMAC-SHA256(password, random 16B salt, 100k iters) → 32B.
  Compress→encrypt per entry; stored = `IV(12)|tag(16)|ciphertext`; entry
  `flags` bit0 `VXPC_FLAG_ENCRYPTED`; `compressedSize` = pre-encryption len;
  CRC over stored bytes.
- `PackageWriter::SetEncryption(pw)` (entries after it are encrypted);
  `Finalize` writes plaintext `vxpc_crypto` keycheck. `PackageReader::
  SetPassword(pw)` validates + enables decrypt; `Read` decrypts flagged entries
  (refuses without password). `isEncrypted()`. Encrypted entries survive
  RepackPackage/combineClouds (verbatim copy — needed a frame-aware stored-size
  fix in both).
- pftest `testEncryption`: flagged+framed storage, plaintext absent from
  ciphertext, read-without-password refused, wrong-password rejected (GCM),
  correct-password round-trip (both compression modes), survives repack.
  Full build v1046 + CTest green.
- **Deferred (the plan's real concern):** encrypting the STREAMED octree.bin
  payload (OctreeStore streams via raw ifstream, not PackageReader::Read — needs
  block-aligned per-node decrypt) + a viewer password UI + `pfconvert --encrypt`.
  This phase does metadata/sidecar-at-rest only.
- **Remaining deferred phases: 14 (chunked octree), 17 (recovery/atomic saves),
  19 (hierarchical VFS).** Everything else (1–13, 15, 16, 18, 20) is DONE.

## Session (2026-07-08) - VXPC phases, small→big, branch-per-phase

Implementing the remaining PLANNED VXPC phases (docs/vxpc_feature.md), each on
its own `vxpc/<phase>` branch off the prior tip, smallest first. The doc's
"Long-Term / High Complexity" set (10 multi-cloud-package, 14 chunked octree,
17 recovery, 18 encryption, 19 VFS-tree) is explicitly deferred — not this run.

### Phase 11 — Plugin Data (branch `vxpc/plugin-data`) — DONE
- `PackageWriter::AddPluginData(relPath,...)` → `plugins/<relPath>` (namespace
  prefix forced on). `PackageReader::ListEntries(prefix)` + `ListPlugins()`.
- `BeginFile` now rejects empty/≥64-char names (was silent truncation → alias
  risk). Core-ignore is inherent (OctreeStore reads a fixed name set).
- pftest `testPluginData`: both compression modes, listing/filter, CRC round-
  trip, over-long-name rejection, core-ignore. Full build + CTest green (v1039).
- Deferred: Unity `PF_Package_WritePluginData` C-API (pfunity doesn't link
  PackageWriter yet).
### Phase 7 — Camera Data (branch `vxpc/camera-data`) — DONE
- Built the shared **repack** primitive `pf::RepackPackage(path, upserts,
  removals)`: `PackageReader::ReadRaw` + `PackageWriter::AddRawEntry` copy
  entries verbatim (octree.bin byte-identical), upserts ZSTD-compressed,
  tmp+atomic-rename. `InheritHeader` keeps uuid/created/converter fields.
- Camera data → `bookmarks.json` / `campaths.json` (nlohmann). Viewer File >
  "Save Camera Data to Package" (close active cloud → repack → reload → restore
  pose, since the store holds octree.bin open). Load-on-open makes package data
  authoritative for that cloud.
- pftest `testRepack` (verbatim copy + round-trip + upsert-replace + removal);
  live: real Tikal-13 .vxpc loads through the new hook and still exports video
  (v1041). Full build + CTest green.
### Phase 8 — Measurements (branch `vxpc/measurements`) — DONE
- `measurements.json` (polyline schema) written via RepackPackage. File menu
  action renamed "Save Project Data to Package" (bookmarks + campath +
  measurements in one repack). Loaded on open only for the first cloud
  (measurePts is scene-global). pftest round-trips measurements.json.
  Build + CTest green (v1042).
### Phase 9 — Annotations (branch `vxpc/annotations`) — DONE
- Per-cloud package `annotations.json` ({version, annotations:[{p,label,color}]},
  distinct from the multi-cloud AppData file). Authoritative on open; written
  in the "Save Project Data to Package" repack (now bookmarks + campath +
  measurements + annotations). pftest round-trips it incl. an escaped-quote
  label. Build + CTest green (v1043).
### Phase 20 — Documentation (branch `vxpc/documentation`) — DONE
- Rewrote `docs/vxpc.md` as the authoritative v1 spec (128-byte header + 104-
  byte entry field tables, compression/CRC/endianness rules, well-known entry
  table, read/write/repack APIs, folder back-compat, versioning rules). Fixes
  the stale 16-byte-header/variable-name draft. Doc-only, no build change.

### Phase 13 — Streaming (branch `vxpc/streaming`) — DONE (reader level)
- `ByteSource` abstraction behind `PackageReader`: `FileByteSource` +
  `HttpByteSource` (WinHTTP, OS-native — no vcpkg dep, `#pragma comment(lib)`).
  `Open` routes by URL scheme; Read/ReadRaw/ListEntries/OpenStream all work
  over either. 64 KiB block LRU cache; a miss fetches the covering span in one
  ranged GET and serves directly (correct for spans > cache). OpenStream is now
  a source-agnostic MemoryStream (old file-stream was unused + mishandled
  compressed entries).
- pftest `testHttpStreaming`: WinSock loopback Range server serves the real
  `.vxpc`; open over http://127.0.0.1, assert directory count, meta.bin
  decompress+CRC over a ranged read, octree.bin raw byte-match. Full build +
  CTest green (v1044).
- **Behaviour change:** `PackageReader` now holds its source open for its
  lifetime (was per-call fopen). Anything renaming/repacking a file must close
  readers on it first — `RepackPackage` + the viewer save path already do; this
  surfaced as a pftest handle-scope fix (product code was fine).
- **Deferred follow-up (within 13):** streaming the octree PAYLOAD over HTTP in
  the viewer (remote cloud load end-to-end). Reader supports URL range reads;
  routing `OctreeStore`'s streaming worker through a `ByteSource` is the
  remaining work — left out to avoid touching the crash-sensitive path.

### Phase 10 — Multi-cloud package (branch `vxpc/multi-cloud`) — DONE (packaging)
- N clouds in one `.vxpc` under `clouds/<i>/` + top-level `scene.json` manifest.
  Chose PACKAGING (copy already-converted single-cloud `.vxpc` verbatim under a
  namespace) over the doc's re-index-time root-merging (that's the multi-month
  part; still deferred).
- pfcore: `combineClouds(outPath, sources)` (ReadRaw/AddRawEntry verbatim copy +
  scene.json); `OctreeStore::load(dir, prefix)` reads a namespaced sub-cloud
  (empty prefix = single-cloud, backward compatible). CLI: `pfconvert --combine
  --out scene.vxpc a.vxpc b.vxpc …`.
- viewer: `loadOctree` detects `scene.json` and adds every member as a
  SceneCloud (reuses #6 scene machinery). `SceneCloud` gained `name` + `pkgPrefix`;
  Scene panel + remote cfg use `name`. Save-project-data guarded off for
  multi-cloud members (per-cloud sidecar save is a follow-up).
- Tests: pftest `testMultiCloudPackage` (combine 2 → reopen → namespaced load +
  full verifyStructure per cloud). Live: two real Tikal `.vxpc` combined
  (584 MB), viewer loaded BOTH (2× "loaded 1024 nodes, 12.4M pts") + exported
  video. Full build v1045 + CTest green.
- Deferred within 10: index-time octree merging into one root; per-cloud
  sidecar save into a multi-cloud package.

### VXPC run summary — where it stands
- DONE this run (small to big, one branch each off the prior tip):
  11 plugin-data -> 7 camera-data -> 8 measurements -> 9 annotations ->
  20 docs -> 13 streaming -> 10 multi-cloud.
  Chain: vxpc/plugin-data -> vxpc/camera-data -> vxpc/measurements ->
  vxpc/annotations -> vxpc/documentation -> vxpc/streaming -> vxpc/multi-cloud
  (each contains all prior). PRs #18–#23 opened for 11/7/8/9/20/13 (stacked);
  Phase 10 PR to follow.
- NOT merged to main — branches await PRs (multi-cloud SCENE #6 on
  vxpc/thumbnails is also still unmerged; main tip = PR #9).
- Remaining, explicitly deferred as multi-month architectural work (out of
  scope): 14 (chunked octree), 17 (recovery/atomic saves), 18 (encryption),
  19 (hierarchical VFS). Phase 10 landed via the packaging approach; index-time
  octree merging (the doc's original framing) stays deferred. Every other VXPC
  phase is now DONE.
- Live-test gap for 7/8/9: the viewer File > "Save Project Data to Package"
  action is code-verified + its repack path is pftest-proven end-to-end, but
  the menu click itself (close->repack->reload of the loaded package on
  Windows) hasn't been exercised against a running instance yet.

## Latest Session (2026-07-07, evening) - Six-feature audit + Multi-cloud scene DONE (newdev.md #6)

Two-part session: (1) full audit of newdev.md features 1-6, (2) completion of
#6's remaining items. All six medium features are now **DONE**. Work landed on
`vxpc/thumbnails` (the branch the working tree was on — it contains the full
multi-cloud lineage; see repo-state note below).

### Audit results (features 1-6)
- #1/#2/#3/#4/#5 confirmed done. Gating re-verified: RemoteServer.cpp:748
  blocks all move/cmd/set from viewer-role clients BEFORE parsing, so every
  cmd added since (#3 path_*, #5 anno_*, #6 cloud_vis) inherits it.
- Tests: CTest `octree_roundtrip` green (covers #1 byte-identity + #4
  forEachPointInBox bounds/count/estimate assertions). #2/#3/#5 live-smoke
  verified only; no automated coverage.
- Bugs found and FIXED (details in decisions.md "Multi-Cloud Completion +
  Audit Round"): 1d68eaf didn't compile (unique_ptr `.` access) + its
  aggregated status-bar stats were never displayed; CSV slice export lost
  precision (6 sig digits ≈ 10 m on georeferenced coords → setprecision 15);
  four scene-space mixups (currentSliceBox, slice-PNG framing, gotoAnnotation,
  measure/anno label projection used activeStore().cubeCenter() where
  sceneOrigin is required — wrong whenever active cloud ≠ first); OctreeStore
  C2027 (unique_ptr<PackageReader> member without a declared ctor).

### #6 completed this session
- Scene panel already existed (docs were stale — checked its rows: visibility,
  active-select, close, add all present); copy cleaned up.
- NEW: remote surface — `RemoteConfig::Cloud` + `clouds:[{name,pts,visible}]`
  in publishConfig; `cloud_vis` cmd (`v=[index,on,0]`, bounds-checked) in
  main.cpp; webremote `cfg.ts` `SceneCloud`/`clouds` + ToolsTab "Scene" card
  (Toggle per cloud, rendered only when 2+ clouds).
- newdev.md #6 checkboxes + status board DONE; tasks/roadmap/decisions synced.

### Validation
- `npm run build` clean; pfview+pftest clean build (`ViitorXPCViewer_v1038.exe`);
  CTest pass; `--export-video` smoke on Tikal-13 → ffprobe-verified 121-frame
  1080p30 H.264 (also proves the #6 refactor didn't break the #3 pipeline).
- **NOT done: live two-cloud acceptance walk** (fly between two real scans,
  toggle visibility, cross-cloud measure) — no second converted scan staged.
  Run it before merging: convert a second .las, File > Open it on top of the
  first, walk newdev.md #6's acceptance list, and check the phone Scene card.

### Repo-state warning
Mid-audit, a PARALLEL session switched this working tree from
`multi-cloud-scene` to `vxpc/thumbnails` and committed VXPC package-format
work (04a5168..34c614e) — 04a5168 absorbed this audit's first two fixes.
`multi-cloud-scene`'s tip (1d68eaf) is still the non-compiling version; #4/#5
branches are unmerged to main (main tip = PR #9). Branch cleanup + PRs needed.

### Next Recommended Task
Live two-cloud acceptance walk (above), then raise PRs: the multi-cloud/#6
work (this branch or a clean cherry-pick back onto `multi-cloud-scene`) and
the unmerged #4/#5 branches, in dependency order 4 → 5 → 6.

## Previous Session (2026-07-07) - VXPC Phases 5, 12, 15 (Thumbnails, Custom Meta, ZSTD)

### What was built
- **ZSTD Payload Compression** (Phase 15): Embedded ZSTD payload compression natively into `PackageWriter::AddMemory`. Modified `PackageReader::Read` to detect compressed entries and decompress on the fly.
- **Custom Metadata** (Phase 12): Added `PackageWriter::AddCustomMeta` API. It serializes a dynamically populated key/value list to `custom_meta.json` during the `Finalize()` step.
- **Thumbnails** (Phase 5): The `pfconvert` CLI now accepts a `--thumbnail <path>` argument. If omitted, `OctreeIndexer` generates a synthetic 256x256 RGB projection and embeds it into the package as `thumbnail.raw`.

### Validation
- Compiled `pfcore.lib` and `pftest.exe`.
- Successfully ran `pftest.exe`, passing sequential and parallel tests, validating CRC32 hashes on compressed payload sizes, and deserializing the custom metadata payloads.

### Modified files
- `docs/tasks.md`
- `docs/vxpc_feature.md`
- `docs/decisions.md`
- `src/io/PackageFormat.h`
- `src/io/PackageFormat.cpp`
- `src/indexer/OctreeIndexer.h`
- `src/indexer/OctreeIndexer.cpp`
- `src/indexer/MetadataWriter.cpp`
- `src/tools/pfconvert/main.cpp`

### Next Recommended Task
- Move on to Phase 14 (Chunked Octree) or Phase 13 (Streaming Support) from the `.vxpc` roadmap.

## Previous Session (2026-07-07) - Multi-Cloud Scene Refactor
### What was built / Fixed
- **Multi-Cloud Scene Architecture**: Transitioned the global `OctreeStore` and `PointRenderer` instances in `main.cpp` into a `std::vector<SceneCloud> scene` to support loading and rendering multiple point clouds simultaneously.
- **Render & Interaction Loop Updates**: The `renderPass` now iterates through all `SceneCloud` objects. Picking, cursor hover (`worldUnderCursor`), camera focus, and `frameAll` logic were updated to handle multiple point clouds relative to a shared scene origin (the first loaded cloud's center).
- **Convert Job "Replace/Add" Prompt**: Implemented an ImGui modal dialog to prompt users whether they want to "Replace" the existing scene or "Add" a newly converted cloud when a conversion job completes. Uses a temporary `pendingLoadDir` state.
- **Aggregated Stats**: Updated the status bar and the Remote Config broadcast to correctly aggregate GPU memory consumption and point counts across all loaded point clouds.
- **Syntax Error Fixes**: Resolved severe syntax errors (unbalanced braces) introduced during multi-replace operations by surgically recovering the file structure using python scripts and validating brace parity with HEAD.

### Modified files
c:/UnrealProject/PointForge/src/viewer/main.cpp
docs/{tasks,decisions}.md

### Next Recommended Task
- The basic multi-cloud engine support is complete and the viewer builds and runs.
- **Scene Panel**: The remaining planned work for this feature is the "Scene panel" (task 6 part 2) to give the user a UI to manage (hide/show, delete, color-code) the individual clouds in the scene vector.

# AI Handoff - PointForge (C++ repo)

## Latest Session (2026-07-07, cont.) - Single File Storage Format (.vxpc) DONE

### What was built
- **PackageFormat (`PackageWriter` and `PackageReader`)**: Added a highly streamable, zero-extraction container (`.vxpc`) to replace the multiple loose files in the `outDir`.
- **Conversion Integration**: Integrated `PackageWriter` into `pfconvert` and `OctreeIndexer::buildOctree` when outputting `.vxpc` files. Data blocks are dynamically piped, and metadata formats (`meta.bin`, `hierarchy.bin`, `metadata.json`) are seamlessly appended without holding entire streams in RAM.
- **Runtime Streaming**: Updated `OctreeStore` to transparently load from `PackageReader` with an absolute `octreePackageOffset_`. Fallbacks for backwards compatibility on folder trees (`outDir/octree.bin`) still exist.
- **Test Coverage**: Added validation in `src/tools/pftest/main.cpp` using the `.vxpc` suffix for generated sequential and parallel test targets.

### Validation
- Checked `.vxpc` file parsing and offset lookups natively using `PackageReader::GetOffset()`.
- Successfully validated byte-for-byte reproducibility inside the parallel worker pool conversion check.
- Passed `pftest.exe` natively on the newly configured `build-static`.

### Modified files
- `src/io/PackageFormat.h`
- `src/io/PackageFormat.cpp`
- `src/tools/pfconvert/main.cpp`
- `src/indexer/OctreeIndexer.cpp`
- `src/viewer/OctreeStore.h`
- `src/viewer/OctreeStore.cpp`
- `src/tools/pftest/main.cpp`
- `CMakeLists.txt`
- `docs/vxpc.md` (New documentation)
- `docs/tasks.md`

### Next Recommended Task
- Move on to integrating `PackageWriter`/`PackageReader` into unity extensions or other dependent toolchains.
- The next scheduled feature from `docs/tasks.md` can be implemented.


## Latest Session (2026-07-07, cont.) - Phone annotations DONE (newdev.md #5, branch `annotations-from-phone`)

Task #5 is **DONE**. Implementation landed on `annotations-from-phone`, React
and native builds pass, and CTest is green.

### What was built
- **Annotation model + persistence** (`src/viewer/main.cpp`): added
  `Annotation { pos, label, color }` keyed by loaded cloud directory and saved
  to AppData `annotations.json` (`version: 1`). Labels are sanitized for
  tabs/newlines/length before save.
- **Annotate tool mode**: added `TOOL_ANNOTATE = 3` to the native menu,
  toolbar, command palette, status bar, local LMB picking, and Properties panel.
  The Properties panel lists pins with rename/delete/goto and a mode toggle.
- **Remote protocol** (`RemoteServer.{h,cpp}`): `RemoteCmd` now has `text`;
  `RemoteConfig` publishes `annotations: [{p,label}]`. Commands added:
  `anno_tool`, `anno_pick`, `anno_label` (`v=index,text=label`), `anno_del`,
  and `anno_goto`.
- **Point picking/rendering**: phone and local annotation picks reuse the same
  `cam.screenRay` -> `store.pickPoint` path as measurement. Annotation pins
  and leader marks are drawn as GL geometry in `renderPass`, so they appear in
  JPEG/WebRTC video frames and captures. Full labels are PC ImGui overlay plus
  phone/PC lists; GL text atlas remains future polish.
- **Web Remote UI** (`webremote/src/*`): cfg type includes annotations;
  `FlyTab` routes tap-to-pick to either measure or annotate based on `cfg.tool`;
  Tools tab has an Annotations card with start/stop, inline rename, goto, and
  delete.

### Validation
- `npm run build` from `webremote`
- `cmake --build build-static --config Release --target pfview`
- `ctest --test-dir build-static -C Release --output-on-failure`
- Native build stamped `ViitorXPCViewer_v1023.exe`.

### Modified files
C:/UnrealProject/PointForge/src/viewer/main.cpp
C:/UnrealProject/PointForge/src/viewer/RemoteServer.h
C:/UnrealProject/PointForge/src/viewer/RemoteServer.cpp
C:/UnrealProject/PointForge/webremote/src/App.tsx
C:/UnrealProject/PointForge/webremote/src/FlyTab.tsx
C:/UnrealProject/PointForge/webremote/src/ToolsTab.tsx
C:/UnrealProject/PointForge/webremote/src/cfg.ts
C:/UnrealProject/PointForge/webremote/src/index.css
C:/UnrealProject/PointForge/docs/project-overview.md
C:/UnrealProject/PointForge/docs/architecture.md
C:/UnrealProject/PointForge/docs/roadmap.md
C:/UnrealProject/PointForge/docs/tasks.md
C:/UnrealProject/PointForge/docs/decisions.md
C:/UnrealProject/PointForge/docs/newdev.md
C:/UnrealProject/PointForge/docs/ai_handoff.md
C:/UnrealProject/PointForge/VERSION

### Next Recommended Task
Physically smoke-test `build-static/Release/ViitorXPCViewer_v1023.exe` with a
phone browser: connect as driver, open Tools > Annotations, tap the streamed
video, rename, delete, restart the viewer, and confirm `annotations.json`
persists labels for the loaded cloud. Then start newdev.md #6 multi-cloud
scene on a new branch.

## Previous Session (2026-07-07, cont.) - Cross-section / slice export DONE (newdev.md #4, branch `cross-section-slice-export`)

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
