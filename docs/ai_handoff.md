# AI Handoff

## Latest Session (2026-07-02) — Unity native plugin (branch `library/unity`)
PointForge is now consumable from Unity as a native DLL. New branch
**`library/unity`** (do not merge to main without review; main is untouched).

- **`src/library/unity/PointForgeC.h/.cpp`** — flat C API (`PF_OpenProject`,
  `PF_UpdateCamera`, `PF_GetVisibleNodes`, `PF_DequeueLoadedNode`,
  `PF_ReleaseLoadedNode`, `PF_UnloadNode`, `PF_GetEvictionCandidates`,
  `PF_GetStatistics`, `PF_GetNodeInfo`, `PF_SetLogCallback`). POD-only
  boundary, opaque handle, single-thread contract (Unity main thread);
  OctreeStore's worker thread does the disk I/O as usual.
- Ports pfview's frustum+SSE `visit()` traversal (recursion → explicit stack).
  Residency is tracked in the API layer because Unity owns the GPU buffers;
  draw list = visible ∧ resident. LRU eviction planning via
  `PF_GetEvictionCandidates` (never evicts nodes in the current draw list).
- **CMake**: new `pfunity` SHARED target (`PF_BUILD_UNITY_PLUGIN`, ON) —
  compiles `OctreeStore.cpp + Log.cpp + PointForgeC.cpp` only; links glm +
  zstd. **pfcore untouched**; no laszip/E57/SDL/GL in the DLL. The zstd
  detection block now sets `PF_ZSTD_LIB`/`PF_ZSTD_INC` before appending to
  `PF_OPTIONAL_*` so pfunity can link zstd alone.
- Build: `cmake --build build-static --config Release --target pfunity`
  → `build-static/Release/PointForgeUnity.dll` (static CRT+zstd, 432 KB,
  KERNEL32-only imports, 13 exports verified with dumpbin).
- Verified with a standalone smoke test against
  `C:\UnrealProject\model\PointForgeCache_direct` (1024 nodes, 12.4M pts):
  open/metadata/traversal/streaming/release/stats/eviction all correct.
- Consumer: Unity plugin repo at
  `C:\Unity\unityvc-base-project\Assets\Games\Pointcloud-unity` (own git repo,
  own AGENTS.md + docs/). Keep `PointForgeNative.cs` there in sync with
  `PointForgeC.h`.
- Known gaps: perspective-only SSE (ortho cameras unsupported), no
  orthographic variant exported yet; API version = 1 via `PF_GetVersion`.

## Previous Session — Docked UI shell redesign + single-file build fix
Two unrelated pieces of work, both on `main`:

### 1. Full UI redesign (viewport-centric docked shell)
The old UI was one scrolling 350px "PointForge Dashboard" ImGui window mixing
viewer settings, converter settings, and controller/ESP32 config. Preceded by
a UX audit (four independently-designed IA proposals — DCC-dock, steelmanned
top-tabs, viewport+jobs, ribbon — scored by three judge lenses: beginner,
power-user, scalability/feasibility). All four proposals, judged
independently, rejected top-level page tabs; the tabs advocate reversed its
own position after steelmanning. Converged recommendation: menu+dock shell
(same pattern as Unreal/Unity/Blender/VS/CloudCompare), conversion as a
background job queue, controller config out of daily UI into Preferences.
Implemented that recommendation in full — see `architecture.md` §7 for the
shell's structure and `decisions.md` ("Viewport-Centric Docked UI Shell") for
the rationale. Highlights:
- Repinned ImGui FetchContent `v1.91.5` -> **`v1.91.5-docking`** (same repo,
  docking branch) to get `DockSpace`/`DockBuilder`.
- New `src/viewer/Jobs.h` (`JobQueue`/`ConvertJob`): one worker thread runs
  conversions sequentially via the existing `IndexOptions` progress/cancel
  contract — unchanged from the old inline converter, just called from a
  queue instead of a raw `std::thread` in `main.cpp`. This is the same
  abstraction batch conversion and future AI-segmentation jobs reuse.
