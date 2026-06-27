# Roadmap

## Done (shipped)
- **CPU Point Picking & Distance Measurement** — multi-segment polyline measure.
- **Improved Caching** — frame-stamped streaming-queue purge.
- **UI Scaling** — DPI auto-detect + UI Scale slider.
- **Eye-Dome Lighting (EDL)** — depth-edge post-process for uncoloured clouds.
- **Navigation overhaul** — orbit, focus, zoom-to-cursor, frame-all.
- **Single-file static release** — `ViitorXPCViewer.exe`, zero DLLs.

## Short-term Goals
- **On-disk cache management**: auto-purge old `PointForgeCache_*` converted dirs.
- **Color-by attribute polish**: editable elevation/intensity ranges + ramps.
- **Convert cancel coverage**: extend cooperative cancel to Phase A/B.

## Mid-term Goals
- **Animation Paths**: Allow users to keyframe camera paths and render out video frames.
- **Cross-section / Slicing Export**: Export clipped cross-sections to standard CAD or image formats.
- **EDL quality**: linearised-depth response, configurable neighbour kernel.

## Long-term Goals
- **Multi-user Sync**: Stream point cloud states between multiple viewers simultaneously.
- **VR Support**: Translate the current stereoscopic rendering into a full OpenXR integration.
