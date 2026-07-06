# PointForge Architecture

This document explains *why* the importer is built the way it is. The viewer is
comparatively simple; the importer is where the engineering lives, because the
whole point is to handle inputs that do **not** fit in memory.

## 1. The core problem: out-of-core indexing

A naïve importer reads every point into a `std::vector` and builds an octree in
RAM. That dies at a few hundred million points. To reach billions we must never
hold the whole cloud in memory at once, and we must produce an on-disk structure
the viewer can stream.

PointForge uses the well-proven **three-phase** strategy popularised by the
Potree converter and by Euclideon-style "unlimited detail" pipelines:

```
 Phase A  COUNT      one streaming pass: global bounding box + total point count
 Phase B  CHUNK      one streaming pass: bin every point into coarse grid cells,
                     each written to its own file on disk → "chunks"
 Phase C  INDEX      per chunk (independently, in parallel):
                     load chunk → build a local octree with subsampling →
                     append node payloads + record hierarchy
```

Memory is bounded by **the largest single chunk**, not by the input size. Choose
the chunk grid depth so that the densest chunk fits comfortably in RAM (a few
million points). Phase B guarantees chunks are spatially disjoint, so Phase C can
run each chunk on a separate thread with no locking on the point data.

### Why a cube bounding box

The octree is defined over a **cube** (equal extents on all axes). We expand the
true AABB to a cube so that child subdivision is a clean halving in every
dimension and node sizes are predictable across LOD. The true AABB is still
stored in metadata for display.

## 2. The octree / LOD model

We build a **Modifiable Nested Octree (MNO)**-style LOD, the same family Potree
uses:

* Every node covers a cubic region. The root covers the whole cube.
* Each node stores a **subsample** of the points in its region such that no two
  retained points are closer than the node's *spacing*. Spacing **halves** with
  each level, so deeper nodes are denser.
* Points not retained at a level are pushed down to the appropriate child, where
  the spacing test is finer. A point therefore lives in exactly one node.
* The union of a node and all its ancestors is a complete, progressively refined
  representation of that region. The viewer renders a node **plus its ancestors**,
  so partial loading still looks correct.

Subsampling uses a **3D grid (Poisson-disk approximation)**: lay a uniform grid
with cell size = spacing over the node's cube; keep the first point that lands in
each empty cell, demote the rest to children. This is O(n) per node and gives
visually even density, which is what the screen-space-error metric in the viewer
assumes.

### Spacing

Root spacing defaults to `cubeSize / 128` (configurable via `--spacing`). Level
`L` uses `rootSpacing / 2^L`. A node stops subdividing when it holds fewer than
`maxPointsPerNode` (default ~50k) or the spacing reaches the data's intrinsic
resolution.

## 3. On-disk format

A converted cloud is a directory:

### `metadata.json`
```json
{
  "version": 2,
  "pointCount": 4200000000,
  "boundingBox": { "min": [..], "max": [..] },     // true AABB (double)
  "cube":        { "min": [..], "size": 1234.5 },   // octree cube
  "scale":  [0.001, 0.001, 0.001],                  // quantization (like LAS)
  "offset": [..],
  "rootSpacing": 9.64,
  "attributes": [
    { "name": "position",      "type": "int32",  "components": 3 },
    { "name": "rgb",           "type": "uint16", "components": 3 },
    { "name": "intensity",     "type": "uint16", "components": 1 },
    { "name": "classification","type": "uint8",  "components": 1 }
  ],
  "bytesPerPoint": 22   // sizeof(PackedPoint): int32*3 + uint16*4 + uint8*2
}
```

Positions are stored **quantized** to int32 using `scale`/`offset` exactly like
LAS, so coordinates are compact and lossless to the source precision. The viewer
dequantizes to float (relative to the cube centre to avoid precision loss on
large UTM-style coordinates).

### `hierarchy.bin`
A flat array of fixed-size node records, depth-first, root at index 0. Each
record is the `NodeRecord` struct in `src/common/OctreeFormat.h` (the single
source of truth; a `static_assert` pins the size):

```
struct NodeRecord {        // 52 bytes, little-endian, #pragma pack(1)
    uint8_t  level;        // 0 = root
    uint8_t  childMask;    // bit o set => children[o] != kNoChild (redundant, handy)
    uint16_t reserved;
    uint32_t pointCount;   // points stored *in this node*
    uint64_t byteOffset;   // offset of this node's payload in octree.bin
    uint32_t byteSize;     // payload size in bytes (raw, or compressed if < raw)
    uint32_t children[8];  // global node index per octant, or kNoChild (0xFFFFFFFF)
};
```

