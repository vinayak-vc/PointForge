# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

PointForge is an **out-of-core point cloud importer + viewer** written
to ingest very large clouds (LAS/LAZ, E57, PLY, PTS/XYZ — billions of points,
larger than RAM) and display them interactively. It deliberately mirrors the
tech stack of the "Axiom Present" app in the parent folder.

## Stack
- C++17
- SDL2 (window/input), OpenGL 3.3 core, Dear ImGui (GUI), GLM (math)
- LAS/LAZ via LASzip; E57 via libE57Format; PLY + PTS/XYZ via own parsers
- Dependencies come from **vcpkg** (manifest: `vcpkg.json`), except Dear ImGui,
  which is compiled from source via CMake `FetchContent` (the vcpkg imgui port
  dropped its SDL2 binding in 1.92.x). Pinned to **v1.91.5-docking** — the
  viewer shell uses DockSpace/DockBuilder; do not repin to a non-docking tag.

## Two programs (share static lib `pfcore`)
- `pfconvert` — importer. Source cloud -> streamable on-disk octree.
- `pfview` — viewer. Streams octree nodes with frustum culling + screen-space
  error LOD + async loading + LRU GPU-memory budget.

## Layout
```
src/common/    Vec3d, AABB, Point, PointFormat (PackedPoint), OctreeFormat
                 (FileMetadata + NodeRecord on-disk structs), Log
src/io/        PointReader interface; LASReader, E57Reader, PLYReader,
                 TextReader; ReaderFactory (dispatch by extension)
src/indexer/   Chunker (out-of-core chunking), OctreeIndexer (chunk subtrees +
                 coarse stitch), MetadataWriter
src/tools/pfconvert/main.cpp   importer CLI entry point
src/viewer/    Camera, Shader, OctreeStore (load + async streaming),
                 PointRenderer (GPU VBO cache + eviction), main.cpp (pfview),
                 Jobs.h (background convert job queue), UiLog.h (Console ring
                 buffer fed by pf::setLogSink)
shaders/       point.vert / point.frag (GLSL 330 core)
docs/ARCHITECTURE.md   the why: 3-phase out-of-core build, MNO LOD, on-disk format
```