- New `src/viewer/UiLog.h` + `pf::setLogSink()` (added to `common/Log.h/.cpp`):
  mirrors every `pf::log()` call — including from the converter thread — into
  a ring buffer backing the new Console panel. Original stdout/stderr logging
  untouched; the UI sink is additive.
- `main.cpp` rewritten around the shell: menu bar, toolbar, passthru-docked
  viewport, Properties dock (collapsing sections; tool options *append*, e.g.
  Measure/Clip sections show up alongside Display rather than replacing it),
  bottom Jobs/Console/Performance dock (closed by default), status bar with a
  job-progress pill, toasts, F1 searchable shortcut sheet (one keybinding
  table is the source of truth), Ctrl+P command palette, welcome/empty state
  with drag-drop routing (octree dir -> load, raw scan file -> Convert dialog
  pre-filled), Preferences dialog (General/Display/Input/Advanced tabs —
  Input absorbs the entire gamepad + ESP32 block).
- **Stereoscopic SBS (F9) now suppresses ALL UI** per explicit requirement —
  not just leaves it on top of the split view. Every ImGui draw this
  frame (menu/toolbar/docks/status bar/watermark/measure overlay/colour
  legend) is skipped; only a fading "F9/Esc to exit" hint renders, drawn once
  per eye viewport so it fuses correctly through a stereoscope. Hotkeys still
  process; only rendering stops. Boots correctly into the hint if `stereoSBS`
  is loaded `true` from `pfview_config.txt`.
- New keys: `M`/`C` tool toggles, `1`/`3`/`7` view presets, `5` ortho, `F3`
  stats HUD, `F9` stereo, `Shift+Space` zen (alias for `F5`), `Ctrl+O/I/P/,`.
  Fixed a latent bug in the process: `F` (frame-all) used to fire while typing
  in an ImGui text field; now guarded by `WantCaptureKeyboard` like the other
  letter-key shortcuts.
- Verified: Release build clean (only pre-existing E57Reader warnings), 6s
  smoke-launch with no crash. **Not** verified against a live multi-billion
  point cloud this session, and the Jobs/Console panels are untested with a
  real conversion run end-to-end — worth doing before calling this final.

### 2. Single-file static build: toolset-mismatch fix simplified
The existing single-file recipe (decisions.md, prior session) required
configuring with Ninja inside a VS18 developer command prompt to dodge a
vcpkg/CMake-generator toolset mismatch (LNK2019 on `__std_regex_transform_*`
etc. — vcpkg auto-picked the newest installed MSVC, 14.51 from VS 18
Community, while CMake's default "Visual Studio 17 2022" generator targets
14.44 from the VS 2022 BuildTools instance). Reproduced the *exact same*
LNK2019 this session, then found a one-file fix instead of the Ninja dance:
new **`triplets/x64-windows-static.cmake`** overlay triplet pins
`VCPKG_PLATFORM_TOOLSET_VERSION 14.44`, forcing vcpkg to compile every static
port with the toolset the default generator actually links against. Normal
`cmake -B build-static -S . -DCMAKE_TOOLCHAIN_FILE=... -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_OVERLAY_TRIPLETS=<repo>/triplets`
+ `cmake --build build-static --config Release` now links clean, no Ninja/
vcvars step. Verified: `dumpbin /DEPENDENTS` on the resulting
`ViitorXPCViewer.exe` shows only OS DLLs (OPENGL32/KERNEL32/USER32/GDI32/
WINMM/IMM32/ole32/OLEAUT32/VERSION/ADVAPI32/SETUPAPI/SHELL32); copied the exe
alone into an empty directory and launched it successfully (6s, no crash).
The Ninja recipe in decisions.md still works and is left documented, but the
overlay-triplet recipe above is simpler and should be preferred going forward.