Child indices are stored **explicitly** (`children[8]`), not as a `firstChild` +
contiguous-rank scheme: the out-of-core builder writes chunk subtrees and coarse
nodes in separate phases, so a node's children are not necessarily contiguous on
disk. `childMask` is a convenience mirror of which `children[o]` entries are set.
(For multi-billion-point clouds the hierarchy itself can be chunked into
sub-trees; v1 keeps it as one file, which is fine into the low billions —
52 bytes per node, and nodes are far fewer than points.)

### `octree.bin`
Concatenated payloads. Node `i`'s points are
`octree.bin[byteOffset .. byteOffset+byteSize)`, laid out as tightly packed
records of `bytesPerPoint` following the `attributes` layout.

## 4. Octant numbering

Child octant index is a 3-bit number `(x<<2)|(y<<1)|z` where each bit is 0 for the
low half and 1 for the high half of the parent cube along that axis. The same
convention is used in the builder and the viewer — do not change one without the
other.

## 5. Viewer streaming

`pfview` keeps the hierarchy resident (small) and streams payloads:

1. **Traverse** the octree top-down each frame.
2. **Frustum cull** using each node's cube.
3. **Screen-space error**: estimate a node's projected spacing in pixels. If it
   exceeds a budget (e.g. 1px) and the node has children, descend; otherwise stop.
4. For every node selected to render that isn't resident, enqueue an async load.
   A worker thread `pread`s the payload from `octree.bin` and hands the bytes back;
   the main thread uploads to a VBO.
5. Render resident nodes as `GL_POINTS`. A node is drawn together with its
   ancestors (which are always resident because traversal visits them first).
6. **Eviction**: an LRU cap on GPU bytes frees the least-recently-visible nodes'
   VBOs when over budget.

The point shader sizes points by distance (`gl_PointSize`) so density looks even,
and supports an optional fixed-pixel mode. Coordinates are uploaded relative to
the cube centre as `float`; the model matrix re-adds the centre in double on the
CPU side to keep large-coordinate clouds stable.

## 7. Viewer UI shell (docked, job-queue based)

`pfview`'s UI is a viewport-centric docked shell built on ImGui's docking
branch (`v1.91.5-docking`, `ImGuiConfigFlags_DockingEnable` +
`DockSpaceOverViewport`-style passthru central node): menu bar + thin
toolbar + central 3D viewport (never occluded) + a right "Properties" dock
(collapsing sections, not tabs — tool options append rather than replace, so
Display settings stay reachable mid-Measure) + a bottom dock holding
Jobs/Console/Performance (closed by default) + a status bar. This replaced an
earlier single scrolling 350px panel that mixed viewer and converter settings.

**Why not top-level page tabs**: the viewport is the application — a tab that
hides it to show converter or settings controls amputates the one thing that
justifies the app, and cannot represent a long-running background job (which
needs to stay visible while the user keeps navigating). Every reference
desktop tool in this space (Unreal, Unity, Blender, Visual Studio,
CloudCompare) converges on the same menu+dock shell for this reason.

**Conversion as a background job, not a modal operation**: `src/viewer/Jobs.h`
defines a `JobQueue` — one worker thread pulls `ConvertJob`s sequentially
(conversion is disk/CPU bound; parallel jobs would fight for the same
resources) and calls the existing `buildOctree()` with a per-job
`progressCb`/`cancel` atomic, exactly the same contract `IndexOptions` already
exposed for the old inline converter. The Convert dialog only *enqueues*; all
progress/cancel/failure surfaces through the Jobs panel and a status-bar pill.
This is deliberately the same abstraction that will carry batch conversion
(multi-job enqueue, no new UI) and AI segmentation (another job type) later.

**Logging fan-out**: `pf::setLogSink()` (added to `src/common/Log.h/.cpp`) lets
the viewer mirror every `pf::log()` call — including from the converter's
worker thread — into `src/viewer/UiLog.h`'s ring buffer, which backs the
Console panel. The original stdout/stderr sink is unchanged; the UI sink is
additive and thread-safe (same mutex-guarded pattern as the existing logger).

