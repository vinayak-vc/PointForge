# PointForge

An out-of-core point cloud **importer + viewer**, built to ingest very large point
clouds (LAS/LAZ, E57, PLY, PTS/XYZ) — including data sets far larger than RAM
(billions of points) — and to display them interactively.

It deliberately mirrors the technology stack of *Axiom Present*:

| Concern            | Technology                                  |
|--------------------|---------------------------------------------|
| Language           | C++17                                        |
| Windowing / input  | SDL2                                         |
| Rendering          | OpenGL 3.3 core (GLSL `#version 330 core`)   |
| GUI overlay        | Dear ImGui (docking branch)                  |
| Math               | GLM                                          |
| LAS/LAZ I/O        | LASzip (`laszip_api`)                        |
| E57 I/O            | libE57Format                                 |
| PLY I/O            | own parser (no external dep)                 |
| PTS/XYZ I/O        | own text parser                              |
| Node compression   | zstd (optional, `--compress` / Convert dialog)|

PointForge is split into two programs that share a common core (`pfcore`):

* **`pfconvert`** — the *importer*. Converts a source point cloud into a
  streamable, disk-based **octree** (LOD pyramid). Runs out-of-core: memory use
  is bounded regardless of input size.
* **`pfview`** (built as `ViitorXPCViewer.exe`) — the *viewer*. Streams octree
  nodes on demand using frustum culling and screen-space-error LOD selection,
  so only the visible/relevant detail is ever resident on the GPU.

Builds and runs on MSVC 2022 + vcpkg. See `CLAUDE.md` and `docs/ARCHITECTURE.md`
for the deeper why/how; this file is the quick-start.

---

## 1. Prerequisites

* MSVC 2022 (Windows) — the project is developed and tested on this toolchain.
  Other C++17 compilers (Clang 12+, GCC 10+) may work but are untested.