## Previous Session — Controller support
Branch `Controller-support`. Added gamepad / joystick input via SDL (no new dep):
- New `src/viewer/Controller.{h,cpp}` — `GameInput` wraps `SDL_GameController`
  (Xbox, auto-mapped) and falls back to raw `SDL_Joystick` for custom HIDs
  (axes/buttons by configurable index). Deadzone, edge-detected buttons, hotplug.
- `main.cpp`: `SDL_INIT_GAMECONTROLLER|JOYSTICK`, per-frame poll + apply.
  - Camera: left stick move, right stick look, triggers down/up, RB boost.
    Custom 1-stick pads: hold LB + stick to look.
  - UI: `ImGuiConfigFlags_NavEnableGamepad` toggled by **UI-nav mode**
    (Start/B). Xbox drives ImGui nav natively; raw joysticks use button actions.
  - Actions: A=frame-all, Y=measure, X=screenshot, Back=toggle UI.
  - Settings panel "Controller": enable, deadzone, look/move sens, invert-Y,
    live raw axis/button monitor + index rebinding for custom devices. Persisted.
- Status bar shows `Pad:Cam/UI`; F1 help lists controller controls.

Limitation: raw-joystick ImGui nav is not auto-fed (Xbox only); custom pads get
button actions + camera. Not tested with a physical device this session — verify
mappings live (the rebind panel exists for non-standard axis/button indices).

