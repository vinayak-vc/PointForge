# Tasks

## Completed
- `[x]` Basic Viewer and Importer functionality
- `[x]` Fix CMake linking issues
- `[x]` Implement multiple Coloring Modes (True Color, Elevation, Solid)
- `[x]` Add dynamic clipping planes
- `[x]` Add Ortho / Perspective toggle and Camera Presets
- `[x]` Modify application name to "ViitorX PointCloud Viewer" and insert watermark
- `[x]` Build Viewer as a standard WIN32 executable to hide the console terminal
- `[x]` Add system SFX / Beeps on load completion
- `[x]` Persistent saving and loading of Viewer UI settings locally
- `[x]` Documentation update (`AGENTS.md` standardization)
- `[x]` CPU Point Picking + distance measurement (synchronous on-click disk read; no CPU-resident buffer)
- `[x]` Cache purge of stale streaming requests (frame-stamped queue + ready cap)
- `[x]` ImGui DPI scaling (auto-detect + persisted UI Scale slider)
- [x] Doc drift fix: `NodeRecord` 32->52 bytes, `PackedPoint` 20->22 bytes
- [x] Single-file static release `ViitorXPCViewer.exe` (embedded shaders+icon)
- [x] Route configuration (`pfview_config.txt`) persistence to user's AppData directory via SDL_GetPrefPath
- [x] Windows File Explorer integration (embed `vx.ico` via `app.rc` Resource Script)
- [x] Render watermark correctly in Stereoscopic SBS mode with a 3D negative parallax pop-out effect
- `[x]` Navigation UX: orbit (LMB), double-click focus, wheel zoom-to-cursor, frame-all
- `[x]` Always-on status bar + F1 help overlay + slider tooltips
- `[x]` Multi-segment polyline measure (per-segment + total, snap, undo/clear/copy)
- `[x]` Quality preset + color-by Intensity + Classification + colour-bar legend + theme toggle
- `[x]` Eye-Dome Lighting (EDL) post-process (FBO + fullscreen pass)
- `[x]` Recent files (MRU) + auto-load last + convert Cancel button
- `[x]` Screenshot (F12, BMP), reset confirmation, clear-clipping, top toolbar
- `[x]` Controller support: Xbox gamepad (SDL_GameController) + raw joystick fallback
        (camera, actions, ImGui UI-nav mode, deadzone/sens config, live rebind panel)
- `[x]` Custom ESP32 Bluetooth-SPP controller (Win32 serial reader, MAC auto-detect):
        joystick=look, trigger=fly-forward, PAUSE=UI mode, PLAY=activate/frame-all
- `[x]` Docked UI shell redesign: menu bar + toolbar + central passthru viewport +
        Properties (right dock, collapsing sections) + Jobs/Console/Performance
        (bottom dock, closed by default) + status bar; ImGui repinned to
        `v1.91.5-docking`
- `[x]` Conversion moved to a background `JobQueue` (`src/viewer/Jobs.h`) — Convert
        dialog enqueues, Jobs panel + status-bar pill + completion toast monitor;
        never blocks the viewport
- `[x]` Log fan-out: `pf::setLogSink()` mirrors all `pf::log()` calls into a
        Console panel ring buffer (`src/viewer/UiLog.h`), filterable by severity
- `[x]` F1 searchable keyboard-shortcut sheet (single source-of-truth table) +
        Ctrl+P command palette
- `[x]` Controller/ESP32 config moved out of the daily panel into a Preferences
        dialog (Edit > Preferences); Convert dialog params collapsed under
        Advanced with `(?)` tooltips (replacing inline gray description text,
        and fixing copy-pasted Flush/KeepChunk descriptions)
- `[x]` Stereoscopic SBS (F9) now hides all UI chrome — menu/toolbar/docks/status
        bar/watermark/overlays all suppressed; per-eye fading exit hint only
- `[x]` Single-file static build toolset-mismatch fixed via overlay-triplet
        `VCPKG_PLATFORM_TOOLSET_VERSION` pin (`triplets/x64-windows-static.cmake`) —
        no Ninja/vcvars dev-shell step needed; re-verified zero non-system DLL
        imports and a clean launch isolated in an empty directory

- `[x]` Unity native plugin (branch `library/unity`): `pfunity` DLL target +
        flat C API (`src/library/unity/PointForgeC.{h,cpp}`) — streaming reader
        only, pfcore untouched; smoke-tested against a real 12.4M-pt octree

