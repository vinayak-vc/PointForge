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
- `[x]` Single-file static release `ViitorXPCViewer.exe` (embedded shaders+icon)
- `[x]` Navigation UX: orbit (LMB), double-click focus, wheel zoom-to-cursor, frame-all
- `[x]` Always-on status bar + F1 help overlay + slider tooltips
- `[x]` Multi-segment polyline measure (per-segment + total, snap, undo/clear/copy)
- `[x]` Quality preset + color-by Intensity + Classification + colour-bar legend + theme toggle
- `[x]` Eye-Dome Lighting (EDL) post-process (FBO + fullscreen pass)
- `[x]` Recent files (MRU) + auto-load last + convert Cancel button
- `[x]` Screenshot (F12, BMP), reset confirmation, clear-clipping, top toolbar
- `[x]` Controller support: Xbox gamepad (SDL_GameController) + raw joystick fallback
        (camera, actions, ImGui UI-nav mode, deadzone/sens config, live rebind panel)

## In Progress
- (none)

## To Do
- `[ ]` Improved caching: auto-purge old converted-cloud cache dirs on disk
- `[ ]` UI font re-rasterization at scale (current DPI path scales metrics + FontGlobalScale only)
- `[ ]` Linearised-depth EDL (current uses raw depth diff; tune for ortho)
- `[ ]` Convert cancel for Phase A/B (currently aborts at Phase C chunk boundaries)
- `[ ]` VR/OpenXR initialization support
