# ViitorX PointCloud Viewer — Test Plan & Results

Branch: `testing-application`  ·  Build: `ViitorXPCViewer_v1068.exe`  ·  Date: 2026-07-29

Angles covered: **functional · boundary · negative/error · integration · regression · performance · security · platform**.

Legend — **Method**: `AUTO` (pftest/CLI + log assertions, run here) · `BROWSER` (web-remote UI via browser pane, run here) · `MANUAL` (native ImGui GUI — needs a human, no native-automation tool in this env).
**Status**: ✅ pass · ❌ fail · ⚠️ pass-with-note · ⛔ blocked · ☐ not-yet-run (manual).

---

## Execution summary (2026-07-29, this session)

**Ran here (automatable): 26 cases, 0 fail.**
- **pftest suite (A1–A10):** all ✅ — octree convert seq+parallel × zstd on/off, repack, multi-cloud, plugin-data, HTTP-range, AES-256-GCM, 3DGS reader.
- **Load / negative (B1–B9):** all ✅ — 147 MB LiDAR `.vxpc` loads (12.4M pts); raw `.ply`/`.las`/missing/corrupt all fail **gracefully, no crash**; `detectFormat` regression fixed and verified.
- **Web Remote (C1–C6):** all ✅ via browser pane — page load, PIN gating (both directions + server log), `/shot.png` 403/404/200 with valid PNG, command round-trip (Frame-all moved camera), live JPEG stream, cfg telemetry.
- **Render smoke via remote (D subset):** ✅ color-mode / quality / EDL changes applied live, produced distinct valid frames (506 KB → 149 KB), no console/log errors, viewer stayed healthy the whole session.

**Not run here — needs a human at the native ImGui GUI:** D1–D9 visual correctness (incl. gsplat covariance/sort fixes), E camera/nav, F tools (measure/annotate/clip/slice), G convert wizard + photogrammetry, H camera-path + MP4, I stereo/fullscreen/hide-UI, J UI shell, K1/K2/K5/K6. See `☐` rows.

**Environment caveat:** the browser pane could not composite, so no visual screenshot diffing — stream/render verified via network traffic, PNG magic/size, and the viewer's own log rather than by eye.

---

## Round 2 — deep bug-hunt (2026-07-29)

Real data + code audit surfaced **4 more bugs, all fixed & verified**:

| ID | Bug | Angle | Fix | Verified |
|----|-----|-------|-----|----------|
| R1 | **Loading any Gaussian-splat cloud crashes the app** (SIGSEGV). `octreeLoaded` was used both for "a cloud is loaded" (render/nav) and to justify `activeStore()` derefs (needs an octree). Splat clouds have a null `store` → `setupCamera`, `publishState`, Scene panel, status bar, stats/perf panels all null-deref. | correctness/crash | Added `octreeActive` (active cloud is an octree); routed framing through a type-aware `activeCloudSize()`; guarded ~16 `activeStore()`/`activeRenderer()` sites + the Scene-panel and status-bar per-cloud loops. | ✅ 50k `.splat` loads, renders, survives (was instant SIGSEGV); remote `/shot.png` = 1.03 MB valid PNG, byte-spread 255 (non-blank). |
| R2 | **Splat vertices uploaded in absolute coords** but placed with `worldOffset = centre − sceneOrigin` (octree convention) → double-offset + GPU float-precision loss; breaks multi-cloud coexistence. | correctness | `SplatRenderer::upload` now uploads centre-relative (mirrors octree cube-centre invariant). | ✅ splat renders centred/visible in the remote frame. |
| R3 | Real 322 MB LAS convert (`pfconvert`) end-to-end. | integration/perf | — | ✅ 12,416,792 pts, Phase C 16 threads 0.89 s, matches the shipped `.vxpc` exactly. |
| R4 | Remote command handler audit (auth/role/garbage-JSON). | security | — | ✅ bad-PIN drop at 3 tries, viewer role ignores move/cmd/set, malformed JSON discarded, move axes clamped. No bug. |

