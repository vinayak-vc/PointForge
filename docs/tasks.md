# Tasks

## Completed
- `[x]` Basic Viewer and Importer functionality
- `[x]` Fix CMake linking issues
- `[x]` Implement multiple Coloring Modes (True Color, Elevation, Solid)
- `[x]` Add dynamic clipping planes
- `[x]` Add Ortho / Perspective toggle and Camera Presets
- `[x]` Camera preset fix: Front/Side/Top used world-space cubeCenter in the
        centred camera space (model lost by the cloud's georeferenced offset)
        and yaw/pitch that never faced the model; now `presetView(dir)` =
        frameAll-fit distance + `lookAt(origin)` + orbit-pivot reset
- `[x]` Modify application name to "ViitorX PointCloud Viewer" and insert watermark
- `[x]` Guided fullscreen Conversion Wizard — Source → Quality → Destination → Convert
        → Done. Replaces the compact "Convert to Octree" dialog; a fullscreen ImGui
        modal blocks the menu/toolbar/docks so a conversion can't be disturbed mid-flow.
        Triggered by the Convert button, Ctrl+I, or dropping a LAS/LAZ/E57/PLY/PTS/XYZ file.
- `[x]` Renamed remaining user-facing "PointForge" strings to "ViitorX PointCloud Viewer"
        (welcome panel title, Help > About menu item + About dialog). Internal names
        (pfcore/pf::/format magic) and the AppData settings folder are unchanged.
- `[x]` Premium brand watermark — vx.svg rasterized (cairosvg) + alpha-aware 2px
        Gaussian blur (PIL) into an embedded RGBA header (`src/viewer/EmbeddedWatermark.h`,
        regen via `scratch/gen_watermark.py`). Drawn centred at ~70% height, ~6% opacity,
        original colours, behind everything (no interaction) with a subtle dark vignette,
        on the empty/welcome state and inside the conversion wizard.
- `[x]` Conversion cancel now covers Phase A/B — `runChunker` takes an optional
        `cancel` flag (checked per read batch in the bounds + chunking loops); buildOctree
        forwards `opts.cancel` and distinguishes user-cancel from error. The wizard's
        Cancel button now stops during Scanning/Chunking, not only the final index phase.
- `[x]` Conversion wizard shows an estimated time remaining on the Converting step
        (SDL_GetTicks rate on overall progress).
- `[x]` Welcome "Recent" list caches + shows each cloud's point count and on-disk size
        (persisted in `pfview_config.txt` as `recent=path\tpoints\tbytes`, back-compatible).
- `[x]` Orphaned `<out>/chunks` temp dirs from a crashed/killed conversion are purged at
        the start of the next conversion to the same target (scoped to the dir buildOctree owns).
- `[x]` Open dialog handles both `.vxpc` packages and octree folders — the primary
        "Open Cloud..." (Ctrl+O) file picker has two distinct filter choices: "VXPC Package
        (*.vxpc)" (default) and "Octree Folder (meta.bin)". Picking a `.vxpc` loads it
        directly; picking a folder's `meta.bin` (or `hierarchy.bin`/`octree.bin`) loads that
        folder (native dialogs can't select a file and a folder in one picker). A dedicated
        "Open Octree Folder..." folder picker also remains.
- `[ ]` Finish at-rest encryption in the viewer — DEFERRED by decision: `octree.bin` (the
        points) can't be encrypted with the current per-entry-GCM + raw-seek streaming design,
        so `--encrypt` would protect metadata only (misleading). Needs per-node payload
        encryption first (a larger format/streaming change). See decisions.md.
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
- `[x]` Orbit snapping fix: Left-Click orbit after Right-Click look or WASD move
        no longer snaps violently to the old stale pivot; the pivot is dynamically
        projected onto the current forward axis.
- `[x]` Always-on status bar + F1 help overlay + slider tooltips
- `[x]` Multi-segment polyline measure (per-segment + total, snap, undo/clear/copy)
- `[x]` Quality preset + color-by Intensity + Classification + colour-bar legend + theme toggle
- `[x]` Eye-Dome Lighting (EDL) post-process (FBO + fullscreen pass)
- `[x]` Recent files (MRU) + auto-load last + convert Cancel button
- `[x]` Screenshot (F12, BMP), reset confirmation, clear-clipping, top toolbar
- `[x]` Screenshot upgrade: PNG (vendored stb_image_write) saved to
        `<Pictures>\ViitorXPC\shot_<timestamp>.png` (never beside a
        Program-Files exe), copied to the Windows clipboard as a DIB, toast +
        Console log; last capture kept in RemoteServer and served at
        `/shot.png?pin=<PIN>` (PIN-gated) with a `shot_ready` WS notify
- `[x]` Camera bookmarks: named poses (centred-space position + yaw/pitch +
        ortho), persisted per-cloud in AppData `bookmarks.txt` (TSV); UI in
        Properties > Camera (goto/delete/add-with-name); names broadcast in
        cfg, remote cmds `bookmark_add`/`bookmark_goto`/`bookmark_del`
- `[x]` Multi-select batch conversion: Convert dialog Browse uses
        `openFileDialogMulti` (FOS_ALLOWMULTISELECT); N>1 shows a scrollable
        remove-list + total size, output dir becomes the parent (each file →
        `<out>\<stem>_octree`), one JobQueue job per file, load-when-done
        forced off for batches
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
- `[x]` Auto-incrementing build version: `VERSION` file (patch bumps every
        build via `tools/bump_version.cmake` -> generated `src/viewer/Version.h`);
        shown small in the C++ status bar (`v1.0.N`) and embedded as the exe's
        Win32 FILEVERSION/ProductVersion (`app.rc`, `1 VERSIONINFO` — NOT
        `VS_VERSION_INFO`, which is undefined without `<winver.h>` and silently
        drops the whole resource)
- `[x]` Build output filename embeds the version: `ViitorXPCViewer.exe` ->
        `ViitorXPCViewer_v<MAJOR><MINOR><PATCH>.exe` via a `POST_BUILD` step
        (`tools/stamp_exe_name.cmake`) that renames the freshly-linked exe and
        removes any stale `_v*` copy from a previous build
- `[x]` Web Remote toolbar consolidated to icon-only buttons
        (`.toolbar-btn--icon`/`--compact`) so the full control set fits
        without horizontal scroll; dropped the scroll/fade-mask CSS
- `[x]` Web Remote desktop mouse+keyboard camera input (`FlyTab.tsx`):
        left-drag look, right-drag pan, wheel zoom, WASD fly, Space/Ctrl
        up/down, Shift boost — a desktop browser previously had no camera
        input at all (touch-only)
- `[x]` Web Remote remote tap-to-measure: tapping the video while Measure is
        active sends normalized coords through a new `measure_pick` cmd;
        server inverts the same screen-ray pick a local LMB click uses.
        `VideoLayer` gains `getNaturalSize()` to correctly invert the video
        layer's `object-fit:contain` letterboxing
- `[x]` Elevation/intensity colour legend UI fixes (C++ viewer + React webremote):
        legend now dock-aware (anchors left of "Properties" only when it truly
        abuts the window's right edge — not for left-docked/floating panels),
        stays visible in zen/hide-UI mode, renders once per eye with the
        watermark's negative-parallax offset in stereoscopic SBS; title width
        now reserved in the layout (previously "Intensity" could overshoot past
        the bar+labels' reserved width and bleed across the SBS eye split).
        Color-mode control replaced with a `Select` dropdown (`controls.tsx`,
        `DisplayTab.tsx`) since only one mode is ever active; new
        `webremote/src/Legend.tsx` viewport overlay mirrors the C++ ramp (new
        additive `zmin` cfg key, `RemoteConfig.zMin` -> `{"zmin":...}`). CSS UX
        pass: `.seg` flex->grid (fixes clipped "Intensity"/"Classification"
        options), safe-area insets, `100dvh`/`46dvh`, coarse-pointer target
        bump, mobile toolbar dedup (`.toolbar-group--dup`), connect-screen
        scroll fix, `--text-3` contrast bump, typeable PIN input, lucide icons
        replacing emoji in the pinch-mode toggle
- `[x]` Cross-section / slice export (branch `cross-section-slice-export`):
        current clip box exports as DXF, PNG, or CSV. Implemented
        `OctreeStore::forEachPointInBox` + `estimatePointsInBox`, a pfcore
        ASCII DXF R12 writer, CSV streaming writer, Clip-panel "Export
        slice..." dialog, Jobs-panel progress/cancel/reveal, PNG export
        through the existing offscreen render + EDL path, and permanent
        `--export-slice <prefix>` smoke hook. Real-model validation:
        `C:\UnrealProject\model\PointForgeCache_direct` exported DXF/CSV/PNG;
        CSV rows and DXF POINT entities both 871,420; PNG valid 1920×2160 and
        visually verified.

## In Progress
- `[x]` Web remote controller (branch `webapp-controller`) — phone browser drives
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
        - `[x]` Phase 7 fix (client signaling): WebRTC answer/ICE never reached
                the RTCPeerConnection (stale no-op `onWebRTC` captured in the
                useWebSocket options object), server trickle-ICE arrived as
                `webrtc_candidate` but client only handled `webrtc_ice`, and
                remote candidates could race setRemoteDescription. Fixed in
                webremote (ref-based handler wiring, accept both ICE message
                names, pending-candidate queue, offer now carries `type`).
        - `[x]` Phase 7 fix (server answer): libdatachannel matches tracks by
                mid — hardcoded mid "video"/PT 96 never matched the browser
                offer (mid "0", browser's own H264 PT), so the answer rejected
                the video m-line. Server now mirrors the offer's mid + H264
                payload type + fmtp. Mobile autoplay fixed (imperative muted +
                gesture fallback); webrtc console diagnostics added.
        - `[x]` Phase 8: Premium UI Redesign — Completely overhauled the React frontend
                to an enterprise-grade UI using CSS custom variables, Lucide icons,
                and a clean desktop-like app shell (Toolbar, Workspace, Status Bar).
                Replaced native inputs with styled sliders, segmented controls, and
                iOS-style toggles.
        - `[x]` Phase 9: Virtual File System (`.vxpc`) Architecture — Designed and
                implemented the primary `.vxpc` PointForge native archive format.
                (Phases 1-6, 12, 15, 16 completed: ZSTD compression, CRC32 checksums,
                Thumbnail generation, JSON Custom Metadata, PackageWriter APIs).

## Planned — medium features (see docs/newdev.md for the full living plan)
Recommended order; update newdev.md status board as these move:
- `[x]` 1. Parallel indexer (branch `parallel-indexer`): Phase C worker pool
        with pure-function chunk builds + in-chunk-order coordinator →
        output byte-identical to sequential (sha256-verified on a real
        12.4M-pt LAS); FlatCellSet replaces unordered_set in subsample
        (heap-lock contention fix, helps sequential too); `--threads` CLI +
        IndexOptions::threads + Convert-dialog control; `pftest` round-trip
        test target (first automated test; CTest `octree_roundtrip`).
        Bench: phase C 4.02s → 1.37s (2.9×) on Tikal-13.las at compressed
        preset settings. Pending: one live convert via the viewer Jobs panel.
- `[x]` 2. Multi-client roles (branch `multi-client-roles`): second
        watch-only viewer PIN (regenerated per start, always != driver PIN),
        role echoed in hello_ok, server silently drops move/cmd/set from
        viewers (security boundary is the server, not the UI); stream/state/
        cfg + /shot.png work for both roles; "Allow view-only clients"
        preference (persisted) + both PINs in Preferences and the QR dialog;
        webremote read-only mode (View-only badge, video+status only).
        Live-verified: concurrent driver+viewer, forged viewer input ignored,
        frames flowing to the viewer client.
- `[x]` 3. Camera path animation + MP4 export (branch `camera-path-export`):
        `CamPath.h` keyframes (non-uniform Catmull-Rom, shortest-way yaw,
        ortho step channel) persisted per-cloud in AppData `campaths.txt`;
        `VideoExporter.{h,cpp}` IMFSinkWriter H.264 MP4 (hardware transforms,
        abort deletes partial file); NV12 conversion lifted to shared
        `Nv12.h`; incremental offline export loop (export-sized FBO + EDL
        pass, LOD-settle before each frame, 30 ms/frame budget keeps UI
        live, progress modal + Cancel/Esc, camera restored); Properties >
        Camera Path UI (key list Go/Set/delete, scrub, preview, export
        dialog 720p–4K / 24–60 fps / bitrate, save-file dialog); remote
        cfg `pathKeys/pathDuration/pathPlaying` + `path_play`/`path_stop`
        cmds + CameraTab card; streaming suspended during export; toast
        with Explorer [Show] reveal; `--export-video` smoke hook.
        Smoke-verified: Tikal-13 → 1080p30, 121 frames, ffprobe-clean H.264.
- `[x]` 4. Cross-section / slice export (branch `cross-section-slice-export`):
        `forEachPointInBox`, own DXF R12 writer in pfcore, Clip-panel
        DXF/PNG/CSV export, pftest boxed-query coverage, and real-model smoke
        validation are complete.
- `[x]` 5. Annotations from phone (branch `annotations-from-phone`):
        remote/local Annotate tool, JSON per-cloud persistence,
        Web Remote annotation card, GL stream-visible pins + PC/phone labels.
- `[x]` 6. Multi-cloud scene (branch `multi-cloud-scene`, completed on
        `vxpc/thumbnails`): SceneCloud vector (unique_ptr store+renderer per
        cloud), scene origin = first cloud's cube centre, per-cloud
        worldOffset render/pick/frame-all, GPU budget split total/N, Scene
        panel (left dock: visibility, active-cloud select, close, add),
        convert-done Replace/Add prompt, aggregated status-bar stats,
        remote `clouds` cfg + `cloud_vis` cmd + webremote Scene card.
        Audit fixes folded in: compile-breaking status-bar code, unused
        aggregates, CSV export precision (6 -> 15 sig digits), and
        scene-space mixups (slice box, annotation goto, overlay projection
        used the ACTIVE cloud's centre where sceneOrigin was required).
        Pending: live two-cloud acceptance walk (needs a second converted
        scan on disk).
- `[ ]` Linearised-depth EDL (current uses raw depth diff; tune for ortho)
- `[ ]` Convert cancel for Phase A/B (currently aborts at Phase C chunk boundaries)
- `[ ]` VR/OpenXR initialization support
- `[ ]` Workspace layout presets (Window menu: save/restore named DockBuilder layouts)
- `[ ]` Recent-files pin/unpin + cached point-count/size metadata
- `[x]` Scene panel (multi-cloud) — shipped with newdev.md #6 (left dock,
        visibility/active/close/add rows)
