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
| GUI overlay        | Dear ImGui                                   |
| Math               | GLM                                          |
| LAS/LAZ I/O        | LASzip (`laszip_api`)                        |
| E57 I/O            | libE57Format                                 |
| PLY I/O            | tinyply                                      |
| PTS/XYZ I/O        | built-in text parser                         |

PointForge is split into two programs that share a common core:

* **`pfconvert`** — the *importer*. Converts a source point cloud into a
  streamable, disk-based **octree** (LOD pyramid). Runs out-of-core: memory use
  is bounded regardless of input size.
* **`pfview`** — the *viewer*. Streams octree nodes on demand using frustum
  culling and screen-space-error LOD selection, so only the visible/relevant
  detail is ever resident on the GPU.

> ## Build status / honesty note
> This repository was authored as a complete, coherent C++ project but has **not
> been compiled in the environment that generated it** (no toolchain was
> available there). Treat it as a strong, well-structured starting point: expect
> to resolve a small number of compile issues as you wire up dependencies on your
> machine. The architecture, data formats, and algorithms are the durable value;
> the build is the easy part to iterate on. See `docs/ARCHITECTURE.md`.

---

## 1. Prerequisites

* A C++17 compiler (MSVC 2019+, Clang 12+, or GCC 10+)
* [CMake](https://cmake.org/) ≥ 3.20
* [vcpkg](https://github.com/microsoft/vcpkg) (recommended — supplies every
  third-party library through the manifest in `vcpkg.json`)

## 2. Building (Windows + vcpkg, recommended)

```powershell
# one-time: clone & bootstrap vcpkg somewhere
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat

# from the PointForge folder:
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The vcpkg manifest (`vcpkg.json`) pulls in `sdl2`, `glew`, `glm`, `imgui[sdl2-binding,opengl3-binding]`,
`laszip`, `libe57format`, and `tinyply` automatically during configure.

Binaries land in `build/Release/` (`pfconvert.exe`, `pfview.exe`).

## 3. Usage

### Import (convert to octree)

```powershell
pfconvert input.laz --out scans/mysite --chunk-depth 4 --spacing 0.0
```

* `--out <dir>`        output octree directory (created if missing)
* `--chunk-depth <n>`  coarse grid depth L for the disk-chunking pass; grid is (2^L)³ (default 4 → up to 4096 chunks)
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
  metadata.json     bounds, scale/offset, attribute layout, spacing, point count
  hierarchy.bin     packed octree node descriptors (see ARCHITECTURE)
  octree.bin        concatenated per-node point payloads
```

### View

```powershell
pfview scans/mysite
```

Controls: **W/A/S/D** move, **right-mouse drag** look, **Q/E** down/up,
**Shift** to move faster, **mouse wheel** adjust point size. The ImGui panel
shows resident nodes, points on GPU, and LOD budget controls.

## 4. Layout

```
PointForge/
  CMakeLists.txt
  vcpkg.json
  README.md
  docs/ARCHITECTURE.md
  src/
    common/      Vec3d, AABB, Point, attribute layout, logging
    io/          PointReader interface + LAS/E57/PLY/text readers + factory
    indexer/     out-of-core octree builder (chunking + sampling + writer)
    viewer/      SDL2/OpenGL/ImGui streaming renderer
    tools/
      pfconvert/ importer CLI entry point
  shaders/       point.vert / point.frag (GLSL 330 core)
```

## 5. License of dependencies

LASzip (LGPL/permissive), libE57Format (BSD), tinyply (public domain/Unlicense),
SDL2 (zlib), GLEW (modified BSD), GLM (MIT), Dear ImGui (MIT). Review each before
shipping commercially.