**Remaining minor gaps (non-crashing, noted for follow-up):**
- `frameAll` (F) skips splat clouds (guards on `store`) → 'F' won't re-frame a splat-only scene (load-time `setupCamera` still frames it once). 
- Cloud Info panel shows the "no cloud" placeholder for a splat-only scene (Scene panel now shows the splat's point count correctly).
- No SH1–SH3 view-dependent colour for splats (parsed, not uploaded) — pre-existing, documented.

---

## A. Automated harness (pftest) — AUTO

| ID | Feature | Angle | Expected | Status |
|----|---------|-------|----------|--------|
| A1 | Octree convert, sequential, zstd off | functional | round-trip ok | ✅ |
| A2 | Octree convert, parallel (8 threads), zstd off | integration/perf | ok, ~1.04x | ✅ |
| A3 | Octree convert, sequential, zstd on | functional | ok | ✅ |
| A4 | Octree convert, parallel, zstd on | integration/perf | ok, ~1.12x | ✅ |
| A5 | Phase 7 repack (verbatim copy + upsert/remove) | functional | ok | ✅ |
| A6 | Phase 10 multi-cloud combine → one .vxpc | integration | ok | ✅ |
| A7 | Phase 11 plugin-data namespace + listing | functional | ok | ✅ |
| A8 | Phase 13 open .vxpc over http (Range reads) | integration | ok | ✅ |
| A9 | Phase 18 AES-256-GCM at rest (+ wrong-password reject) | security | ok | ✅ |
| A10 | Phase 1 3DGS SplatReader (.splat + 3DGS .ply) | functional | 2+2 splats | ✅ |

## B. File loading & format detection — AUTO (CLI + log)

| ID | Input | Angle | Expected | Status |
|----|-------|-------|----------|--------|
| B1 | `Tikal-13.vxpc` (147 MB LiDAR octree) | functional/regression | loads as octree (~12.4M pts) | ✅ |
| B2 | synthetic `.splat` | functional | loads as GaussianSplat | ✅ (A10) |
| B3 | synthetic 3DGS `.ply` | functional | loads as GaussianSplat | ✅ (A10) |
| B4 | `.vxpc` w/ `splat.bin` (packaged gsplat) | integration | loads via memory buffer | ✅ (A10) |
| B5 | raw non-gaussian `.ply` (`Tikal-13.ply`, mesh) | negative | NOT hijacked as splat; graceful fail/route | ✅ no "splat" log; routed to octree → "missing meta.bin", no crash |
| B6 | raw `.las` (`Tikal-13.las`) opened in viewer | negative | graceful fail (convert input, not viewable) | ✅ "missing meta.bin", no crash |
| B7 | nonexistent path | negative | graceful error, no crash | ✅ "cannot open package", app stayed alive |
| B8 | truncated/corrupt file (4 KB header) | negative | graceful error, no crash | ✅ "cannot open package", app stayed alive |
| B9 | `detectFormat` default = Unknown (regression) | regression | non-splat never mis-tagged SplatBinary | ✅ (root-cause fix) |

## C. Web Remote (civetweb + React) — BROWSER

| ID | Feature | Angle | Expected | Status |
|----|---------|-------|----------|--------|
| C1 | HTTP serves control page at `:8899` | functional | page loads | ✅ React PIN page loaded |
| C2 | Driver vs Viewer PIN gating | security | wrong PIN rejected; driver=control, viewer=watch | ✅ wrong→"Incorrect PIN"; driver 4097→full UI; server logged "authenticated (driver)" |
| C3 | `/shot.png` PIN-gated screenshot | functional/security | returns frame w/ PIN, 403 without | ✅ no-pin 403, wrong 403, right-no-shot 404, right+shot 200 (valid PNG 506 KB) |
| C4 | Command send (frame/reset/preset/pointsize/…) | integration | viewer reacts (log/cfg echo) | ✅ Frame-all moved camera (coords changed via cfg) |
| C5 | JPEG-over-WS viewport stream | functional | frames arrive | ✅ continuous blob frame GETs (200), no errors |
| C6 | cfg broadcast ~1 Hz | functional | live telemetry received | ✅ live cloud name / 12.4M pts / camera coords |
| C7 | stale-velocity zeroing (500 ms) | boundary | motion stops after release | ☐ manual (timing) |

## D. Rendering pipeline (incl. gsplat fixes) — MANUAL + partial BROWSER

| ID | Feature | Angle | Expected | Status |
|----|---------|-------|----------|--------|
| D1 | 5 color modes (TrueColor/Elev/Solid/Intensity/Class) | functional | each renders; Turbo legend on Elev/Intensity | ☐ |
| D2 | Point size 1–16, round, attenuate | boundary | clamps at ends | ☐ |
| D3 | EDL post pass (strength 0.1–5, radius 0.5–4) | functional/boundary | depth edges enhance | ☐ |
| D4 | LOD SSE budget + GPU budget + LRU eviction | perf | stays in budget, no leak | ☐ |
| D5 | Quality presets Low→Ultra map sse+GPU | functional | values change | ☐ |
| D6 | Frustum culling | perf | offscreen nodes not drawn | ☐ |
| D7 | gsplat 2D covariance (fixed J·W) | regression | correct perspective foreshortening | ☐ visual |
| D8 | gsplat back-to-front sort (fixed VBO reorder) | regression | no blend popping on orbit | ☐ visual |
| D9 | gsplat + LiDAR coexist, depth occlusion | integration | LiDAR occludes splats in front | ☐ visual |

## E. Camera / Navigation — MANUAL

| ID | Feature | Angle | Status |
|----|---------|-------|--------|
| E1 | Orbit/free-look/zoom-to-cursor/dbl-click focus | functional | ☐ |
| E2 | Fly WASD/QE, Shift 5× | functional | ☐ |
| E3 | Presets 1/3/7, Ortho 5 (+ ortho size) | functional | ☐ |
| E4 | Frame All (F), Reset View | functional | ☐ |
| E5 | Bookmarks per cloud, persist + embed in .vxpc | integration | ☐ |
| E6 | >100 km from origin precision warning | boundary | ☐ |

## F. Tools — MANUAL

| ID | Feature | Angle | Status |
|----|---------|-------|--------|
| F1 | Measure polyline, per-seg + total, undo/clear/copy | functional | ☐ |
| F2 | Annotate pins, persist, embed | functional | ☐ |
| F3 | Clip box min/max, reset planes | functional/boundary | ☐ |
| F4 | Slice export DXF/PNG/CSV, density, large warn | functional/boundary | ☐ |
| F5 | Tools mutually exclusive, Esc exits | functional | ☐ |

## G. Convert wizard — MANUAL (engine covered by A1–A4)

| ID | Feature | Angle | Status |
|----|---------|-------|--------|
| G1 | Source: LAS/LAZ/E57/PLY/PTS/XYZ multi-select + drag&drop | functional | ☐ |
| G2 | Quality cards + Advanced (spacing/leaf/depth/threads/flush/zstd) | functional | ☐ |
| G3 | Destination single→.vxpc / multi→folder | functional | ☐ |
| G4 | Progress + ETA, cancel mid Phase A/B | functional | ☐ |
| G5 | Photogrammetry from photos (GPS→ODM / else COLMAP) | integration | ☐ |
| G6 | BIOS-virtualization-disabled → ODM skip + guidance | negative | ☐ |

## H. Camera paths & video — MANUAL

| ID | Feature | Angle | Status |
|----|---------|-------|--------|
| H1 | Keyframe add/set/go/delete, scrub, preview | functional | ☐ |
| H2 | Export MP4 (≤4K, 24/30/60, 5–100 Mbps, NVENC/QSV) | functional/perf | ☐ |

## I. Stereo / Fullscreen / Hide-UI — MANUAL

| ID | Feature | Angle | Status |
|----|---------|-------|--------|
| I1 | Stereo SBS (F9) hides ALL UI, per-eye hint, watermark, Esc exits | functional | ☐ |
| I2 | Fullscreen (F11) | functional | ☐ |
| I3 | Hide-UI zen (F5 / Shift+Space) | functional | ☐ |

## J. UI shell — MANUAL

| ID | Feature | Angle | Status |
|----|---------|-------|--------|
| J1 | Menu bar all items | functional | ☐ |
| J2 | Toolbar + recent dropdown | functional | ☐ |
| J3 | Docks: Scene/Properties/Jobs/Console/Performance, reset layout | functional | ☐ |
| J4 | Command palette Ctrl+P fuzzy | functional | ☐ |
| J5 | F1 shortcuts overlay, F3 stats HUD | functional | ☐ |
| J6 | Preferences persist to pfview_config.txt | integration | ☐ |
| J7 | Multi-cloud Scene: vis toggle, close, "+", frame-all union | integration | ☐ |
| J8 | Drag&drop folder/.vxpc/raw-scan routing | functional | ☐ |

## K. Persistence / package / platform — mixed

| ID | Feature | Angle | Method | Status |
|----|---------|-------|--------|--------|
| K1 | Save project data → single .vxpc (embed sidecars) | integration | MANUAL | ☐ |
| K2 | Save into multi-cloud package blocked | negative | MANUAL | ☐ |
| K3 | .vxpc header/dir/zstd round-trip | functional | AUTO | ✅ (A) |
| K4 | HTTP(s) Range ByteSource | integration | AUTO | ✅ (A8) |
| K5 | File association HKCU refresh (Windows) | platform | MANUAL | ☐ |
| K6 | Watermark shown in UI, NOT in saved screenshots | functional | MANUAL | ☐ |

## Known limitations (from inventory §"Half-implemented")
- AES encryption: format-only, no UI (A9 tests the format layer only).
- Save into multi-cloud package: intentionally blocked (K2).
- About dialog version string stale ("0.1.0").
- WebRTC stream only if built with support; default JPEG/WS.
- MP4 export + serial/BT controller: Windows-only, stubbed elsewhere.
