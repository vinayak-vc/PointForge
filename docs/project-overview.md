# Project Overview

**PointForge** (shipped as **ViitorXPCViewer**) is a Windows point-cloud
importer + viewer for very large clouds (LAS/LAZ, E57, PLY, PTS/XYZ — billions
of points, larger than RAM):

- Out-of-core conversion to a streamable on-disk octree (`pfconvert` CLI or
  in-app background jobs), then interactive viewing with frustum culling,
  screen-space-error LOD, async streaming, and a GPU memory budget.
- Docked ImGui shell (menu/toolbar/Properties/Jobs/Console), measurement,
  clipping with DXF/CSV/PNG slice export, EDL, stereoscopic SBS, controller +
  ESP32 serial input.
- Single-file static release: all deps, shaders, icons, **and the web remote
  app** are embedded in the exe.

**Web Remote**: a React app served by an embedded civetweb server lets a phone
*or desktop* browser control the camera and viewer options over LAN (PIN
pairing, multi-touch gestures **and** mouse-drag/WASD-keyboard input, 30 Hz
move protocol, remote tap-to-measure). Two viewport streaming engines,
switchable at runtime via `preferredStream`:

1. **JPEG** – turbojpeg frames over WebSocket; robust, per-frame crisp.
2. **WebRTC H.264** – low-latency; libdatachannel + Media Foundation with
   **hardware encoding** (NVENC/QuickSync/VCE via async MFT) and automatic
   software-encoder fallback; quality-based VBR at a 12 Mbps hint.

A Unity integration (branch `library/unity`) exposes the streaming core and
converter as flat C-API DLLs (`pfunity`, `pfconvert_dll`).

Every build stamps a `MAJOR.MINOR.PATCH` version (status bar, exe file-version
metadata, and the exe's own filename) so a tester can identify exactly which
build they're running.

Current milestone: medium-feature plan items #1-#4 are complete
(`parallel-indexer`, `multi-client-roles`, `camera-path-export`,
`cross-section-slice-export`). Next planned work is #5 phone annotations.
