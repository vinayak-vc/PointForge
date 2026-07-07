# Roadmap

## Done (shipped)
- **CPU Point Picking & Distance Measurement** — multi-segment polyline measure.
- **Improved Caching** — frame-stamped streaming-queue purge.
- **UI Scaling** — DPI auto-detect + UI Scale slider.
- **Eye-Dome Lighting (EDL)** — depth-edge post-process for uncoloured clouds.
- **Navigation overhaul** — orbit, focus, zoom-to-cursor, frame-all.
- **Single-file static release** — `ViitorXPCViewer.exe`, zero DLLs; toolset-mismatch
  build gotcha now has a one-command fix (overlay-triplet toolset pin, no Ninja needed).
- **Controller + custom ESP32 Bluetooth controller support**.
- **Docked UI shell redesign** — menu bar/toolbar/Properties/Jobs/Console/Performance
  docks replace the old single scrolling settings panel; conversion runs as a
  background job queue; F1 searchable shortcuts + Ctrl+P command palette;
  stereoscopic SBS now hides all UI chrome.
- **Web Remote** — embedded civetweb HTTP+WS server + React app (embedded in the
  exe) for phone control over LAN: PIN pairing, multi-touch gestures, 30 Hz move
  protocol, settings mirror. Viewport streaming via JPEG (turbojpeg over WS) or
  WebRTC H.264 (libdatachannel + Media Foundation, hardware NVENC/QuickSync
  encode with software fallback, quality VBR) — runtime-switchable.
- **Web Remote premium redesign + desktop input** — enterprise-grade connect
  screen (PIN boxes, animated logo, branding pane) and in-app UI (icon-only
  toolbar, dropdown pickers, elevation/intensity legend overlay); desktop
  mouse+keyboard camera control (drag look, right-drag pan, wheel zoom, WASD
  fly) alongside the original touch gestures; remote tap-to-measure (place a
  measurement point by tapping the video, not just at the PC).
- **Auto-incrementing build version** — every build stamps a `v1.0.N` label
  (status bar, exe file-version metadata, and the exe's own filename) so a
  tester can identify exactly which build they're running.

## Short-term Goals
- **Web Remote status-bar clipping**: a flex-overflow audit of
  `.app-shell`/`.workspace` is still needed so the in-app status bar can't be
  clipped off-screen at some desktop window sizes.
- **On-disk cache management**: auto-purge old `PointForgeCache_*` converted dirs.
- **Color-by attribute polish**: editable elevation/intensity ranges + ramps.
- **Convert cancel coverage**: extend cooperative cancel to Phase A/B.
- **Multi-select batch conversion**: Convert dialog accepts multiple source files,
  enqueues one job per file (the `JobQueue` already supports N jobs — UI-only work).
- **Workspace layout presets**: save/restore named `DockBuilder` layouts
  (e.g. Viewing / Measuring / Converting) from the Window menu.
- **Recent-files polish**: pin/unpin entries, point-count + size metadata cached
  alongside each recent path.

## Mid-term Goals
> Six of these now have a detailed, code-anchored implementation plan with a
> recommended order in **docs/newdev.md** (living doc — keep its status board
> current): parallel indexer, multi-client roles, camera path + MP4 export,
> cross-section export, phone annotations, multi-cloud scene.
- **Animation Paths**: Allow users to keyframe camera paths and render out video frames. *(**done** — newdev.md #3, branch `camera-path-export`: CamPath keyframes + IMFSinkWriter MP4 export)*
- **Cross-section / Slicing Export**: Export clipped cross-sections to standard CAD or image formats. *(planned — newdev.md #4)*
- **Parallel indexer**: Phase C chunk worker pool, 4–8× conversion speedup. *(**done** — newdev.md #1: 2.9× phase C on a real scan, byte-identical output, pftest)*
- **Web Remote roles**: view-only clients with a separate viewer PIN. *(**done** — newdev.md #2: viewer PIN, server-side gating, read-only web UI)*
- **Phone annotations**: tap video → labelled 3D pin, synced + persisted. *(planned — newdev.md #5)*
- **EDL quality**: linearised-depth response, configurable neighbour kernel.
- **Scene panel (multi-cloud)**: left dock listing loaded clouds + future
  annotations/segments/streams as child items — deliberately deferred until
  `OctreeStore`/`PointRenderer` support more than one resident cloud, so it
  ships once instead of being bolted on twice (see `architecture.md` §7).
  *(planned — newdev.md #6, scheduled last)*
- **Point/measurement tool suite growth**: area, angle, profile tools alongside
  the existing distance measure, using the same tool-mode + Properties-section
  pattern (toolbar mode button + docked options, never a modal).

## Long-term Goals
- **Multi-user Sync**: Stream point cloud states between multiple viewers simultaneously.
- **VR Support**: Translate the current stereoscopic rendering into a full OpenXR integration.
- **AI segmentation**: a new job type in the existing `JobQueue`, producing
  Scene-panel layers — same background-job + progress-pill UX as conversion.
- **Plugin support**: menu items, toolbar buttons, tool modes, dock panels, and
  job types are the shell's extension points (see `architecture.md` §7);
  no plugin should require shell changes to add a new one of these.
