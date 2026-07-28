# ViitorX PointCloud Viewer — Complete Feature Inventory

Source of truth: `src/viewer/main.cpp` (~5244 lines) plus supporting headers.
Version `1.0.56` (`Version.h`). App/window title **"ViitorX PointCloud Viewer"**
(`main.cpp:421`); internal codename in comments is "pfview". Shortcut catalog is
centralized in `kKeyBinds[]` (`main.cpp:338-368`), which also feeds the F1 overlay.

This is the script the slide deck is built from. Every entry was verified against
the code — do not add features that aren't here.

## 1. Menu Bar  (`main.cpp:3289-3388`)

**File** — Open Cloud… (Ctrl+O, `.vxpc` or an octree's `meta.bin`) · Open Octree
Folder… (legacy loose folders) · Open Recent ▸ (up to 10, + Clear Recent) ·
Convert a Scan… (Ctrl+I) · Save Project Data to Package (embeds bookmarks/path/
measurements/annotations into the loaded `.vxpc`; single-cloud only) · Screenshot
(F12) · Exit (Esc Esc).

**Edit** — Preferences… (Ctrl+,).

**View** — Frame All (F) · Reset View · Front (1) · Side (3) · Top (7) ·
Orthographic (5, toggle) · Color Mode ▸ (True Color / Elevation / Solid Color /
Intensity / Classification) · Eye-Dome Lighting (toggle) · Stereoscopic SBS (F9) ·
Stats Overlay (F3) · Fullscreen (F11) · Hide UI zen (F5) · Welcome Screen.

**Tools** — Navigate (Esc) · Measure (M) · Annotate (A) · Clip (C) · Convert a
Scan… (Ctrl+I). Tools are mutually exclusive.

**Window** — toggle Scene / Properties / Jobs / Console (badge `Console (N!)` on
unseen issues) / Performance / Status Bar · Reset Layout.

**Help** — Keyboard Shortcuts (F1) · Command Palette (Ctrl+P) · About. *(About
modal hardcodes "0.1.0" — stale string, real version is 1.0.56.)*

## 2. Toolbar  (`main.cpp:3397-3459`)

Open (+ recent dropdown) · Convert… · Frame · **Nav / Measure / Annotate / Clip**
(active highlighted) · Color-mode combo · Quality combo (Low / Medium / High /
Ultra) · Shot (F12).

## 3. Docked Panels  (`main.cpp:3478-4021`)

- **Scene** (left) — multi-cloud list: visibility checkbox, name, point count,
  Close; "+" to add another cloud. Tools follow the active cloud; rendering &
  frame-all include every visible cloud.
- **Properties** (right) — collapsing sections:
  - **Cloud Info** — points, nodes, cube size.
  - **Display** — Quality · Point size (1–16 px) · Color Mode · Solid Color picker ·
    Eye-Dome Lighting (Strength 0.1–5, Radius 0.5–4) · **Advanced**: LOD budget
    (px SSE), GPU budget (128–8192 MB), Uploads/frame, Round points, Attenuate,
    Background colour.
  - **Camera** — Orthographic + Ortho Size · Fly Speed · Reset View · Frame All ·
    presets · **Bookmarks** (named poses per cloud).
  - **Camera Path** — keyframes (time / Go / Set / ✕) · Add Key at Current View ·
    Clear · Duration · Scrub · Preview · Export MP4….
  - **Measure** (when active) — point list · Total (m) · Undo/Clear/Copy/Done.
  - **Annotations** — per-pin label · Go · ✕ · coords · Add Pins/Done.
  - **Clip** — Enable Clipping · Min/Max SliderFloat3 · Reset Planes · Export slice….
- **Jobs** (bottom) — convert & slice-export jobs: state tag, progress + message,
  Cancel / Open / Reveal / Console. Clear finished.
- **Console** (bottom) — mirrors `pf::log()`; Debug/Info/Warn/Error filters,
  Auto-scroll, Clear, Copy.
- **Performance** (bottom) — FPS plot · Visible/Drawn nodes · Drawn points ·
  Points on GPU · Load queue · GPU budget bar.

## 4. Keyboard Shortcuts  (`main.cpp:2065-2123`, catalog `338`)

Ctrl+O open · Ctrl+I convert · F12 screenshot · drag&drop (folder/.vxpc loads,
raw scan → Convert). LMB orbit · dbl-click focus · RMB free-look · wheel zoom ·
Ctrl+wheel point size · WASD/QE fly (Shift 5×) · F frame · 1/3/7 presets ·
5 ortho. M/A/C tools · Esc exit tool. F1 shortcuts · Ctrl+P palette · F3 stats ·
F5 / Shift+Space hide UI · F9 stereo · F11 fullscreen · Ctrl+, prefs · Esc Esc
quit. Movement ignored while typing in a field.

## 5. Convert Wizard  (`main.cpp:4029-4421`)

Accepts LAS/LAZ/E57/PLY/PTS/XYZ. Breadcrumb: **1 Source ▸ 2 Quality ▸ 3
Destination ▸ 4 Convert**.
- **Source** — Browse for files… (multi-select) or drag&drop; per-file size + remove.
- **Quality** — cards **Balanced (recommended)** / Draft / High / Custom.
  **Advanced settings**: Sampling (Spacing, Leaf size, Max depth), Resources
  (Chunk grid depth, Flush budget Mpts, Indexer threads), Output (Compress nodes
  zstd, Keep chunk files). Presets in `applyConvertPreset` (`1815`).
- **Destination** — single → `.vxpc` path + "Open when finished"; multi → folder
  (one `.vxpc` each). Review summary.
- **Converting / Done** — progress + ETA; then Convert Another / Open in Viewer /
  Close. Jobs run on background threads (`Jobs.h`).
- **Photogrammetry (from photos)** (`Photogrammetry.h/.cpp`, v1.0.57+) — Source
  step "Or start from photos": pick/drop a folder of overlapping drone or camera
  JPGs. Own EXIF sniffer samples GPS tags; **GPS → ODM** (georeferenced
  metric-scale LAZ via Docker), **no GPS or no CUDA → COLMAP 4.1.0** (native,
  densest) — Auto recommendation with reason, both always selectable. One
  consent dialog auto-installs both engines (COLMAP zip → app data; Docker
  Desktop via winget + `docker pull opendronemap/odm`). Reconstruction runs as a
  cancelable background job (0–70% of the bar) chaining into the normal octree
  convert (70–100%). Virtualization disabled in BIOS → ODM soft-skipped and the
  wizard shows the motherboard's exact BIOS steps (registry board detect,
  8-vendor table, web-search button); COLMAP used instead.

