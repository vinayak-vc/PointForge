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
- **Toolset gotcha**: vcpkg compiled the static deps with **VS18 / MSVC 14.51**, but CMake 3.28 only generates "Visual Studio 17 2022" projects -> the VS2022 static CRT lacked newer STL vectorized symbols (`__std_find_first_not_of_trivial_pos_1`, `__std_regex_transform_primary_char`) referenced by E57/laszip, causing LNK2019. **Fix (session 1)**: build with the **Ninja** generator inside the VS18 developer environment so the exe links against the same MSVC 14.51 CRT as the deps. Verified zero non-system DLL imports via `dumpbin /DEPENDENTS`.
- **Toolset gotcha, simpler fix (session 2, current)**: the Ninja/vcvars dance is a genuine hassle to reproduce. Root cause is really that **vcpkg's own toolset auto-selection** picks the newest MSVC across *all* installed VS instances (14.51 from VS 18 Community) instead of the one CMake's default "Visual Studio 17 2022" generator actually targets (14.44, from the VS 2022 BuildTools instance CMake's `CMAKE_GENERATOR_INSTANCE` resolves to). Pin it the other direction instead: `triplets/x64-windows-static.cmake` overlay triplet sets `VCPKG_PLATFORM_TOOLSET_VERSION 14.44`, forcing vcpkg to compile every static port with the *same* 14.44 toolset the default generator links with. No Ninja, no vcvars, no separate dev-shell step — `cmake -B build-static -S . -DCMAKE_TOOLCHAIN_FILE=... -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_OVERLAY_TRIPLETS=<repo>/triplets` then a normal `cmake --build build-static --config Release` links cleanly. Re-verified zero non-system DLL imports via `dumpbin /DEPENDENTS` (only OPENGL32/KERNEL32/USER32/GDI32/WINMM/IMM32/ole32/OLEAUT32/VERSION/ADVAPI32/SETUPAPI/SHELL32 — all OS-provided) and a clean launch from an exe copied alone into an empty directory. **Prefer this recipe going forward**; the Ninja approach still works but is no longer necessary.

## Viewport-Centric Docked UI Shell
- **Decision**: Replaced the single scrolling 350px "PointForge Dashboard" ImGui window (which mixed viewer settings, converter settings, and controller config in one list) with a docked shell: menu bar, thin toolbar, central passthru-docked viewport, right "Properties" panel (collapsing sections), bottom "Jobs / Console / Performance" dock (closed by default), status bar. Requires ImGui's docking branch — repinned FetchContent from `v1.91.5` to `v1.91.5-docking`.
- **Reason**: A UX audit (four independent design proposals judged against beginner/power-user/scalability lenses) unanimously rejected top-level page tabs — the viewport is the application, and a tab that hides it to show settings or a converter defeats the point, and cannot represent a background job staying visible while the user keeps working. Every named reference app (Unreal, Unity, Blender, Visual Studio, CloudCompare) uses the same menu+dock shell for this reason. See `architecture.md` §7.
- **Consequence**: Conversion moved from an inline blocking-looking panel to a `JobQueue` background-thread abstraction (`src/viewer/Jobs.h`) — the same one batch conversion and future AI-segmentation jobs will reuse without further shell changes. Controller/ESP32 config (previously inline next to point-size) moved to a Preferences dialog, since it's configured once, not touched daily.
- **Stereoscopic SBS**: extended to hide *all* UI chrome (not just leave it visible over the split view) per explicit user requirement — a stereoscope should show nothing but the two eye images. F9/Esc still processes as input even though nothing is drawn; a fading exit-hint is rendered once per eye so it fuses correctly.
## Controller Support (SDL gamepad + raw joystick)
- **Decision**: One `GameInput` wrapper over SDL. Prefer `SDL_GameController` (Xbox-style, auto-mapped via SDL's built-in DB); fall back to raw `SDL_Joystick` for custom HIDs, reading axes/buttons by **configurable index** with a live rebinding panel. No new dependency — SDL2 is already linked, so the single-file static exe stays single-file.
- **Camera vs UI**: a **UI-nav mode** toggle (Start / B) flips `ImGuiConfigFlags_NavEnableGamepad` on and suppresses camera input, so sticks never drive both at once. ImGui's SDL2 backend feeds gamepad nav natively for `SDL_GameController`; raw joysticks rely on mapped button actions (full raw-joystick ImGui nav is a known gap).
- **Custom 1-stick pads**: with no right stick, holding **LB** switches the single stick from move to look (modifier pattern). Bindings + deadzone + sensitivities persist in `pfview_config.txt`.
- **Why index-based rebinding**: raw joystick axis/button numbering is not standardised across devices, so a fixed mapping can't work for an arbitrary custom controller; the panel shows live values to discover indices.

## Custom ESP32 Controller (Bluetooth SPP serial)
- **Decision**: The user's custom controller is an ESP32 over Bluetooth SPP (virtual COM port), not a HID joystick, so SDL can't see it. Added a dedicated Win32 `SerialController` (background thread, `CreateFile("\\\\.\\COMx")` + blocking `ReadFile`) that parses the exact protocol from their `JoystickReceiverBluetooth.cs` (`x,y,b` lines, `PAUSE`/`PLAY`). COM port is auto-detected from the device MAC via `HKLM\SYSTEM\CurrentControlSet\Enum\BTHENUM` (the same registry walk the C# uses), with a manual COM fallback.
- **Kept separate from SDL input**: it's a third input source alongside mouse and the SDL `GameInput` (Xbox/raw joystick) — existing paths untouched, per the user's "only add" requirement.
- **Mapping** (user choice): joystick = look; hold the joystick trigger = fly forward along the look direction; PAUSE toggles UI-nav mode; PLAY = activate in UI (fed to ImGui via `GamepadFaceDown`) / Frame-All in camera mode. In UI mode the stick drives ImGui nav through `AddKeyAnalogEvent(ImGuiKey_GamepadLStick*)`.
- **No new dependency**: pure Win32 + std::thread, so the single-file static exe stays single-file. Non-Windows builds get a no-op stub.

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

## Unity Plugin: Thin C API Over OctreeStore (branch `library/unity`)
- **Decision**: Unity integration is a separate SHARED target (`pfunity`) that
  compiles only `OctreeStore.cpp + Log.cpp` plus a new flat C API
  (`src/library/unity/PointForgeC.{h,cpp}`). It does NOT link pfcore.
- **Reason**: Unity consumes already-converted octrees, so the importer stack
  (laszip/E57/indexer) is dead weight and would drag extra DLLs across the
  plugin boundary. OctreeStore has no SDL/GL/ImGui dependencies, so the
  streaming path is reusable as-is — the DLL ends up with KERNEL32-only
  imports when built with the static triplet.
- **Consequence**: LOD/visibility decisions stay native (the pfview
  frustum+SSE traversal was ported into the API layer), Unity only mirrors
  GPU residency back (`PF_ReleaseLoadedNode`/`PF_UnloadNode`). Keep
  `PointForgeC.h` in sync with `PointForgeNative.cs` in the Unity repo
  (`C:\Unity\unityvc-base-project\Assets\Games\Pointcloud-unity`).
  POD-only boundary; API version via `PF_GetVersion` (currently 1).

## Convert Integration (DLL instead of Process)
- **Decision**: Replaced System.Diagnostics.Process invocation of pfconvert.exe with a native DLL library (PointForgeConvert.dll) in the Unity integration.
- **Reason**: System.Diagnostics.Process is not supported and usually stripped in IL2CPP builds. Providing a DLL ensures cross-platform and IL2CPP compatibility within Unity, keeping the conversion logic directly within the Unity process space.
- **Consequence**: Added a new C-API pfconvert_api.cpp and a pfconvert_dll CMake target that exposes PF_ConvertDataset and PF_Convert_SetLogCallback. Logs are sent via a callback to C#, which avoids freezing the editor and correctly feeds the Unity UI console.
