# Architectural Decisions

This document records major design decisions.

## Out-of-Core Processing
- **Decision**: The importer uses a Three-Phase algorithm (Count, Chunk, Index).
- **Reason**: Processing datasets exceeding RAM capacity requires bounded memory streaming. The "Chunk" phase acts as a spatial scatter, meaning Phase C (Index) can operate completely independently in parallel on separate chunks with no locking.

## Viewer Stack
- **Decision**: Uses SDL2 + OpenGL 3.3 Core + Dear ImGui.
- **Reason**: Maximizes cross-platform compatibility without heavy dependencies (like Qt or modern Vulkan which complicates driver support for older enterprise hardware).

## CPU / GPU Separation
- **Decision**: Point nodes stream directly from disk to GPU memory (`glBufferData`) and are then immediately freed from CPU RAM.
- **Reason**: Double buffering points in CPU RAM causes massive overhead. 
- **Consequence**: Implemented CPU-based point-picking requires loading a chunk specifically for intersection, or building a dedicated picking octree, which is a known limitation.

## Application Name & Presentation
- **Decision**: The executable is built as a WIN32 subsystem application (on Windows) rather than a Console App.
- **Reason**: This prevents a persistent and ugly DOS prompt terminal window from opening alongside the ViitorX Viewer application.

## Settings Persistence
- **Decision**: Viewer settings are serialized to a local `pfview_config.txt` file manually rather than relying on a complex JSON parser.
- **Reason**: Avoids heavy dependencies, keeping the repository light and builds fast.

## Point Picking (synchronous on-click disk read)
- **Decision**: `OctreeStore::pickPoint()` casts the ray against resident node cubes and reads the payloads of intersected nodes off `octree.bin` synchronously at click time (reusing the streaming decode path `readNodeInto`), rather than keeping every point's coordinates resident in CPU RAM.
- **Reason**: Preserves the CPU/GPU separation invariant (points stream disk -> GPU, freed from RAM). A pick is a rare user action, so a one-off bounded disk scan is cheaper than permanently doubling memory.
- **Consequence**: Pick latency is bounded by `maxScanPoints` (default 8M) and by how many nodes the ray crosses; ray-AABB pruning + a `bestT` cutoff keep it small. Picks resolve in world coords; the camera ray is re-based from centred to world space inside `pickPoint`.

## Streaming Queue Purge (frame-stamped staleness)
- **Decision**: Each load request is stamped with the frame it was last wanted. `purgeStale()` drops queued-but-unstarted requests older than ~120 frames and caps the buffered `ready_` results.
- **Reason**: A fast-moving camera enqueues nodes it then leaves frustum before the worker reaches them; without purging, the queue and upload backlog grow unbounded. In-flight (already reading) requests are not cancelled — only not-yet-started ones are dropped.

## Single-File Static Release (`ViitorXPCViewer.exe`)
- **Decision**: Ship the viewer as one self-contained exe. Statically link all vcpkg deps via the `x64-windows-static` triplet, static CRT (`/MT`), embed shaders (`EmbeddedShaders.h`) and the watermark/icon BMP (`EmbeddedImage.h`), and rename the `pfview` output to `ViitorXPCViewer`.
- **Reason**: Distributable as a single file with zero DLLs and no external `shaders/` folder or hardcoded image path (the old absolute `C:\...\vx.bmp` broke on any other machine).
- **Build**: CMakeLists auto-detects a `*static*` triplet (`PF_STATIC`) and switches to single-binary mode (static CRT, `GLEW_STATIC`, `SDL2::SDL2-static`); the default dynamic triplet build is unchanged. laszip has no CMake config, so its lib is found release-only (`PATHS ... NO_DEFAULT_PATH`) to avoid latching onto the debug variant; zstd uses its config target.
- **Toolset gotcha**: vcpkg compiled the static deps with **VS18 / MSVC 14.51**, but CMake 3.28 only generates "Visual Studio 17 2022" projects -> the VS2022 static CRT lacked newer STL vectorized symbols (`__std_find_first_not_of_trivial_pos_1`, `__std_regex_transform_primary_char`) referenced by E57/laszip, causing LNK2019. **Fix**: build with the **Ninja** generator inside the VS18 developer environment so the exe links against the same MSVC 14.51 CRT as the deps. Verified zero non-system DLL imports via `dumpbin /DEPENDENTS`.
## DPI Scaling (metrics-only)
- **Decision**: Detect display DPI via `SDL_GetDisplayDPI` (96 dpi = 1.0x) for the default; apply with `ImGuiStyle::ScaleAllSizes` from a captured base style plus `io.FontGlobalScale`. Exposed as a persisted "UI Scale" slider. The same apply path also swaps light/dark theme colours from the captured base.
- **Reason**: Simple, dependency-free, and re-applies cleanly (base style snapshot avoids cumulative scaling). Trade-off: the font glyph atlas is not re-rasterized, so large scales soften text — acceptable for now (tracked in tasks.md).

## UX Overhaul (navigation, HUD, EDL)
- **Depth-readback for cursor interaction**: double-click focus, wheel zoom-to-cursor, and the status-bar XYZ all unproject the GPU depth buffer at the cursor (`glReadPixels(GL_DEPTH_COMPONENT)` + `inverse(viewProj)`), not a disk pick — nearly free and works between stored points. Exact-point `pickPoint` (disk) stays reserved for measurement, where the stored coordinate matters.
- **Always render to an offscreen FBO**: the scene draws into a colour+depth FBO every frame (not only when EDL is on), then a fullscreen pass copies (EDL off) or shades (EDL on). One code path means cursor depth-reads work identically either way; cost is one fullscreen blit/frame.
- **EDL = screen-space depth-edge darkening**: 4-neighbour positive depth-difference sum, `shade = exp(-strength*response)`. Uses raw (non-linear) depth for simplicity; good for perspective, weaker for ortho (roadmap).
- **GpuVertex 16 -> 20 bytes**: added `intensity` (uint16) + `classification` (uint8) so the viewer can colour by them on the GPU (new attribs loc 2/3, shader modes 3/4). Accepted per-point GPU cost for the feature.
- **Orbit vs free-look**: LMB-drag = turntable orbit around a pivot (discoverable default); RMB-drag keeps free-fly look; pivot updates on double-click/zoom via depth readback; measure mode reclaims LMB.
- **Convert cancel is cooperative**: `IndexOptions::cancel` (atomic) is polled at Phase C chunk boundaries; aborting returns false and leaves partial output. Phase A/B not yet cancellable.
