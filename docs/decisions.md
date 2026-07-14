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

## Encryption not wired into the viewer/CLI (deferred, on purpose)
- **Decision**: Did NOT add `pfconvert --encrypt` or a viewer password prompt, even though
  Phase 18 shipped an at-rest AES-256-GCM API in `PackageFormat`.
- **Reason**: The viewer streams `octree.bin` (the point payloads) by seeking to a raw byte
  offset inside the `.vxpc` container (`OctreeStore::readNodeInto`), never through the
  decrypting `PackageReader::Read`. Encryption in `PackageWriter` only applies to `AddMemory`
  entries (meta/hierarchy/sidecars), not the streamed `octree.bin` (raw `Write`). So a
  `--encrypt` cloud would leave the actual points **plaintext** on disk while presenting as
  "encrypted" — a misleading, weak security guarantee, which the org security guidelines say
  to avoid. Per-entry GCM (one IV/tag per whole entry) is also incompatible with random
  per-node seeking.
- **Consequence**: Real at-rest protection of the points requires per-node payload
  encryption (per-node IV/tag or a seekable CTR scheme) plus a decrypt step in the streaming
  path — a larger format/streaming change. Left as a scoped follow-up rather than shipping a
  half-measure.

## Cache auto-purge → orphaned chunk-dir cleanup (roadmap item reinterpreted)
- **Decision**: There is no on-disk "cache" concept in this repo to auto-purge (the
  `PointForgeCache_*` name in docs is UE-plugin/test-data naming, never created here). Instead
  of scanning+deleting arbitrary user directories (dangerous), `buildOctree` now removes a
  stale `<workDir>/chunks` directory left by a previously crashed/killed conversion to the
  same target, at the start of the next run.
- **Reason**: A leftover chunk dir would otherwise mix stale chunk files into the new run and
  corrupt output. The cleanup is scoped to the exact path buildOctree owns and creates, so it
  can't touch unrelated user data.

## Guided Conversion Wizard (fullscreen modal)
- **Decision**: The Convert flow is a step-by-step fullscreen wizard rendered as an
  ImGui `BeginPopupModal` sized to the whole viewport (Source → Quality → Destination
  → Convert → Done), replacing the old compact non-modal "Convert to Octree" window.
- **Reason**: The user asked for a guided flow that can't be disturbed. A modal popup
  blocks input to the menu bar, toolbar and docks for free, and covering the full
  viewport hides them visually — so once a conversion starts the user cannot touch
  other controls until it finishes or is canceled.
- **Consequence**: The underlying state (`convInputs`/`convOutput`/`convPreset`/
  `customOpts`) and the `JobQueue`/`buildOctree` path are unchanged — the wizard just
  guides the choices. It captures the jobs it enqueues (`convWizJobs`) to drive the
  Converting/Done screens, and enqueues them with `loadWhenDone=false` so the wizard
  (not the global finished-job handler) owns post-conversion loading. The `--convert`
  smoke hook enqueues directly and bypasses the wizard so headless runs still auto-load.

## Settings Persistence (User AppData)
- **Decision**: Viewer settings (`pfview_config.txt`) are serialized into the user's `AppData` directory via `SDL_GetPrefPath("ViitorX", "ViitorXPC")`, rather than alongside the executable.
- **Reason**: Standard desktop application behavior. Keeping it in `AppData` ensures the executable can be deployed to a read-only directory (e.g., `Program Files`) without triggering UAC virtualization or permission-denied errors when users modify UI settings.

## Application Branding and Visuals
- **Decision**: The project is officially branded as `ViitorXPC` (executable `ViitorXPCViewer.exe`). A Windows Resource Script (`app.rc`) embeds a transparent Windows icon (`vx.ico`) for native File Explorer/Taskbar integration.
- **Decision**: The background watermark logo in Stereoscopic SBS mode renders twice with a negative parallax shift (15px convergence).
- **Reason**: A 2D overlay across the whole screen breaks stereoscopic fusion. Rendering it twice natively in the SBS buffer with negative parallax makes the logo fuse correctly and "pop out" of the 3D depth field towards the user.

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

## WebRTC Answer Must Mirror the Browser's Offer (mid + payload type)
- **Decision**: When the browser's `webrtc_offer` arrives, the server parses it with `rtc::Description`, extracts the video m-line's **mid** and the browser's **H264 payload type** (preferring `packetization-mode=1`) plus its fmtp, and builds the local send track from those — never hardcoded values.
- **Reason**: libdatachannel matches local tracks to remote m-lines strictly by mid (`populateLocalDescription` → `mTracks.find(remoteMedia->mid())`). The original hardcoded mid `"video"`/PT 96 never matched Chrome's offer (mid `"0"`, dynamic PT), so the answer marked the video line removed (port 0) and no media could ever flow — black screen on every browser.
- **Consequence**: If the offer carries no H264 codec at all, the server logs an error and stays on JPEG instead of negotiating a dead connection. Client-side companions to this fix: `useWebSocket` forwards the server's `webrtc_candidate` trickle-ICE messages (previously dropped — only `webrtc_ice` was handled), `useWebRTC` queues remote candidates until `setRemoteDescription` resolves, and the `onWebRTC` handler is wired through a ref (a `let` placeholder captured a stale no-op in the options object, silently discarding the answer).