## In Progress
- `[ ]` Web remote controller (branch `webapp-controller`) — phone browser drives
        camera + viewer options over LAN; embedded civetweb HTTP+WS server in
        pfview serves a React control page from `web/` beside the exe.
        Plan phases:
        - `[x]` Phase 0: deps — vcpkg civetweb port ships WS enabled
                (`CIVETWEB_ENABLE_WEBSOCKETS=ON` in portfile); civetweb 1.16 +
                nlohmann-json 3.12 added to vcpkg.json; qrcodegen vendored
                (`src/viewer/qrcodegen.{hpp,cpp}`, MIT); `PF_WITH_REMOTE` guard
        - `[x]` Phase 1: `src/viewer/RemoteServer.{h,cpp}` — pimpl keeps
                civetweb/json out of headers; atomics input, mutex cmd queue,
                5 Hz state broadcast (200 ms throttle), PIN handshake (3 tries),
                500 ms move-staleness → axes read zero; stub build when deps absent
        - `[x]` Phase 2: main.cpp — remote axes applied after serial block, cmds
                → existing flags, Preferences > Input "Web remote" section,
                "Connect phone" QR modal (qrcodegen → ImDrawList), settings
                persisted (remoteEnabled/remotePort)
        - `[x]` Phase 3: React app `webremote/` (Vite5+React18+TS strict) — dual
                joystick, up/down/boost holds, action bar, HUD, PIN screen,
                33 ms move loop w/ trailing zero frame, backoff reconnect, wake
                lock; `vite build` clean, dist 152 KB
        - `[x]` Phase 4a: CMake post-build copy `webremote/dist` → `web/`;
                localhost smoke test passed: GET / 200, WS 101 upgrade,
                hello_bad/hello_ok, state @5 Hz, hideui flips `ui` in state,
                disconnect handler zeroes axes
        - `[x]` Phase 4b: physical-phone end-to-end test (same WiFi): QR scan,
                PIN, fly with both sticks (verify pitch/yaw sign feel), all
                commands, phone-sleep disconnect safety, `hideui` clean-screen
                flight; Windows Firewall private-network allow on first run
        - `[x]` Phase 5: viewport JPEG streaming — libjpeg-turbo (vcpkg) encode
                thread in RemoteServer, backbuffer blit→downscale→readback
                post-EDL/pre-UI in main.cpp, binary WS frames on same socket;
                client opts in `{"t":"stream","on":1,"w","fps","q"}`; verified
                FF D8 JPEG frames end-to-end (WebRTC evaluated, rejected for
                v1: libwebrtc/GStreamer too heavy; libdatachannel+MF = phase 2)
        - `[x]` Phase 6 (C++ side): FULL viewer control — protocol v2 `set.<key>`
                for every Properties control (display/camera/stereo/clip/UI
                toggles) + cmds (reset_view, fullscreen, measure_undo/clear,
                clip_tool/reset, loadrecent) + `{"t":"cfg"}` snapshot broadcast
                (1 Hz + instant echo, incl. measure pts, recent files, bounds);
                debounced settings save; verified cfg keys + set echo live
        - `[x]` Phase 7 (WebRTC & Mobile UI): Integrated libdatachannel + Microsoft 
                Media Foundation for low-latency H.264 WebRTC streaming to the 
                browser. Replaced visual joysticks with multi-touch gestures 
                (single-tap look, double-tap pan, pinch zoom).

## To Do
- `[ ]` Orthographic screen-space-error variant in the C API (Unity scene
        cameras can be ortho; streaming currently pauses in ortho views)
- `[ ]` Improved caching: auto-purge old converted-cloud cache dirs on disk
- `[ ]` UI font re-rasterization at scale (current DPI path scales metrics + FontGlobalScale only)
- `[ ]` Linearised-depth EDL (current uses raw depth diff; tune for ortho)
- `[ ]` Convert cancel for Phase A/B (currently aborts at Phase C chunk boundaries)
- `[ ]` VR/OpenXR initialization support
- `[ ]` Multi-select batch conversion in the Convert dialog (queue already supports N jobs)
- `[ ]` Workspace layout presets (Window menu: save/restore named DockBuilder layouts)
- `[ ]` Recent-files pin/unpin + cached point-count/size metadata
- `[ ]` Scene panel (multi-cloud) — deferred until multi-cloud rendering exists