## 6. Preferences  (`main.cpp:4615-4783`)

- **General** — Light theme · UI Scale 0.5–3× · Auto-load last cloud · Open .vxpc
  with this viewer (per-user file assoc) · Clear recent.
- **Display** — Stereoscopic: Eye Separation (IPD) · Focal Distance.
- **Input** — Gamepad (enable, rescan, UI-nav mode, deadzone, look/move sens,
  invert Y, raw calibration) · Remote Streaming Mode (JPEG WebSocket / WebRTC) ·
  Serial (Bluetooth) controller (enable, auto-detect MAC, MAC, COM port,
  reconnect) · **Web remote** (enable, Port 8899, allow view-only, serving URL,
  Driver/Viewer PINs, Show connect QR, Restart).
- **Advanced** — Reset all settings to defaults.
Settings persist to `pfview_config.txt`.

## 7. Camera / Navigation

Orbit (LMB), free-look (RMB), zoom-to-cursor (wheel), double-click focus, fly
(WASD/QE, Shift 5×). Presets Front(1)/Side(3)/Top(7)/Ortho(5). Frame All (F),
Reset View. Bookmarks persisted per cloud & embeddable in `.vxpc`.

## 8. Measure & Clip

- **Measure** (M) — click points → world polyline; per-segment + total (m);
  Undo/Clear/Copy/Done. Real 3D geometry (shows in screenshots & web stream).
- **Annotate** (A) — labelled pins; persisted, embeddable.
- **Clip** (C) — axis-aligned clip box (Min/Max), Reset Planes.
- **Slice Export** (`4481-4581`) — DXF / PNG / CSV; density (DXF/CSV) or image
  size (PNG); estimated point count + large-export warning. CSV columns
  `x,y,z,r,g,b,intensity,classification`.