## Virtual File System (`.vxpc` Container)
- **Decision**: PointForge output files (`octree.bin`, `hierarchy.bin`, `meta.bin`, `metadata.json`) are now packaged into a single binary container format (`.vxpc`) instead of a traditional directory folder.
- **Reason**: Millions of tiny files or multi-file directory structures are painful for enterprise customers to copy, share, and backup. A single container acts identically to Unity AssetBundles or Unreal `.pak` files.
- **Consequence**: 
  - `PackageWriter` creates the container with zero-copy streamable `AddFile` and memory-mapped `AddMemory`.
  - `PackageReader` fulfills the new `VirtualFileSystem` interface so `OctreeStore` functions identically whether loading from a folder or `.vxpc` package.
  - Payloads can optionally be individually compressed with `ZSTD` (`PackageWriter::Compression::ZSTD`), verified safely via CRC32 validation during `PackageReader::Read`, and embedded with dynamic custom JSON metadata (`custom_meta.json`) and synthetic visual thumbnails (`thumbnail.raw` / `thumbnail.jpg`).

## WebRTC Encoder: Hardware-First, Quality VBR, Never-Crash Fallback
- **Decision**: The encode thread selects a hardware H.264 MFT (`MFTEnumEx` with `MFT_ENUM_FLAG_HARDWARE` — NVENC/QuickSync/VCE) before falling back to the software `CLSID_CMSH264EncoderMFT`. Rate control is quality-based VBR (`eAVEncCommonRateControlMode_Quality`, quality 78) with a 12 Mbps `MF_MT_AVG_BITRATE` hint (up from a hardcoded 2 Mbps CBR).
- **Reason**: Point-cloud frames are dense fields of high-contrast dots — worst-case content for H.264 inter prediction, so 2 Mbps CBR smeared badly versus the JPEG stream's effective 12–18 Mbps. The software encoder also burned CPU (plus CPU RGB→NV12) competing with the renderer; hardware encode moves that to the GPU.
- **Consequence**: Hardware codec MFTs are async by spec, so the encode loop gained the `METransformNeedInput`/`METransformHaveOutput` event model — pumped strictly non-blocking (`MF_EVENT_FLAG_NO_WAIT`) with a bounded 150 ms input-credit wait that drops the frame rather than stalling. Any hardware failure (enum, activate, configure, runtime ProcessInput/Output) releases the encoder and permanently falls back to software for the session via a sticky `mfHwFailed` flag — the stream degrades, the app never crashes or hangs. Verified live: NVENC selected on the dev machine, browser decoded 1280×734, survived repeated JPEG↔WebRTC toggles and reconnects with zero errors.

