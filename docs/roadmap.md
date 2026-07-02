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

## Short-term Goals
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
- **Animation Paths**: Allow users to keyframe camera paths and render out video frames.
- **Cross-section / Slicing Export**: Export clipped cross-sections to standard CAD or image formats.
- **EDL quality**: linearised-depth response, configurable neighbour kernel.
- **Scene panel (multi-cloud)**: left dock listing loaded clouds + future
  annotations/segments/streams as child items — deliberately deferred until
  `OctreeStore`/`PointRenderer` support more than one resident cloud, so it
  ships once instead of being bolted on twice (see `architecture.md` §7).
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