## 9. Rendering

5 color modes (True Color, Elevation, Solid Color, Intensity, Classification);
Elevation & Intensity show a **Turbo colormap legend**. Point size 1–16 px,
Round points, Attenuate. **EDL** (offscreen FBO post pass) with Strength/Radius.
LOD streaming with screen-space-error budget, uploads/frame throttle, GPU budget
+ LRU eviction. Quality presets → sse+GPU budget (Low 4px/512MB … Ultra
0.5px/4096MB). Frustum culling.

## 10. Stereoscopic / Fullscreen / Hide-UI

- **Stereoscopic SBS** (F9) — per-eye side-by-side, hides ALL UI, "press F9/Esc
  to exit" hint per eye, watermark pop-out. Esc exits stereo before quitting.
- **Fullscreen** (F11). **Hide UI / zen** (F5 or Shift+Space).

## 11. Camera Paths & Video

`CamPath.h` keyframes (t, pos, yaw, pitch, ortho, orthoSize), interpolated;
authored in Properties ▸ Camera Path; persisted per cloud & embeddable. **Export
Video** (`4424-4478`): resolution up to 4K, 24/30/60 fps, H.264 bitrate 5–100
Mbps. `VideoExporter.h` = IMFSinkWriter H.264/NV12 MP4, NVENC/QuickSync
hardware-accelerated (Windows only; stub elsewhere).

## 12. Web Remote  (`RemoteServer.h/.cpp`)

Embedded civetweb HTTP + WebSocket serving a React control page. Two roles —
**Driver PIN** (full control) / **Viewer PIN** (watch-only); regenerate on start.
Default port 8899. Phone joysticks send velocity intent (zeroed after 500 ms
stale). Command set (`consumeCommands`): frame/reset/presets/hideui/shot/
fullscreen/stereo/ortho/measure/clip/annotate/cloud_vis/pointsize/speed/bookmarks/
path. Config broadcast `{"t":"cfg"}` ~1 Hz. Viewport streaming: JPEG over
WebSocket (optional WebRTC). Screenshot served PIN-gated at `/shot.png`. QR
"Connect phone" modal via `qrcodegen`. No-op stub if built without civetweb/json.

## 13. .vxpc / Encryption / File Assoc / Watermark

- **.vxpc** — 128-byte header + directory of entries, optional per-entry zstd.
  Random-access `ByteSource` supports local files AND `http(s)://` Range requests.
  Multi-cloud via `scene.json`. Embedded sidecars: bookmarks / campaths /
  measurements / annotations / scene.
- **Encryption** — AES-256-GCM at rest, PBKDF2-HMAC-SHA256 (100k). **Format-level
  only — NOT wired to any UI**; no password prompt anywhere. Not user-facing.
- **File association** — per-user HKCU `.vxpc` → `ViitorXPC.Package`, refreshed
  each startup. Toggle in Preferences ▸ General. Windows only.
- **Watermark** (`EmbeddedWatermark.h`) — brand logo behind welcome/empty-state,
  convert wizard, and viewport (per-eye pop-out in SBS). NOT burned into saved
  screenshots.

## 14. Command Palette (Ctrl+P)  (`main.cpp:4919-4990`)

Fuzzy-filtered command list; Enter runs first match. Covers open/convert/frame/
reset/presets/ortho/tools/EDL/stereo/screenshot/stats/panels/prefs/shortcuts/
reset-layout/fullscreen/hide-ui/quit.

## 15. Other UI

Status bar (mode + hint, hover XYZ, job pill, pad/BT indicators, GPU/points/FPS/
version). Stats overlay (F3 HUD). Toasts (with Open/Reveal). Welcome/empty state.
First-load nav hint.

## Half-implemented / disabled

1. AES-256-GCM encryption — no UI, not usable by end users.
2. Save project data into a **multi-cloud** package — blocked ("not supported yet").
3. About dialog version string stale ("0.1.0").
4. WebRTC remote streaming — only when the build supports it; default is JPEG/WS.
5. MP4 export & serial/Bluetooth controller — Windows-only; stubbed elsewhere.