## Build (Windows, vcpkg)
```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
Binaries + `shaders/` land in `build/Release/`. Watch the configure output for
`-- LASzip: ...` to confirm LAS/LAZ support was detected (sets `PF_WITH_LAS`).

## Run
```powershell
cd build\Release
.\pfconvert.exe "C:\path\to\scan.laz" --out scans\mysite
.\pfview.exe scans\mysite
```
Viewer UI is a docked shell: menu bar + toolbar, central viewport (passthru
dock node), right "Properties" dock, bottom Jobs/Console/Performance dock
(closed by default), status bar. Conversion runs as background jobs: Convert
dialog (Ctrl+I) -> Jobs panel + status-bar pill + completion toast. Controller
and ESP32-serial config live in Edit > Preferences > Input.
Keys: LMB orbit, 2xLMB focus, RMB look, WASD+QE fly (Shift fast), wheel zoom,
Ctrl+wheel point size, F frame, M measure, C clip, 1/3/7 view presets, 5 ortho,
F1 shortcuts, Ctrl+P command palette, F3 stats HUD, F5 or Shift+Space hide UI,
F9 stereoscopic SBS (hides ALL UI; F9/Esc exits), F11 fullscreen, Ctrl+O open,
Ctrl+I convert, Ctrl+E export GLB, Ctrl+, preferences, Esc Esc quit.

Export: 2D slice → DXF/PNG/CSV (Properties > Clip). 3D mesh → GLB/glTF 2.0 or
FBX (File > Export > GLB/FBX, Ctrl+E; Format combo in the dialog). GLB: point
clouds as POINTS, Gaussian splats via KHR_gaussian_splatting (lossless), writer
`src/io/GlbWriter.{h,cpp}`. FBX: ASCII 7400 vertex-only mesh + per-point colour
(Unity/Blender/Unreal), splats degrade to points, no intensity/class, writer
`src/io/FbxWriter.{h,cpp}`. Both share the dialog + `meshExportWorker` pipeline
(scope/layout(separate|merge|per-file)/region/decimation) in pfcore. Smoke hooks:
`--export-slice <prefix>`, `--export-glb <out.glb>`, `--export-fbx <out.fbx>`,
`--export-video <out.mp4>`.

`pfconvert` flags (arg parsing in src/tools/pfconvert/main.cpp; `--out` required):
- `--out <dir>`        output octree directory (created if missing)
- `--chunk-depth <n>`  coarse grid depth L, grid is (2^L)³ (default 4; range 0..10)
- `--spacing <m>`      root sample spacing; `0` = auto = cubeSize/128 (default 0)
- `--leaf <n>`         target max points per leaf node (default 50000)
- `--max-depth <n>`    hard octree depth cap (default 24)
- `--flush <points>`   chunker memory budget in points (default 16M ≈ 320 MB)
- `--keep-chunks`      keep intermediate chunk files (debug)
- `--verbose`          debug logging

## Tests / lint
None. No test suite, no linter, no CI. MSVC builds with `/W3 /permissive- /MP`;
GCC/Clang with `-Wall -Wextra`. A synthetic `.xyz` generator + octree round-trip
test is the suggested first addition (see gaps §6).

## Current status (as of handoff)
- Builds and runs on MSVC (VS 2022) + vcpkg.
- Fixed so far: imgui SDL2-binding removal (now FetchContent), PLYReader private
  enum access (`typeSize` made a static member), LASzip detection (now via
  `find_path`/`find_library` for `laszip_api.h` + import lib rather than a
  guessed CMake target name).
- Was mid-verification that `pfconvert` ingests a real `.laz`
  (`C:\UnrealProject\model\NTPC.laz`). If it still logs "built without LASzip",
  reconfigure and check the `-- LASzip:` status line; ensure `laszip_api.dll` is
  beside `pfconvert.exe` at runtime (vcpkg usually auto-copies it).

## On-disk octree format (v1) — keep writer/reader in sync
A converted cloud directory contains:
- `meta.bin`     `FileMetadata` (magic "PFO1") — see src/common/OctreeFormat.h
- `metadata.json` human-readable copy
- `hierarchy.bin` array of `NodeRecord` (52 bytes each; explicit `children[8]`)
- `octree.bin`   concatenated per-node `PackedPoint` payloads (22 bytes each;
                 v2 added `classification` + pad — see src/common/PointFormat.h)
Octant numbering: `(x<<2)|(y<<1)|z`, 0 = low half. `childCube()` in
OctreeFormat.h is the single source of truth — used by both indexer and viewer.

## Known gaps / next steps (see docs/ARCHITECTURE.md §6)
1. Indexer is single-threaded; chunks are independent so Phase C parallelises well.
2. E57 reader uses libE57Format's version-sensitive Simple API — may need tweaks
   for the installed version (guarded by `PF_WITH_E57`; stubs if absent).
3. Coarse-tree build assumes the level-L sample fits in RAM (fine for typical L
   and real surface data; chunk the hierarchy for extreme cases).
4. Node payload compression (e.g. LAZ-per-node) — format reserves `byteSize`.
5. Eye-dome lighting / EDL post-process for depth perception.
6. No automated tests yet — a synthetic `.xyz` generator + round-trip test of the
   octree format would be a good first addition.

## Conventions
- `pfcore` must not depend on SDL/GL/ImGui (viewer-only). Keep readers/indexer
  free of GPU headers. CMake enforces this: `pfcore` links only `glm` + optional
  format libs; SDL2/GLEW/OpenGL/ImGui link into `pfview` alone.
- Positions are quantized (LAS-style scale/offset) on disk; the viewer uploads
  positions **relative to the octree cube centre** as float for GPU precision.
- **Out-of-core invariant**: every `PointReader` streams — never load a whole
  file into RAM. `read(out, maxPoints)` fills a batch and returns the count; keep
  calling until it returns 0. The indexer pulls batches; memory is bounded by the
  largest chunk, not the input size.
- Optional readers are compile-guarded: `PF_WITH_LAS` (LASzip), `PF_WITH_E57`
  (libE57Format). Absent lib → that reader is a graceful stub, project still
  builds. PLY + PTS/XYZ use **own parsers** (no external dep).

## Note on stale docs
README.md predates the current build and is wrong in places: it claims PLY uses
**tinyply** and that vcpkg pulls `imgui`/`tinyply` — neither is true. Actual:
PLY/text are own parsers; ImGui is FetchContent (pinned v1.91.5); `vcpkg.json`
lists only sdl2/glew/glm/laszip/libe57format. Trust this file and CMakeLists.txt
over README for the dependency story.
