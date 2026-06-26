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
  "version": 1,
  "pointCount": 4200000000,
  "boundingBox": { "min": [..], "max": [..] },     // true AABB (double)
  "cube":        { "min": [..], "size": 1234.5 },   // octree cube
  "scale":  [0.001, 0.001, 0.001],                  // quantization (like LAS)
  "offset": [..],
  "rootSpacing": 9.64,
  "attributes": [
    { "name": "position", "type": "int32",  "components": 3 },
    { "name": "rgb",      "type": "uint16", "components": 3 },
    { "name": "intensity","type": "uint16", "components": 1 }
  ],
  "bytesPerPoint": 20
}
```

Positions are stored **quantized** to int32 using `scale`/`offset` exactly like
LAS, so coordinates are compact and lossless to the source precision. The viewer
dequantizes to float (relative to the cube centre to avoid precision loss on
large UTM-style coordinates).

### `hierarchy.bin`
A flat array of fixed-size node records, depth-first. Each record:

```
struct NodeRecord {        // 32 bytes, little-endian
    uint8_t  name[1]...     // encoded path is implicit by index; see below
    uint8_t  childMask;     // bit i set => child octant i exists
    uint8_t  level;
    uint8_t  _pad;
    uint32_t pointCount;    // points stored *in this node*
    uint64_t byteOffset;    // offset of this node's payload in octree.bin
    uint32_t byteSize;      // payload size in bytes (== pointCount*bytesPerPoint)
    uint32_t firstChild;    // index of first child record, or 0xFFFFFFFF
};
```

`childMask` + `firstChild` lets the viewer walk the tree and know exactly which
of the eight octants exist without probing. (For multi-billion-point clouds the
hierarchy itself can be chunked into sub-trees; v1 keeps it as one file, which is
fine into the low billions — ~32 bytes per node, and nodes are far fewer than
points.)

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

## 6. Extension points (deliberate TODOs)

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