## Auto-Incrementing Build Version
- **Decision**: A `VERSION` file at the repo root (`MAJOR.MINOR.PATCH`) is stamped into every build. `tools/bump_version.cmake` (an `ALL` custom target `version_header`, same pattern as `web_build`/`embed_web_header`) reads the current value, writes it into generated `src/viewer/Version.h` (`PF_VERSION_STRING` etc.) for THIS build, then persists `PATCH+1` back to `VERSION` for the next.
- **Reason**: A small, always-visible build number (status bar `v1.0.N`, plus the exe's Win32 file-version metadata) lets a tester/QA report exactly which build they saw without asking the dev, at zero manual-bump cost.
- **Gotcha**: `cmake -P <script>` runs with an ambiguous `CMAKE_CURRENT_SOURCE_DIR` unless explicit `-D` args are passed (it is NOT reliably the project root) — the custom-target `COMMAND` must pass `-DVERSION_FILE=...` / `-DOUTPUT_HEADER=...` explicitly (mirrors `embed_web.cmake`'s existing `-DDIST_DIR=...` pattern) or the script silently reads/writes the wrong path and the version never advances.
- **Gotcha**: the exe's VERSIONINFO resource resource ID must be a literal (`1 VERSIONINFO`), not the `VS_VERSION_INFO` macro — that macro is defined in `<winver.h>`, and without it rc.exe silently drops the entire version block (no error, no file-version metadata, `FileVersionInfo` reads all zeros).
- **Decision**: the built exe filename itself embeds the version — `ViitorXPCViewer.exe` -> `ViitorXPCViewer_v<MAJOR><MINOR><PATCH>.exe` (e.g. `_v102` for 1.0.2) via a `POST_BUILD` step (`tools/stamp_exe_name.cmake`) that parses `Version.h` and renames the just-linked exe, deleting any stale `_v*` copy from a previous build first.
- **Reason**: a distinguishable filename per build (not just an in-app label) makes it trivial to tell which build a tester is running from a taskbar/Explorer glance, or when several builds sit side by side in a releases folder.
- **Consequence**: this is safe to run unconditionally on every build (no relink-detection needed) because `main.cpp` `#include`s the generated `Version.h`, whose content changes every build — that alone forces MSVC to recompile `main.cpp` and relink `pfview`, guaranteeing a fresh unversioned exe exists for the rename step to consume each time. Downstream tooling that assumes a fixed `ViitorXPCViewer.exe` name (batch scripts' echoed run instructions, desktop shortcuts) needs a wildcard/glob instead of a literal filename.

## Embedded Web UI: ALL Custom Target Instead of OUTPUT-Keyed Custom Command
- **Decision**: `embed_web_header` (which runs `tools/embed_web.cmake` to generate `EmbeddedWeb.h` from `webremote/dist`) is a plain `add_custom_target(... ALL)` that always executes, not an `add_custom_command(OUTPUT EmbeddedWeb.h DEPENDS web_build ...)` gated on CMake's file-staleness detection.
- **Reason**: caught live — after several rebuilds spanning ~1 hour of UI changes, `EmbeddedWeb.h`'s mtime was stuck at an early point in the session while `webremote/dist`'s content-hashed filenames had moved on multiple builds. `web_build` (already an `ALL` target) reliably reran `npm run build` every time, but the *downstream* `OUTPUT`-based step depended on `web_build` by **target name**, not by file — a target name isn't a timestamp the VS generator can compare against, so it did not reliably detect that `dist/` had changed and could skip regenerating the header. The exe kept linking against a stale embedded web build even though every visible build log showed "Building web UI..." succeeding.
- **Consequence**: same always-run pattern as `web_build`, `version_header`, and the exe-rename `POST_BUILD` step — all four now execute unconditionally every build rather than relying on CMake/MSBuild timestamp-based staleness checks, which this codebase's fast-moving generated-file steps (version, web assets, exe name) have shown to be unreliable with the Visual Studio generator. Verified: `EmbeddedWeb.h`'s embedded hashed filenames now match `webremote/dist/assets/*` exactly on every build, with `EmbeddedWeb.h`/exe mtimes sequenced correctly after `dist/index.html`.

## Web Remote Desktop Input: Parallel Mouse/Keyboard Path Alongside Touch
- **Decision**: `FlyTab.tsx` gained a second input path — gated on `matchMedia('(pointer: fine)')` — running alongside the original touch path rather than replacing or unifying with it: left-drag look, right-drag pan, wheel zoom (reusing the existing Look/Pan/Zoom sensitivity sliders), WASD fly, Space/Ctrl up-down, Shift boost. Mouse-drag listeners attach at `window` level (not the stage element) so a drag keeps tracking even if the cursor leaves the viewport mid-drag.
- **Reason**: the component only ever wired `onTouchStart/Move/End` — a desktop browser opening the web remote had **zero** camera input (mouse-drag did nothing; only the UP/DOWN/BOOST buttons worked, since those use pointer events). Reported as "the Speed slider does nothing," which was a symptom, not the bug: with no baseline movement, there was nothing for the multiplier to scale.
- **Consequence**: two input paths must both stay correct rather than one — a future gesture change needs updating twice (touch handlers, then the mirrored mouse/keyboard logic) unless a shared abstraction is introduced later. Kept them separate for this pass since unifying via Pointer Events would have risked the working multi-touch pinch-zoom/pan logic, which needs raw multi-`Touch` tracking.

## Remote Tap-to-Measure: Reuse the Local Pick Ray, Don't Reimplement It
- **Decision**: measurement points can now be placed from the Web Remote by tapping the video while Measure is active. The client computes a normalized (0..1) tap position *within the actual video content* (inverting `object-fit:contain` letterboxing via a new `VideoLayer.getNaturalSize()`) and sends it as a `measure_pick` remote cmd; the server converts it straight to `pickX = nx*winW`, `pickY = ny*winH` and feeds the **existing** `pendingPick` -> `cam.screenRay` -> `store.pickPoint` path a local LMB click already uses.
- **Reason**: the video stream is a downscaled copy of the exact same rendered window (same camera, same aspect), so a normalized tap position maps 1:1 onto local screen-pixel pick math with no new ray-casting/projection code and no risk of the remote and local pick paths drifting apart.
- **Consequence**: picking accuracy depends on correctly locating the video's letterboxed content rect inside its container — a tap in a letterbox bar (empty space around the video from aspect mismatch) is silently ignored (returns `null`, no cmd sent) rather than picking the wrong point. While Measure is active, `FlyTab` suspends the primary pointer's look/pan (a short tap places a point instead; anything that moves more than `TAP_MOVE_THRESHOLD` is still treated as a drag) — mirrors the PC's existing "measure mode reclaims LMB" behaviour so the same input doesn't do two conflicting things.

## Remote Camera Input Mirrors the PC's Own Mouse Scheme (LMB Orbit vs RMB Look)
- **Decision**: the web remote's desktop mouse mapping was corrected to match `main.cpp`'s actual local scheme exactly — LMB-drag = orbit (rotates the camera *position* around a pivot), RMB-drag = free-look (rotates in place only), wheel = zoom, Q/E = down/up, Shift = boost — instead of an invented mapping (left=look, right=pan) that didn't correspond to anything on the PC. Orbit needed a genuinely new capability: the move message gained an `orbit: 0|1` field, and the server re-establishes the orbit pivot (`pivot = cam.position + cam.front()*dist`) on the rising edge of that flag exactly like the local `SDL_MOUSEBUTTONDOWN`/`SDL_BUTTON_LEFT` handler does, then calls the same `Camera::orbit()` every tick the flag stays set.
- **Reason**: free-look was already trivially replicable remotely (the existing `yaw`/`pit` fields already fed `Camera::addYawPitch`), but orbit moves the camera *position*, not just its look angle — there was no way to fake that client-side without the server's help, and "the web app should feel the same as sitting at the PC" only holds if the same mouse button does the same thing in both places.
- **Consequence**: `Camera::orbit()` already internally calls `addYawPitch` with identical units before repositioning around the pivot, so no unit conversion was needed between the free-look and orbit code paths — swapping which one runs is a single `if (remote.orbit())` branch. The client's "Pan" gesture (previously on right-drag) was dropped from desktop entirely, since the PC has no mouse-pan gesture at all — kept only for touch, where a 2-finger pan gesture has no PC keyboard-equivalent anyway.

## Web Remote Premium UI Redesign
- **Decision**: Overhaul the React UI into a premium, enterprise-grade interface.
- **Reason**: The initial UI looked like a developer settings page. A professional tool used by engineering firms requires a high-end, polished feel comparable to industry standards (e.g. Unreal Engine, Figma).
- **Consequence**: Implemented a desktop-style application shell (Toolbar -> Viewport/Inspector -> StatusBar). Completely decoupled from Tailwind/complex build pipelines by using a handcrafted CSS design system in `index.css` with CSS custom properties (`--bg #0D0F12`, `--accent #4DA3FF`). Used `lucide-react` for iconography. Retained all existing gesture logic and WebSocket protocol contracts unchanged.

## Medium-Feature Plan: Order and Key Approach Choices (docs/newdev.md)
- **Decision**: Adopted a code-anchored implementation plan (docs/newdev.md, living document with a status board) for the six mid-term features, in this order: (1) parallel indexer, (2) multi-client view-only role, (3) camera path + MP4 export, (4) cross-section/DXF export, (5) phone annotations, (6) multi-cloud scene.
- **Reason**: 1–2 are low-coupling with immediate user value; 3–4 reuse freshly verified infra (hardware MFT encoder, clipping + pickPoint traversal); 5 generalises the existing remote tap-to-measure pipeline; 6 has the largest blast radius (store/renderer/camera-space refactor) and goes last so it cannot destabilise the others mid-flight. Every plan section cites verified file/line anchors gathered by a scouted-and-cross-checked codebase survey (2026-07-06).
- **Key approach choices recorded in the plan**:
  - Parallel indexer: chunk builds become pure functions returning in-memory ChunkResults; a single coordinator thread does all global mutation (file append, hierarchy splice, offset rebase) — no locks on the hot path, format unchanged. A synthetic .xyz + round-trip test validates it against the sequential path (first automated test in the repo).
  - Video export: IMFSinkWriter MP4 muxing (new, self-contained VideoExporter) rather than reusing the WebRTC packetizer; NV12 conversion lifted out of RemoteServer into a shared helper; offline fixed-timestep FBO render loop decoupled from vsync; camera keyframes reuse the CamBookmark pose + per-cloud persistence pattern.
  - Cross-section: own minimal DXF R12 writer in pfcore (no new dependency — same spirit as the own PLY/text parsers); new OctreeStore::forEachPointInBox bulk query beside pickPoint; exports use WORLD coordinates (centred-space offset added back).
  - Multi-client: role decided by which of two PINs the client presents (driver PIN vs viewer PIN); viewer role's move/cmd/set silently ignored server-side; stream never exposed unauthenticated.
  - Annotations: JSON (versioned) per-cloud persistence instead of TSV — labels are free text; pins drawn as GL geometry inside renderPass so they appear in the remote stream and exports.
  - Multi-cloud: one PointRenderer per cloud (sidesteps node-index collisions; matches the proven multi-instance Unity C API shape); scene origin = first cloud's cube centre with per-cloud double offsets applied at draw time to preserve GPU float precision.

## Parallel Indexer: In-Order Coordinator + Flat Cell Set (branch parallel-indexer)
- **Decision**: Phase C parallelism uses pure-function chunk builds (each worker returns a self-contained ChunkResult: chunk-local NodeRecords + payload blob + coarse samples) drained by a single coordinator thread strictly IN CHUNK ORDER, which rebases indices/offsets and does every global mutation (fwrite, hierarchy splice, chunkRoots, coarse). In-flight results capped at threads+2.
- **Reason**: in-order draining makes the parallel output BYTE-IDENTICAL to the sequential build — verified by the new pftest round-trip test and sha256 on a real 12.4M-point LAS. That determinism turns the whole feature into a checksum-testable refactor and keeps hierarchy.bin reproducible across machines/thread counts. The head-of-line cost is negligible because chunk build times are similar and the cap parks at most a few results.
- **Finding recorded**: with compression OFF, Phase C wall time is dominated by the serial payload fwrite (~270 MB for the test scan) — parallelism can't help disk writes, so the speedup (2.9× measured) appears where CPU dominates: compressed presets (Balanced/High default compress=on) and deep subtrees. Do not expect linear scaling on write-bound configs.
- **FlatCellSet**: `subsample`'s `std::unordered_set<uint64_t>` allocated a heap node per inserted grid cell; under N workers those mallocs serialized on the Windows process-heap lock and erased the parallel gain (observed: threads=16 slower than threads=1 before the fix). Replaced with a single-allocation open-addressing set (linear probe, splitmix64 finalizer, 0.75 load factor; cellKey uses 63 bits so all-ones is a safe empty marker). Also speeds up sequential builds (~20% on the bench scan).
- **Testing**: pftest (PF_BUILD_TESTS, CTest `octree_roundtrip`) generates a deterministic synthetic .xyz (fixed LCG — never std::random), converts sequentially and in parallel for compress on+off, asserts byte-identity of hierarchy/octree/meta and DFS structural invariants (single-visit reachability, childMask consistency, payload ranges, point-count conservation) via OctreeStore.

## Web Remote Roles: Second PIN, Server-Side Gating (branch multi-client-roles)
- **Decision**: Watch-only access is granted by a SECOND 4-digit PIN (regenerated each server start, guaranteed distinct from the driver PIN). Role is decided at hello time and echoed in hello_ok ({"role":"driver"|"viewer"}); the server silently ignores move/cmd/set from viewer-role clients while allowing stream/webrtc subscription and state/cfg broadcasts. /shot.png accepts either PIN.
- **Reason**: gating on the server is the security boundary — the web UI hiding controls is UX, not enforcement (a forged move message from a viewer must be a no-op). Two PINs keep the protocol unchanged (same hello message) and need no session/user concept. The stream is never exposed unauthenticated, same reasoning as the /shot.png gate.
- **Out of scope (recorded)**: driver arbitration stays last-write-wins when multiple drivers connect; per-client stream quality (streamW/fps/q are global encoder settings — a viewer toggling its own stream subscription is fine, but quality changes affect everyone). Both are follow-ups if multi-user use grows.
- **Build note**: the overlay-triplet toolset pin MUST be passed as an ABSOLUTE path (-DVCPKG_OVERLAY_TRIPLETS=<abs>/triplets). A stale relative "./triplets" in CMakeCache silently stops resolving after reconfigure, vcpkg rebuilds ports with the newest installed toolset (14.51) and pfview fails to link against the 14.44 CRT — the exact LNK2019 __std_* signature documented in the single-file-release decision. Fix: wipe build-static/vcpkg_installed and reconfigure with the absolute overlay path.

## Camera Path + MP4 Export: In-Frame Incremental Export, SinkWriter, LOD Settle (branch camera-path-export)
- **Decision**: MP4 muxing/encoding uses a new self-contained `VideoExporter` wrapping `IMFSinkWriter` (`MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS`, NV12 in → H.264 High MP4 out) rather than reusing the WebRTC live-stream encoder path; only the RGB→NV12 conversion is shared, lifted out of RemoteServer.cpp into `src/viewer/Nv12.h` so both consumers produce identical colour (BT.601). VideoExporter owns its COM/MFStartup lifetime (balanced, tolerant of the app's existing apartment); sample timestamps are computed from the frame INDEX (`i*10^7/fps`), not accumulated, so rounding never drifts; `abort()` skips Finalize and deletes the partial file (a moov-less MP4 is useless).
- **Reason**: the Sink Writer does its own MFT selection (hardware when available) and container muxing in one object — reusing the WebRTC packetizer would have coupled the offline export to the live-stream encoder's async event pump and per-client state for zero gain.
- **Decision**: the offline render loop is an INCREMENTAL in-frame job, not a blocking modal loop: each viewer frame renders as many export frames as fit a 30 ms budget into an export-sized FBO pair (scene colour+depth, then the same EDL post pass), reads back RGB, converts, writes. The UI keeps pumping (progress modal + Cancel; Esc cancels too), the camera is saved/restored around the export, and `remote.wantFrame()` is suspended for the duration (stale backbuffer + encoder contention).
- **Decision — LOD settle**: an export frame is only written once every frustum-visible node at that pose is GPU-resident (drawn == visible from the traversal stats), re-rendering the same frame across viewer frames while streaming catches up — capped at 240 attempts, after which the frame is written anyway with a logWarn (a stuck load must not hang the export forever). An extra 512-result absorb loop runs per viewer frame during export so decoded nodes reach the GPU faster than the normal `uploadsPerFrame` trickle.
- **Reason**: the interactive LOD/streaming policy optimises for responsiveness, not completeness — without settling, fast path segments export with visibly missing detail that the viewer would only stream in after the camera had already passed.
- **Decision — keyframe interpolation** (`CamPath.h`): non-uniform Catmull-Rom via finite-difference tangents on every continuous channel (position, yaw, pitch, orthoSize) — handles unevenly spaced key times without overshoot; yaw is unwrapped shortest-way across the four control points (no 350°→10° long spin); pitch clamps ±89 at sample time; the ortho FLAG is a step channel (previous key holds). Paths persist per-cloud in AppData `campaths.txt` (TSV + `# pf-campath 1` version header — same keying/pattern as bookmarks.txt; poses are centred-space like CamBookmark).
- **Smoke hook**: `--export-video <out.mp4>` (same spirit as `--convert`) synths a 3-key 120° orbit path over the loaded cloud and drives the exact Export-dialog code path, quitting when the export ends — the camera-path pipeline is exercisable headlessly. Verified: Tikal-13 (12.4M pts) → 1080p30, 121 frames, ffprobe-clean.

## Cross-Section / Slice Export: CPU Box Visitor + R12 DXF + Main-Thread PNG (branch cross-section-slice-export)
- **Decision**: Add `OctreeStore::forEachPointInBox(AABB, maxDepth, cancel, visitor)` beside `pickPoint`, reusing `readNodeInto` for compressed/uncompressed node payloads and unpacking points into WORLD coordinates before the callback. A companion `estimatePointsInBox` sums intersecting node payload counts without disk reads for UI preflight/progress. The exact writer path streams directly through the visitor rather than materializing the slice in RAM.
- **Reason**: Slice exports can be GB-scale; keeping the API callback-based preserves the viewer's CPU/GPU separation and avoids a second resident point buffer. The depth cap gives a predictable density/LOD control while retaining the MNO model: node payloads are included up to and including the chosen depth.
- **Decision**: DXF export is a minimal ASCII DXF R12 writer in pfcore (`src/io/DxfWriter.{h,cpp}`), emitting `POINT` entities grouped by classification layer plus a R12 `POLYLINE` slice outline. Coordinates are projected from the current clip box onto its thinnest-axis plane and written in source WORLD coordinates, not centered render coordinates.
- **Reason**: R12 ASCII imports broadly into CAD tools and keeps the feature dependency-free. Using R12 `POLYLINE` rather than `LWPOLYLINE` preserves the chosen file-version target.
- **Decision**: CSV export uses the same CPU visitor and writes `x,y,z,r,g,b,intensity,classification`; PNG export stays on the main/render thread and reuses the existing export-sized FBO + EDL post path with an orthographic camera aligned to the slice normal.
- **Reason**: CSV/DXF are disk/CPU work and can safely run on a background worker with cancel support. PNG needs OpenGL state and must run on the render thread; it is represented as a Jobs-panel item, waits for visible nodes to settle, restores camera/clip state after each attempt, and writes via the existing in-memory PNG encoder.
- **Decision**: keep a permanent `--export-slice <prefix>` smoke hook in `pfview`, analogous to `--convert` and `--export-video`. It loads the first positional octree, chooses a central metadata slice, exports `<prefix>.dxf`, `<prefix>.csv`, and `<prefix>.png`, then exits when all three jobs finish.
- **Reason**: slice export spans CPU file writers and the main-thread GL PNG path; the hook makes real-model validation repeatable without requiring manual UI clicks.
- **Validation**: static `pfview` build succeeds; `pftest` exercises exact boxed traversal on the synthetic cluster and CTest `octree_roundtrip` passes. Real model `C:\UnrealProject\model\PointForgeCache_direct` exported DXF/CSV/PNG through `--export-slice`; CSV rows and DXF `POINT` entities both **871,420**, DXF header is R12 (`AC1009`) and ends with `EOF`, CSV world-coordinate range is inside the selected thin slice (`z=314.847..318.784`), and PNG is valid **1920×2160** and visually populated.

## Phone Annotations: Reuse Tap Picking, Versioned JSON, Stream-Visible Pins (branch annotations-from-phone)
- **Decision**: Add `TOOL_ANNOTATE = 3` beside Navigate/Measure/Clip. Local LMB and phone taps both resolve through the same `cam.screenRay` -> `OctreeStore::pickPoint` path used by measurement; the web client sends `anno_pick` with normalized video coordinates, mirroring `measure_pick`.
- **Decision**: Persist annotations per cloud in AppData `annotations.json` (`version: 1`, keyed by cloud directory), rather than TSV. Labels are free text and are sanitized for tabs/newlines/length before save.
- **Decision**: Extend `RemoteCmd` with `text` for label edits and publish `RemoteConfig.annotations` as `{p,label}`. Rename/delete/goto are one-shot commands: `anno_label` (`v=index,text=label`), `anno_del`, `anno_goto`.
- **Decision**: Draw annotation pins and leader marks as GL geometry inside `renderPass`, so they are baked into the same FBO captured by JPEG/WebRTC streaming, screenshots, and video export. Full text labels remain in the PC ImGui overlay and phone/PC lists for v1 because the viewer has no GL text renderer in the render pass.
- **Validation**: `npm run build`; `cmake --build build-static --config Release --target pfview`; `ctest --test-dir build-static -C Release --output-on-failure` all pass. Build stamped `ViitorXPCViewer_v1023.exe`.

## Multi-Cloud Scene: `std::vector<SceneCloud>` and Global Origin (branch multi-cloud-scene)
- **Decision**: Refactor the global `store` and `renderer` into a `std::vector<SceneCloud> scene` in `main.cpp`, where `SceneCloud` encapsulates an `OctreeStore` and `PointRenderer`. The scene origin is implicitly defined by the first loaded cloud (`scene[0].store.cubeCenter()`), and all rendering/picking logic operates relative to this origin.
- **Reason**: Allows loading and rendering multiple point clouds simultaneously without significant changes to the renderer or camera math, as long as they are close enough in world space to avoid floating-point precision issues relative to the primary origin.
- **Decision**: Convert job completion logic uses a modal dialog to prompt the user to either "Replace" the current scene or "Add" the newly converted cloud to the scene, using a temporary `pendingLoadDir` to hold the conversion result while the dialog is visible.
- **Decision**: Status bar totals (point count, GPU memory, loading pending requests) and Remote Config broadcast (`rc.pointCount`) are now aggregated across all active `SceneCloud` instances in the `scene` vector.

## Multi-Cloud Completion + Audit Round (finished on branch vxpc/thumbnails)
- **Decision**: the remaining #6 remote surface follows the bookmark/annotation additive-cfg pattern: `RemoteConfig::Cloud{name,pts,visible}` rows serialized as `clouds:[...]` in every cfg broadcast, and one new cmd `cloud_vis` with `v=[index,on,0]` (reuses the existing 3-float vec payload — no protocol shape change). The webremote Tools tab shows a "Scene" card only when 2+ clouds are resident; loading/closing clouds stays PC-only, and the viewer-role gate (RemoteServer.cpp `watchOnly` check) covers `cloud_vis` for free because it blocks ALL cmd/set before parsing.
- **Audit findings fixed (2026-07-07 audit of features 1-6)**:
  - Commit `1d68eaf`'s status-bar aggregation did not compile (`.` member access on the `unique_ptr` store/renderer) — the branch tip was unbuildable, contradicting its own handoff note; and once fixed, the aggregated `totalMB`/`totalPts` were computed but never displayed (stats cluster still showed only the active cloud). Both fixed; status bar is now scene-wide.
  - CSV slice export wrote coordinates at the ostream default 6 significant digits (`1.90606e+06` — ~10 m granularity on georeferenced data, useless for survey work); now `setprecision(15)`. The DXF writer was unaffected (fixed-notation `%f`).
  - Scene-space vs active-cloud-centre mixups — `currentSliceBox`, the slice-PNG ortho framing, `gotoAnnotation`, and the measure/annotation label screen projection all converted world<->camera space through `activeStore().cubeCenter()`; the camera lives in SCENE space (origin = FIRST loaded cloud's centre), so each was wrong by the active cloud's `worldOffset` whenever a non-first cloud was active. All now use `sceneOrigin`. clip bounds are documented as scene-space (the per-cloud render already subtracted `worldOffset`).
  - `OctreeStore` gained a `unique_ptr<PackageReader>` member (vxpc work) with only an implicit constructor — any TU constructing an OctreeStore then needed the complete `PackageReader` type (C2027 in main.cpp). Fixed by declaring the constructor in the header and defaulting it in OctreeStore.cpp, the standard pimpl-member idiom (the destructor was already out-of-line).
- **Validation**: `npm run build` clean; pfview+pftest build clean (v1038); CTest `octree_roundtrip` pass; `--export-video` smoke on Tikal-13 → ffprobe-clean 121-frame 1080p30 H.264. NOT yet done: live two-cloud acceptance walk (fly/measure/toggle across two real scans side by side) — no second converted scan staged on this machine.

## Photogrammetry: Orchestrated External Engines, Not Embedded (branch feat/photogrammetry)
- **Decision**: Photos -> point cloud is done by driving external reconstruction engines as child processes ("Level-2" orchestration): ODM via Docker (`opendronemap/odm`) and COLMAP (pinned 4.1.0 official release zip, installed privately under `%LOCALAPPDATA%\ViitorX\PointForge\engines`). No reconstruction library is linked into pfview - `src/viewer/Photogrammetry.{h,cpp}` owns detection, consent-gated auto-install, process spawn/cancel and stage-keyword progress parsing; `Jobs.h` gains `Kind::Photogrammetry` (reconstruction maps to 0-70% of the job bar, then chains into `buildOctree` for 70-100%) and `Kind::EngineSetup` (one-time install job).
- **Reason**: engines are huge and toolchain-coupled (COLMAP drags Ceres/Boost/CUDA; ODM is a Python pipeline that realistically only ships as a Docker image) and they improve fast. Orchestration preserves the single-file-exe property (only system tools are spawned: curl/tar/winget/docker) and swapping or upgrading an engine is a command-line change, not a rebuild.
- **Engine choice**: an own minimal EXIF parser (bounded 128 KB head reads; requires actual GPSLatitude/GPSLongitude entries in the GPS IFD, since some cameras write an empty GPS IFD) samples up to 24 JPEGs per folder. GPS tags -> ODM (georeferenced, metric-scale LAZ straight from the tags); no GPS or no NVIDIA GPU -> COLMAP (its dense MVS needs CUDA). This is only the recommendation - the wizard picker always offers both, with the trade-offs in tooltips.
- **Hardware fallback**: Docker requires VT-x/AMD-V. Probe = `IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED)` OR the CPUID hypervisor bit (an active hypervisor consumes the firmware flag, so both signals are needed). Both false -> the setup job soft-skips ODM (COLMAP alone still counts as success), the recommendation flips to COLMAP, and the wizard status line + help text show the exact BIOS steps to unlock ODM later. Fail-fast by design: never install Docker Desktop and then poll a daemon that can never start.
- **Consequence**: the COLMAP path outputs arbitrary scale/origin (GPS unused) - stated in the UI where it matters for the measure tool. COLMAP's glog prefixes (`I0713 ... file.cc:315]`) are stripped from wizard/job progress lines; raw lines still reach the Console via logDebug. The reconstruction workspace (`<out>\<stem>_photogrammetry`) is kept on success for inspection of the raw engine output.

## Photogrammetry Follow-ups: BIOS Help via Registry, 4.1.0 Validated (branch feat/photogrammetry)
- **Decision**: The "enable virtualization" help shown when ODM is blocked is board-specific: motherboard vendor/product/BIOS version are read from the registry (`HKLM\HARDWARE\DESCRIPTION\System\BIOS`) rather than WMI/COM (no initialization, no extra dependency, works in the static exe), and the steps come from a small static vendor table (Gigabyte/ASUS/MSI/ASRock/Dell/HP/Lenovo/Acer + generic fallback) with the setting name picked by CPU vendor id (`AuthenticAMD` -> "SVM Mode", else "Intel VT-x"). A "Search the web" button opens the default browser on a board-specific query - deliberately NOT live web scraping inside the app (fragile, offline-hostile); the curated table covers the common cases and the search link covers the tail.
- **Validation (2026-07-14)**: COLMAP 4.1.0 CLI verified against the installed binary (`automatic_reconstructor --help`): `--workspace_path/--image_path/--quality/--dense` and `model_converter --output_type PLY` all exist unchanged from 3.x. End-to-end chain proven on a 25-image DJI subset: dense GPU reconstruction -> 157,045 fused points at `dense/0/fused.ply` (the path the job searches) -> pfconvert -> 6.72 MB `.vxpc`, 281 nodes, zero point loss. Remaining: a full 271-image run (hours) and the ODM path once VT-x is enabled in this machine''s BIOS.
