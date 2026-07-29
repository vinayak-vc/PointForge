# gsplat_feature.md — 3D Gaussian Splatting (3DGS) Implementation Plan

> **How to use this doc (per AGENTS.md):** this is the authoritative technical implementation plan for adding **3D Gaussian Splatting (3DGS)** support to ViitorX PointCloud Viewer (`PointForge`). Update the status board as phases are implemented.
> Statuses: `PLANNED` / `IN PROGRESS` / `DONE` / `BLOCKED(reason)`.

---

## 1. Executive Summary & Goals

3D Gaussian Splatting (3DGS) enables photorealistic, continuous surface rendering from spatial captures and photogrammetry datasets. While standard LiDAR scans (LAS/LAZ/E57) provide precise geometry, 3DGS provides photorealistic contextual environments.

**Key Goals:**
1. **Multi-Cloud Coexistence:** Load `.splat` and 3DGS `.ply` datasets as native layers inside the existing **Scene Panel** (`SceneCloud`), allowing LiDAR point clouds and Gaussian Splats to be rendered simultaneously in the same 3D workspace.
2. **High-Performance Alpha-Blended Splatting:** Implement a dedicated GPU splatting renderer with per-frame back-to-front depth sorting and anisotropic Gaussian evaluation in screen space.
3. **Zero-Friction Streaming & VR Synergy:** Ensure 3DGS scenes work out-of-the-box with **Web Remote WebRTC/JPEG streaming** (phone/tablet LAN control), **Stereoscopic SBS**, and **Camera Path MP4 export**.

---

## 2. Status Board

| Phase | Feature | Status | Owner | Notes |
|---|---|---|---|---|
| 1 | **3DGS File Parser (.splat & .ply)** | `DONE` | branch `feat/3dgs-phase1` | Parsers for `.splat` 32-byte binary and 3DGS `.ply` implemented (`src/io/SplatReader.{h,cpp}`); unit tests verified green in `pftest` ✔ |
| 2 | **GPU Splatting Pipeline & Shaders** | `DONE` | branch `feat/3dgs-phase1` | Instanced `SplatRenderer` class (`src/viewer/SplatRenderer.{h,cpp}`) & 2D covariance shaders (`EmbeddedShaders.h`) implemented ✔ |
| 3 | **Per-Frame Depth Sorting** | `DONE` | branch `feat/3dgs-phase1` | Back-to-front depth sorting implemented in `SplatRenderer::sortSplats()` using camera-space depth $z = (V \cdot p)_z$ ✔ |
| 4 | **Scene Panel & Layer Integration** | `DONE` | branch `feat/3dgs-phase1` | Registered `CloudType::GaussianSplat` in `SceneCloud`, wired `SplatReader` into `loadOctree`/`appendCloud`, and added rendering leg to `main.cpp` ✔ |
| 5 | **EDL & Post-Process Routing** | `DONE` | branch `feat/3dgs-phase1` | Integrated splat rendering into offscreen scene FBO (`edlFbo`) for seamless composition with EDL & Web Remote video encoder ✔ |
| 6 | **Web Remote & Stereoscopic SBS** | `DONE` | branch `feat/3dgs-phase1` | Integrated 3DGS rendering into SBS stereo eye-passes & WebRTC H.264 / JPEG video streaming server ✔ |
| 7 | **VXPC Package Container Support** | `DONE` | branch `feat/3dgs-phase1` | Added ZSTD memory-buffer extraction for `splat.bin` datasets packaged in single-file `.vxpc` project files ✔ |
| 8 | **Automated Testing & Benchmarks** | `DONE` | branch `feat/3dgs-phase1` | Added synthetic `.splat`, 3DGS `.ply`, and `.vxpc` package test runner `testSplatReader()` in `pftest` (`pftest: PASS`) ✔ |

---

## 3. Detailed Phase Implementation

### Phase 1: 3DGS File Parser (`.splat` & `.ply`)
- **Objective:** Support both standard formats used by the 3DGS ecosystem.
- **Implementation Details:**
  - **`.splat` binary format:** Stride-based format (position `xyz`, scales, RGBA/SH, quaternion rotation).
  - **3DGS `.ply` format:** Parse custom vertex properties (`f_dc_0..2`, `f_rest_0..44`, `opacity`, `scale_0..2`, `rot_0..3`).
  - **Code Anchor:** Create `src/indexer/SplatLoader.h` and `src/indexer/SplatLoader.cpp` returning a memory-resident `SplatCloudData` buffer.

