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
- `[x]` Doc drift fix: `NodeRecord` 32->52 bytes, `PackedPoint` 20->22 bytes

## In Progress
- (none)

## To Do
- `[ ]` Advanced Eye-Dome Lighting (EDL) shader integration
- `[ ]` Improved caching: auto-purge old converted-cloud cache dirs on disk
- `[ ]` UI font re-rasterization at scale (current DPI path scales metrics + FontGlobalScale only)
- `[ ]` VR/OpenXR initialization support