### Custom ESP32 controller (Bluetooth SPP serial)
The user's custom controller is **not** a USB HID — it's an ESP32 streaming over
a Bluetooth virtual COM port (protocol from their `JoystickReceiverBluetooth.cs`:
lines `x,y,b` with 12-bit ADC + active-low button, plus `PAUSE`/`PLAY`). Added
`src/viewer/SerialController.{h,cpp}` (Win32 only): background thread opens the
COM port (auto-detected from the device MAC via `HKLM\...\Enum\BTHENUM`, same as
the C# script; manual COM fallback), parses lines, exposes normX/normY + trigger
+ pause/play one-shots. Wired in `main.cpp` as a third input alongside mouse +
Xbox:
- **Joystick = look**; **hold trigger = fly forward** along the look direction.
- **PAUSE** = toggle UI-nav mode; in UI mode the stick feeds ImGui gamepad-nav
  (`AddKeyAnalogEvent(GamepadLStick*)`) and **PLAY** = activate (`GamepadFaceDown`).
- **PLAY** in camera mode = Frame-All.
- Controller panel has a "Custom serial controller" section: enable, auto/MAC/COM,
  Reconnect, live X/Y/trigger readout. Persisted (`serialEnabled/Auto/Mac/Port`).
Default MAC `B4BFE90B6036` (from the user's script). Not tested with the physical
device — verify COM auto-detect + axis orientation (invert-Y available).

## Previous Session — UX overhaul
Implemented a broad UX pass on `pfview` (single-file `ViitorXPCViewer`):
- **Navigation**: LMB-drag orbit, double-click focus, wheel zoom-to-cursor
  (point size moved to Ctrl+wheel), `F` frame-all, `F11` fullscreen. Camera
  gained `orbit()`/`lookAt()`; cursor interaction uses GPU depth readback.
- **HUD**: always-on status bar (FPS / points / GPU MB / mode / live cursor XYZ /
  loading), `F1` controls overlay, slider tooltips.
- **Measure**: multi-segment polyline (per-segment + total length), snap preview,
  undo / clear / copy-to-clipboard.
- **Visual**: Quality preset, colour-by Intensity + Classification (GpuVertex now
  carries both; shader modes 3/4 + ASPRS palette), elevation/intensity colour-bar
  legend, light/dark theme.
- **EDL**: eye-dome lighting post-process — scene now renders to an offscreen FBO,
  fullscreen pass copies or shades depth edges. Strength/radius UI.
- **Loading**: recent-files (MRU) dropdown, auto-load-last toggle, convert Cancel
  button (cooperative `IndexOptions::cancel`).
- **QoL**: `F12` screenshot (BMP), reset-to-defaults confirmation, clear-clipping,
  top toolbar (Open/Frame/Measure/Shot/Help).

Touched: `Camera.*`, `OctreeStore.*` (GpuVertex), `PointRenderer.cpp` (attribs),
`Shader.*`, `EmbeddedShaders.h` (point + EDL), `OctreeIndexer.*` (cancel),
`main.cpp`. All build (dynamic + static single-file). Not yet tested against a
real cloud — verify orbit/measure/EDL/colour modes on actual data.

## Prior Work
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

## Modified Files (this session — UI shell + static build)
- `src/viewer/Jobs.h` (new)      — `JobQueue`/`ConvertJob` background conversion queue
- `src/viewer/UiLog.h` (new)     — Console panel ring buffer
- `src/common/Log.h`, `src/common/Log.cpp` — added `pf::setLogSink()`
- `src/viewer/main.cpp`          — full shell rewrite (docking, menu/toolbar,
  Properties/Jobs/Console/Performance docks, status bar, palette, shortcuts,
  Preferences dialog, stereo-hides-UI, new hotkeys)
- `CMakeLists.txt`               — ImGui FetchContent tag -> `v1.91.5-docking`
- `triplets/x64-windows-static.cmake` (new) — overlay triplet, toolset pin
- `CLAUDE.md`                    — stack/layout/run sections updated for the new shell
- `docs/*.md` (all six)          — this handoff entry + architecture §7 +
  decisions + roadmap/tasks updates

## Modified Files (prior sessions)
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
A fully static, single-file `ViitorXPCViewer.exe` (~6.5 MB): zero non-system
DLLs, shaders + icon embedded. See decisions.md ("Single-File Static Release")
for the full history. **Current (simplest) recipe** — no Ninja/vcvars needed:

```powershell
cmake -B build-static -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_OVERLAY_TRIPLETS=C:/UnrealProject/PointForge/triplets
cmake --build build-static --config Release   # -> build-static/Release/ViitorXPCViewer.exe
```
`triplets/x64-windows-static.cmake` pins `VCPKG_PLATFORM_TOOLSET_VERSION` to
match whatever MSVC toolset the default "Visual Studio 17 2022" generator
resolves to on this machine (currently 14.44) — check
`grep CMAKE_GENERATOR_INSTANCE build-static/CMakeCache.txt` and the installed
`VC/Tools/MSVC/<version>` folders if you re-provision the machine and this
starts failing with LNK2019 on `__std_*` symbols again; bump the pin to match.

Older fallback (still works, more steps — see decisions.md for why it was
needed originally):
```powershell
cmake -B build-static-ninja -S . -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_HOST_TRIPLET=x64-windows-static
cmake --build build-static-ninja --target pfview   # run from inside a VS18 dev shell
```
The dynamic build (`build/`, VS2022) still works for fast iteration.

New files: `src/viewer/EmbeddedShaders.h`, `src/viewer/EmbeddedImage.h`
(generated from `shaders/` and `images/vx.bmp` — regenerate if those change).

## Next Recommended Task
- **Verify the new shell against a real conversion end-to-end**: enqueue a
  real scan in the Convert dialog, confirm the Jobs panel progress/cancel,
  status-bar pill, completion toast + auto-load, and Console log lines all
  behave under an actual multi-minute conversion (only smoke-tested this
  session — no real job was run through it), **or**
- **Multi-select batch conversion** in the Convert dialog — the `JobQueue`
  already supports N jobs, this is UI-only (file dialog multi-select loop
  calling `jobs.enqueue()` per file), **or**
- **On-disk cache purge**: a "Clear Cache" action for old `PointForgeCache_*`
  converted-cloud dirs (still a gap; predates this session).

## Note on scope
This repo is the **desktop SDL2/OpenGL** PointForge. A separate **Unreal Engine
`PointForgeViewer` plugin** (EDL, scene proxy, crash fixes) lives in another repo
and is *not* covered by these docs.