**Stereoscopic SBS = zero chrome**: when SBS is active, the entire ImGui frame
for the shell (menu, toolbar, docks, status bar, watermark, measure overlay,
colour legend) is suppressed — only a fading "F9/Esc to exit" hint is drawn,
once per eye viewport, so it fuses correctly through a stereoscope. Hotkeys
(F9, Esc) still process normally; only rendering is suppressed.

## 8. Web Remote (embedded server + viewport streaming)

`RemoteServer` (src/viewer/RemoteServer.cpp, guarded by `PF_WITH_REMOTE`) embeds
a civetweb HTTP+WebSocket server in the viewer. The React app (`webremote/`,
Vite build) is embedded into the exe via a generated `EmbeddedWeb.h`
(`PF_EMBED_WEB`) — **any webremote change requires an exe rebuild**. Protocol is
JSON over WS: `hello{pin}`, `move` @30 Hz, `cmd`, `set`, plus `cfg`/`state`
broadcasts back.

Two viewport stream engines, selected by the `preferredStream` cfg key:

* **JPEG** (`PF_REMOTE_STREAM`): turbojpeg-compressed frames pushed as binary
  WS messages. Robust, per-frame crisp, ~12–18 Mbps at the "med" preset.
* **WebRTC H.264** (`PF_REMOTE_WEBRTC`): libdatachannel + Media Foundation.
  Browser is the offerer; the server **mirrors the offer's video mid + H264
  payload type + fmtp** when building its answer track (libdatachannel matches
  tracks to m-lines strictly by mid — a mismatched mid makes the answer reject
  the video line). RTP SSRC fixed at 1111; Annex-B NALs fed to
  `H264RtpPacketizer`.

Encoding runs on a dedicated thread (`encodeLoop`, latest-frame slot — the main
thread never blocks). Encoder selection is **hardware-first**:
`MFTEnumEx(MFT_ENUM_FLAG_HARDWARE)` (NVENC/QuickSync/VCE) driven through the
async-MFT event model (`METransformNeedInput`/`HaveOutput`, non-blocking pump,
bounded 150 ms input-credit wait → frame drop when backed up), falling back to
the synchronous software `CLSID_CMSH264EncoderMFT` on any init or runtime
failure (sticky `mfHwFailed` flag — the stream degrades, never crashes).
Rate control: quality-based VBR (quality 78) with a 12 Mbps average-bitrate
hint; GOP 30 for a 1 s keyframe cadence so clients joining mid-stream recover.

**Input**: `webremote/src/FlyTab.tsx` drives the 30 Hz `move` message from two
parallel paths — the original multi-touch gestures (1-finger look, 2-finger
pinch zoom/pan) and a `matchMedia('(pointer: fine)')`-gated desktop path
(mouse-drag look, right-drag pan, wheel zoom, WASD/Space/Ctrl/Shift keyboard
fly). Both write into the same `moveRef`/`heldState` the 30 Hz loop reads —
neither path knows the other exists.

**Remote measurement picking**: while the server's Measure tool is active,
tapping the video sends a `measure_pick` cmd carrying the tap's normalized
(0..1) position *within the video content* (`VideoLayer.getNaturalSize()`
inverts the `object-fit:contain` letterboxing client-side). The server maps
that straight onto window pixel coordinates (`nx*winW`, `ny*winH`) and feeds
the same `pendingPick` → `cam.screenRay` → `store.pickPoint` path a local LMB
click already uses — no separate remote ray-casting implementation, and the
video stream is guaranteed to share the local window's aspect ratio (it's a
downscaled copy of the same rendered frame), so the mapping is exact.

## 9. Extension points (deliberate TODOs)

* **LAZ via PDAL**: `laszip_api` covers LAS/LAZ directly; swap to PDAL if you need
  exotic formats.
* **Compression of node payloads** (e.g. LAZ-per-node or Brotli) — the format
  reserves `byteSize` per node so compressed payloads drop in with only the loader
  changing.
* **Hierarchy chunking** for >~10 nodes-in-billions clouds.
* **Additional attributes** (normals, GPS time, classification) — add to the
  attribute layout; readers and the packer already iterate the layout generically.
* **EDL / eye-dome lighting** post-process for better depth perception (a fullscreen
  pass, same pattern as Axiom Present's SSAO/TXAA effects).
