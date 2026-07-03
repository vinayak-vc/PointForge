# Project Overview

**PointForge** (ViitorXPCViewer) is a Windows‑only point‑cloud viewer that supports:
- Local point‑cloud loading (LAZ, LAS, E57, etc.)
- Real‑time remote control via a web UI (React) running on an embedded Civetweb server.
- Two streaming modes for the viewport:
  1. **JPEG fallback** – low‑bandwidth, uses TurboJPEG.
  2. **WebRTC H.264** – low‑latency, high‑quality, powered by Media Foundation and libdatachannel.

The recent work added full WebRTC support on the C++ side and a UI toggle to select the stream engine. The next milestone is to embed the web assets directly into the executable for a single‑binary distribution.