### Phase 2: GPU Splatting Pipeline & Shaders (`SplatRenderer`)
- **Objective:** Render anisotropic Gaussians as screen-space quads with accurate covariance evaluation.
- **Implementation Details:**
  - Create `src/viewer/SplatRenderer.h` / `src/viewer/SplatRenderer.cpp` alongside existing `PointRenderer`.
  - **Vertex/Geometry Shader:** Transform 3D covariance $\Sigma = R S S^T R^T$ to 2D screen-space covariance $\Sigma' = J W \Sigma W^T J^T$. Compute bounding quad extents based on eigenvalues of $\Sigma'$.
  - **Fragment Shader:** Evaluate Gaussian falloff $e^{-\frac{1}{2} (p - \mu)^T (\Sigma')^{-1} (p - \mu)}$ and apply alpha blending with Spherical Harmonics (SH0 baseline, optional SH1–SH3 view-dependent color).

### Phase 3: Per-Frame Depth Sorting
- **Objective:** Correct back-to-front alpha compositing.
- **Implementation Details:**
  - Unlike standard LiDAR point clouds where order is arbitrary, Gaussians require sorted alpha blending.
  - For standard scenes (~1M–5M splats), implement a GPU Radix Sort (via Compute Shader) or a high-speed parallel CPU sort using camera-space depth $z = (V \cdot p)_z$.
  - Re-sort only when camera position/orientation changes above an angular/distance threshold.

### Phase 4: Scene Panel & Layer Integration (`SceneCloud`)
- **Objective:** Multi-cloud scene coexistence.
- **Implementation Details:**
  - Extend `SceneCloud` struct in `src/viewer/ScenePanel.h` with an enum `CloudType::OctreeLiDAR` vs `CloudType::GaussianSplat`.
  - In `main.cpp` render loop, iterate visible `SceneCloud` items:
    - Render LiDAR point clouds via `PointRenderer` (writing depth).
    - Render Gaussian Splats via `SplatRenderer` (with depth-test ON, depth-write OFF) so LiDAR points correctly occlude splats when in front.

### Phase 5: EDL & Post-Process Routing
- **Objective:** Prevent Eye-Dome Lighting artifacts on smooth Gaussian surfaces.
- **Implementation Details:**
  - Eye-Dome Lighting (`EdlPass`) enhances depth edges for uncoloured LiDAR scans.
  - Route rendering such that LiDAR clouds render to the EDL framebuffer, EDL is evaluated, and then Gaussian Splats are composited on top using the depth buffer.

### Phase 6: Web Remote & Stereoscopic SBS Compatibility
- **Objective:** Seamless remote and VR experience.
- **Implementation Details:**
  - Because Web Remote captures desktop framebuffer output (`RemoteServer` encoding via NVENC/Media Foundation), 3DGS streaming to phone/tablet browsers works without client-side GPU splatting.
  - For **Stereoscopic SBS**, ensure `SplatRenderer` respects left/right eye camera view matrices and viewport clipping.

### Phase 7: VXPC Package Container Support
- **Objective:** Package 3DGS datasets into single `.vxpc` project files.
- **Implementation Details:**
  - Extend `PackageWriter` and `PackageReader` (`docs/vxpc_feature.md` Phase 10 multi-cloud pattern) to store `.splat` data under `clouds/<i>/splat.bin` with `scene.json` identifying `type: "gsplat"`.

### Phase 8: Automated Testing & Benchmarks (`pftest`)
- **Objective:** Maintain zero-regression quality.
- **Implementation Details:**
  - Add synthetic Gaussian cluster generator in `src/tools/pftest/main.cpp`.
  - Add CTest test `gsplat_loader_test` validating `.splat` and `.ply` parsing, covariance math invariants, and bounding box calculations.

---

## 4. Acceptance Criteria

1. **Visual Acceptance:** Load a standard 3DGS benchmark scene (e.g., Garden or Bicycle `.splat` / `.ply`) alongside a LiDAR point cloud in the Scene Panel.
2. **Performance Budget:** Maintain >= 60 FPS at 1080p for a 2M-splat scene on baseline desktop GPUs (NVIDIA RTX 3060 / AMD equivalent).
3. **Streaming Acceptance:** Connect mobile browser via Web Remote (`http://<LAN-IP>:8080`) and verify smooth touch-orbiting of the Gaussian scene with H.264 video streaming.
4. **Non-Regression:** Standard LAS/LAZ octree conversion and LiDAR viewing remain byte-identical and unaffected by the splatting pipeline.
