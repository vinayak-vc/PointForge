# Project Overview

**PointForge** (shipped as **ViitorXPCViewer**) is a Windows point-cloud
importer + viewer for very large clouds (LAS/LAZ, E57, PLY, PTS/XYZ — billions
of points, larger than RAM):

- Out-of-core conversion to a streamable on-disk octree (`pfconvert` CLI or
  in-app background jobs), then interactive viewing with frustum culling,
  screen-space-error LOD, async streaming, and a GPU memory budget.
- Docked ImGui shell (menu/toolbar/Properties/Jobs/Console), measurement,
  clipping, EDL, stereoscopic SBS, controller + ESP32 serial input.
- Single-file static release: all deps, shaders, icons, **and the web remote
  app** are embedded in the exe.

**Web Remote**: a React app served by an embedded civetweb server lets a phone
browser control the camera and viewer options over LAN (PIN pairing, multi-touch
gestures, 30 Hz move protocol). Two viewport streaming engines, switchable at
runtime via `preferredStream`:

1. **JPEG** – turbojpeg frames over WebSocket; robust, per-frame crisp.
2. **WebRTC H.264** – low-latency; libdatachannel + Media Foundation with
   **hardware encoding** (NVENC/QuickSync/VCE via async MFT) and automatic
   software-encoder fallback; quality-based VBR at a 12 Mbps hint.

A Unity integration (branch `library/unity`) exposes the streaming core and
converter as flat C-API DLLs (`pfunity`, `pfconvert_dll`).

Current milestone: final phone verification of the WebRTC path, then merge
`webapp-controller` into `main` for the first production release package.