* [CMake](https://cmake.org/) ≥ 3.20
* [vcpkg](https://github.com/microsoft/vcpkg) — supplies every third-party
  library through the manifest in `vcpkg.json` (Dear ImGui is the one
  exception: it's pulled via CMake `FetchContent`, pinned to the
  `v1.91.5-docking` tag, because the vcpkg `imgui` port dropped its SDL2
  binding in 1.92.x and the viewer's docked UI needs the docking branch).

## 2. Building

Two build scripts live at the project root; both always build into a
directory **at the repo root** (`build\` or `build-static\`), regardless of
where you invoke them from. Both pick up `VCPKG_ROOT` from your environment,
falling back to `C:\vcpkg` if unset.

```powershell
.\build.bat          # dynamic build, fast iteration -> build\Release\
.\build-static.bat   # single-file release exe       -> build-static\Release\
```

Or run the underlying CMake commands directly:

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The vcpkg manifest (`vcpkg.json`) pulls in `sdl2`, `glew`, `glm`, `laszip`,
`libe57format`, and `zstd`. Binaries land in `build\Release\`
(`pfconvert.exe`, `ViitorXPCViewer.exe` — plus `shaders\`, copied next to the
exe for the dynamic build only).

Watch the configure output for `-- LASzip: ...` and `-- libE57Format: ...` to
confirm those readers were detected; if a library is missing, the project
still builds — that reader becomes a graceful stub instead of a hard failure.

### Single-file release build

`build-static.bat` links everything statically (static CRT, static
SDL2/GLEW/LASzip/E57Format/zstd) and embeds the shaders + icon, producing one
self-contained `ViitorXPCViewer.exe` with zero third-party DLL dependencies —
verified with `dumpbin /DEPENDENTS` (only OS DLLs: OPENGL32, KERNEL32, USER32,
GDI32, WINMM, IMM32, ole32, OLEAUT32, VERSION, ADVAPI32, SETUPAPI, SHELL32).

The static vcpkg triplet must be compiled with the same MSVC toolset version
CMake's default generator links against, or you'll hit `LNK2019` on `__std_*`
STL symbols. `triplets\x64-windows-static.cmake` (an overlay triplet, wired in
by `build-static.bat`) pins that toolset version — see `docs/decisions.md`
("Single-File Static Release") if it ever needs re-pinning on a new machine.

## 3. Usage

### Import (convert to octree)

```powershell
pfconvert input.laz --out scans\mysite --chunk-depth 4 --spacing 0.0
```

* `--out <dir>`        output octree directory (created if missing, **required**)
* `--chunk-depth <n>`  coarse grid depth L for the disk-chunking pass; grid is (2^L)³ (default 4, range 0..10)
* `--spacing <meters>` root-node sample spacing; `0` = auto (cubeSize / 128)
* `--leaf <n>`         target max points per leaf node (default 50000)
* `--max-depth <n>`    hard octree depth cap (default 24)
* `--flush <points>`   chunker memory budget in points (default 16M ≈ 320 MB)
* `--keep-chunks`      keep intermediate chunk files (debugging)
* `--verbose`          debug logging

> The indexing pass is single-threaded in v1 for correctness and reproducibility.
> Chunks are spatially independent, so parallelising Phase C is the natural next
> optimisation (see `docs/ARCHITECTURE.md` §6).

Produces:

```
scans/mysite/
  meta.bin          FileMetadata (magic "PFO1") - binary metadata
  metadata.json     human-readable copy: bounds, scale/offset, attributes, spacing, point count
  hierarchy.bin     packed octree node descriptors (see docs/architecture.md)
  octree.bin        concatenated per-node point payloads
```

Conversion can also be started from inside the viewer — see below.

### View

```powershell
ViitorXPCViewer.exe scans\mysite
```

The viewer opens as a **docked shell**: menu bar + toolbar across the top, the
3D viewport in the center (never covered by panels), a *Properties* panel on
the right, and a bottom dock holding *Jobs* / *Console* / *Performance*
(closed by default — click their names under the *Window* menu, or let a
running conversion job auto-open *Jobs*). A status bar across the bottom shows
FPS, points drawn/total, GPU memory, and the current tool/mode.

To convert a scan **without leaving the viewer**: `Ctrl+I`, or drag a
`.las`/`.laz`/`.e57`/`.ply`/`.pts`/`.xyz` file onto the window. Conversion runs
as a background job — the dialog closes immediately, and progress shows in the
status-bar pill and the *Jobs* panel. Dragging an already-converted octree
**folder** onto the window loads it directly.

Controls:

| Input                  | Action                                    |
|-------------------------|-------------------------------------------|
| LMB drag                | Orbit around pivot                        |
| Double-click            | Focus pivot on point under cursor         |
| RMB drag                | Free look                                 |
| Wheel / Ctrl+wheel       | Zoom to cursor / point size               |
| WASD, Q/E                | Fly (Shift = fast)                        |
| F                       | Frame all                                 |
| 1 / 3 / 7 / 5           | Front / Side / Top view / toggle ortho    |
| M / C                   | Measure / Clip tool                       |
| Ctrl+O / Ctrl+I         | Open octree folder / Convert a scan       |
| Ctrl+, / Ctrl+P / F1    | Preferences / Command palette / Shortcuts |
| F3 / F5 or Shift+Space  | Stats overlay / hide all UI (zen mode)    |
| **F9**                  | **Stereoscopic SBS** — hides *all* UI so each eye sees only the cloud; F9 or Esc exits |
| F11 / F12               | Fullscreen / screenshot                   |
| Esc Esc                 | Quit                                      |

Gamepad (Xbox auto-mapped, or a custom joystick/ESP32-Bluetooth controller) is
supported too — configure it under *Edit > Preferences > Input*.

## 4. Layout

```
PointForge/
  CMakeLists.txt
  vcpkg.json
  build.bat            dynamic build -> build\
  build-static.bat      single-file release build -> build-static\
  triplets/             vcpkg overlay triplet (static-build toolset pin)
  README.md
  CLAUDE.md
  docs/
    architecture.md      the why: out-of-core build, octree/LOD, on-disk format, UI shell
    project-overview.md, roadmap.md, tasks.md, decisions.md, ai_handoff.md
  src/
    common/      Vec3d, AABB, Point, PointFormat, OctreeFormat, Log (+ setLogSink)
    io/          PointReader interface + LAS/E57/PLY/text readers + factory
    indexer/     out-of-core octree builder (chunking + sampling + writer)
    viewer/      Camera, Shader, OctreeStore, PointRenderer, Controller,
                 SerialController, Jobs.h (background convert job queue),
                 UiLog.h (Console panel log buffer), main.cpp (docked UI shell)
    tools/
      pfconvert/ importer CLI entry point
  shaders/       point.vert / point.frag (GLSL 330 core)
```

## 5. Tests / lint

None yet. No test suite, no linter, no CI. MSVC builds with `/W3 /permissive-
/MP`; GCC/Clang with `-Wall -Wextra`. A synthetic `.xyz` generator + octree
round-trip test is the suggested first addition — see `docs/tasks.md`.

## 6. License of dependencies

LASzip (LGPL/permissive), libE57Format (BSD), SDL2 (zlib), GLEW (modified
BSD), GLM (MIT), Dear ImGui (MIT), zstd (BSD/GPLv2 dual). PLY and PTS/XYZ
readers are original code with no external dependency. Review each before
shipping commercially.
