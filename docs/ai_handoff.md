# AI Handoff - PointForge (C++ repo)

## Latest Session (2026-07-03, cont.) - Logo, Branding, and 3D Watermark

### What was built / Fixed
- **Configuration Routing**: Modified `main.cpp` to use `SDL_GetPrefPath` to route the `pfview_config.txt` serialization to the user's local `AppData` directory (`AppData/Roaming/ViitorX/ViitorXPC/`). This ensures standard desktop behavior without requiring admin privileges to save settings when installed in `Program Files`.
- **Project Renaming**: Renamed the project from `PointForge` to `ViitorXPC` across `CMakeLists.txt` and configuration namespaces. The primary executable is built as `ViitorXPCViewer.exe`.
- **Windows File Explorer Icon**: Generated a transparent `vx.ico` Windows icon from the existing `vx.bmp` logo. Added `app.rc` to the CMake build definitions to embed the `.ico` file into the Windows executable so the branding shows up natively in the File Explorer.
- **Stereoscopic 3D Logo Pop-Out**: Rewrote the background watermark rendering in `main.cpp` for SBS (Side-by-Side) 3D mode. It now renders twice (once for each eye) and applies a 15-pixel negative parallax shift so the logo has a true stereoscopic depth effect, "popping out" of the screen when viewed through 3D lenses.

### Modified files
c:/UnrealProject/PointForge/CMakeLists.txt
c:/UnrealProject/PointForge/src/viewer/main.cpp
c:/UnrealProject/PointForge/make_ico.ps1
c:/UnrealProject/PointForge/src/viewer/app.rc
docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
The application is feature-complete with UI gestures, WebRTC streaming, local application data persistence, native File Explorer branding, and 3D UI overlays.
- Final user test of the SBS 3D effect in VR headsets/stereoscopic lenses.
- Merge current branches and prepare the first production release package.

## Previous Session (2026-07-03, cont.) - Web Remote WebRTC Streaming & Mobile Touch Fixes
During the physical phone test, WebRTC video streaming and mobile UI were finalized. The viewer now perfectly streams high-performance, low-latency H.264 video to the phone browser, and replaces the mobile joysticks with an intuitive multi-touch gesture system.

### What was built / Fixed
- **WebRTC Pipeline Stability**: Fixed COM initialization in `RemoteServer::encodeLoop` which caused silent frame drops. Added manual `IMFSample` allocation for `CLSID_CMSH264EncoderMFT` which fixed crashes during stream processing.
- **SSRC Matching**: Explicitly bound the `rtc::Description::Video` SDP SSRC and `rtc::RtpPacketizationConfig` SSRC to `1111` so the web browser successfully matches and plays the RTP stream instead of dropping it as rogue packets.
- **Keyframe Guarantee**: Configured `CODECAPI_AVEncMPVGOPSize` to `30` (1 second) to guarantee a consistent IDR frame stream, preventing black screens when the client connects mid-stream.
- **WebRTC Thread Safety**: Added `try/catch` and `isOpen()` checks to `videoTrack->sendFrame(...)` and all SDP/ICE handlers, guaranteeing `libdatachannel` exceptions do not crash the C++ app when a client abruptly disconnects.
- **Mobile Touch Gestures**: Removed the nipple.js joysticks in `webremote`. Implemented multi-touch gestures in `FlyTab.tsx`:
  - **Single tap & move**: Maps to Pitch/Yaw (Look)
  - **Double tap & move**: Maps to Up/Down/Left/Right (Pan)
  - **Pinch**: Maps to Forward/Backward (Zoom)
  - **Deltas**: Changed `App.tsx` to automatically zero-out `f/s/u/yaw/pit` on every 33ms send tick so pointer deltas accumulate correctly for drag-to-position control. Held buttons (UP/DOWN/BOOST) use a faster `setInterval` to replenish their state against the zeroes.

### Protocol v1 (verified)
`hello{pin}` -> `hello_ok|hello_bad`; `move{f,s,u,yaw,pit,boost}` 30 Hz;
`cmd{n[,v]}`; `state{fps,pts,pos,ui,file}` 5 Hz. Signs: stick up = +1 = fly
forward / look up (pitch NOT negated in main.cpp — verify feel on phone).

### Verified (localhost, real exe, physical phone)
GET / 200 + assets 200 from `web/`; WS upgrade 101. WebRTC streaming successfully connects and renders the H.264 video to the React `<VideoLayer>`. Gestures drive the camera smoothly via delta accumulation. Full build clean (MSVC).

### Modified files
c:/UnrealProject/PointForge/src/viewer/RemoteServer.cpp
c:/UnrealProject/PointForge/webremote/src/App.tsx
c:/UnrealProject/PointForge/webremote/src/FlyTab.tsx
docs/{tasks,decisions,ai_handoff}.md

### Next Recommended Task
The Web Remote system (React UI, WebSocket, WebRTC streaming, and Native C++ Server) is now fully working and verified. 
- Conduct a final sanity check of the web app controls on the mobile device.
- If everything is working perfectly, commit the `webapp-controller` branch and merge it into main.

## Previous Session (2026-07-03) - Web Remote Controller: plan + branch

Planning session only — no code yet. Goal: control camera + viewer options from a
phone browser over LAN so the PC can hide all UI (F5) while flying continues.

- Created branch `webapp-controller` (based off `library/unity`; that branch's
  uncommitted DLL work — modified `CMakeLists.txt`, untracked
  `src/tools/pfconvert/pfconvert_api.cpp` — carried over untouched).
- Architecture decided and recorded in `docs/decisions.md`: embedded
  civetweb HTTP+WS server inside pfview (rejected Firebase RTDB/Netlify relay —
  same-LAN use case, ~1-5 ms vs 100-300 ms, offline-capable). React+Vite+TS app
  in `webremote/`, built to static files, served by the viewer from `web/`
  beside the exe (like `shaders/`).
- Full 5-phase plan recorded in `docs/tasks.md` (In Progress section):
  deps → `RemoteServer.{h,cpp}` → main.cpp integration → React app → build wiring.
- Protocol v1: JSON over WS — `hello` w/ 4-digit PIN, `move` @30 Hz
  (f/s/u/yaw/pit/boost), `cmd` (frame/presets/ortho/hideui/shot/measure/
  pointsize/speed), `state` @5 Hz back (fps/pts/pos/ui/file).
- Key patterns to follow: input mirrors `SerialController` (bg thread + atomics,
  polled in main loop ~`main.cpp:890`); commands mirror `consumePause()` one-shot
  pattern; commands dispatch to existing flags (`frameAllReq`, `showUI`,
  `pendingShot`, presets).

### Next Recommended Task
Phase 0: verify vcpkg `civetweb` port has WebSocket support enabled (else
FetchContent with `-DCIVETWEB_ENABLE_WEBSOCKETS=ON`, or `ixwebsocket`); add
`civetweb` + `nlohmann-json` to `vcpkg.json`; scaffold `webremote/` with Vite;
add `PF_WITH_REMOTE` CMake guard (graceful stub like `PF_WITH_LAS`).
