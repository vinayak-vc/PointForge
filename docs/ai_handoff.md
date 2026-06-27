# AI Handoff

## Recent Work
Implemented four roadmap tasks in the desktop `pfview` viewer / `pfcore` and
fixed on-disk-format doc drift. Followed the AGENTS.md workflow (read
architecture/roadmap/handoff first; updated tasks.md + decisions.md).

### Features Added
- **CPU Point Picking + distance measurement**: New "Measure" panel. With
  *Measure mode* on, LMB picks points; two picks draw a marker pair + connecting
  line + live distance (metres) via an ImGui foreground overlay. Picks resolve
  by a synchronous, bounded ray scan over the octree (no CPU-resident point
  buffer — see decisions.md). Clear button resets.
- **Streaming queue purge**: `OctreeStore` now stamps each load request with the
  frame it was wanted and exposes `purgeStale()`, called every frame, to drop
  stale not-yet-started requests and cap buffered uploads. Keeps the load queue
  bounded under fast camera motion.
- **ImGui DPI scaling**: Auto-detects display DPI at startup; persisted "UI
  Scale" slider re-applies cleanly from a captured base style. Default clamped to
  0.5x–4.0x.
- **Doc drift fix**: `architecture.md` `NodeRecord` corrected 32 -> 52 bytes
  (explicit `children[8]`, not `firstChild`) and example `metadata.json` updated
  to v2 (bytesPerPoint 22, classification attribute). `CLAUDE.md` `PackedPoint`
  corrected 20 -> 22 bytes.

## Modified Files
- `src/viewer/Camera.h`, `src/viewer/Camera.cpp`        — `screenRay()` (unproject pick ray)
- `src/viewer/OctreeStore.h`, `src/viewer/OctreeStore.cpp` — `pickPoint()`, `readNodeInto()`, `purgeStale()`, frame-stamped requests
- `src/viewer/main.cpp`                                  — measure state/overlay/UI, DPI scaling, purge call, frame-stamped `requestLoad`
- `docs/architecture.md`, `CLAUDE.md`                   — format doc-drift fixes
- `docs/tasks.md`, `docs/decisions.md`, `docs/ai_handoff.md` — workflow upkeep

## Architecture Notes
- Picking re-bases the centred camera ray to world space (`+ cubeCentre`) because
  node cubes and unpacked points are stored in world coords. Tolerance scales
  with along-ray distance (`tolPerDist = pickPx / ssFactor`) for a fixed-pixel
  pick disc.
- `readNodeInto()` is the shared decode (raw read or zstd) used by both the
  background streaming worker and the synchronous picker — keep them in sync.
- DPI scaling does **not** re-rasterize the font atlas (metrics + FontGlobalScale
  only); large scales soften text.

## Verification
- `cmake --build build --config Release --target pfview` succeeds. One *pre-existing*
  warning (C4244 at main.cpp flushBudget, unrelated to this work). Not run against
  a live cloud this session — pick accuracy/perf on a real octree is untested.

## Single-File Release Build
`release/ViitorXPCViewer.exe` (6.2 MB) is a fully static, single-file viewer:
zero non-system DLLs, shaders + icon embedded. See decisions.md ("Single-File
Static Release") for the full recipe. Reproduce:

```powershell
# 1. compile static deps (one-time, long): default-generator configure
cmake -B build-static -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_HOST_TRIPLET=x64-windows-static
# 2. configure + build with Ninja INSIDE the VS18 dev env (must match the
#    toolset vcpkg used, else LNK2019 on __std_* STL symbols):
#    run vcvars64.bat from VS18, put its Ninja on PATH, then:
cmake -B build-static-ninja -S . -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_HOST_TRIPLET=x64-windows-static
cmake --build build-static-ninja --target pfview   # -> ViitorXPCViewer.exe
```
The dynamic build (`build/`, VS2022) still works for fast iteration.

New files: `src/viewer/EmbeddedShaders.h`, `src/viewer/EmbeddedImage.h`
(generated from `shaders/` and `images/vx.bmp` — regenerate if those change).

## Next Recommended Task
- **Eye-Dome Lighting (EDL)** post-process for depth perception (roadmap mid-term;
  fullscreen pass, mirrors Axiom Present SSAO pattern), **or**
- **On-disk cache purge**: a "Clear Cache" action for old `PointForgeCache_*`
  converted-cloud dirs (the Converter "Cache" readout is still a 0 MB stub at
  main.cpp's converter panel).

## Note on scope
This repo is the **desktop SDL2/OpenGL** PointForge. A separate **Unreal Engine
`PointForgeViewer` plugin** (EDL, scene proxy, crash fixes) lives in another repo
and is *not* covered by these docs.
