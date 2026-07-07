# vxpc_feature.md — VXPC Package Implementation Plan

> **How to use this doc:** this is the living plan for expanding PointForge's native `.vxpc` package format into a fully-featured, high-performance binary container across 20 phases. Update the status board as phases are implemented. 
> Statuses: `PLANNED` / `IN PROGRESS` / `DONE` / `BLOCKED(reason)`.

## Status board

| Phase | Feature | Status | Notes |
|---|---|---|---|
| 1 | VXPC Core | PLANNED | Define extended Header and Directory structures |
| 2 | Package Writer | PLANNED | Create, AddFile, AddMemory, Finalize |
| 3 | Package Reader | PLANNED | Open, Validate, Contains, Read, GetOffset, GetSize |
| 4 | Loader Integration | PLANNED | Transparent folder vs .vxpc fallback |
| 5 | Thumbnails | PLANNED | Embedded previews |
| 6 | Project Metadata | PLANNED | Structured binary metadata (UUID, CRS, EPSG) |
| 7 | Camera Data | PLANNED | Bookmarks, Views, Paths, Animation |
| 8 | Measurements | PLANNED | Distance, Area, Volume, Polyline |
| 9 | Annotations | PLANNED | Text, Image, Audio, Pins |
| 10 | Multi Cloud | PLANNED | Support multiple point clouds inside one package |
| 11 | Plugin Data | PLANNED | Namespaced arbitrary binary blobs |
| 12 | Custom Metadata | PLANNED | Arbitrary Key/Value pairs |
| 13 | Streaming Support | PLANNED | Range-request friendly layout |
| 14 | Chunked Octree | PLANNED | Multi-chunk octree layout support |
| 15 | Compression | PLANNED | LZ4/ZSTD integration per file |
| 16 | Checksums | PLANNED | CRC32 per entry validation |
| 17 | Recovery | PLANNED | Crash recovery, rollback, atomic saves |
| 18 | Encryption | PLANNED | AES reserved flags |
| 19 | Virtual File System | PLANNED | True VFS hierarchy (`/clouds/`, `/images/`, `/plugins/`) |
| 20 | Documentation | PLANNED | Finalise `docs/vxpc.md` full spec |

---

## Phase 1: VXPC Core
**Goal**: Establish the fundamental binary layout and structures.
- **Header**: 
  - `Magic`: `"VXPC"`
  - `Version`: `1`
  - Fields: `directoryOffset`, `directorySize`, `entryCount`, `packageFlags`, `uuid`, `createdTime`, `modifiedTime`, `converterVersion`, `reserved`.
- **Directory Entry**:
  - `filename`: 64 bytes (fixed)
  - `offset`: uint64
  - `compressedSize`: uint64
  - `originalSize`: uint64
  - `compression`: uint32
  - `crc32`: uint32
  - `flags`: uint32
  - `userFlags`: uint32
- **Interfaces**: `VirtualFileSystem`, `PackageStream`.

## Phase 2: Package Writer
**Goal**: Build the archival interface to construct `.vxpc` files.
- **API**: `Create()`, `AddFile()`, `AddMemory()`, `Finalize()`, `WriteHeader()`, `WriteDirectory()`.
- **Targets**: Capable of writing `metadata.json`, `meta.bin`, `hierarchy.bin`, `octree.bin`.
- **Constraint**: No internal compression yet. The resulting file should be `Scan.vxpc`.

## Phase 3: Package Reader
**Goal**: Memory-map friendly retrieval system.
- **API**: `Open()`, `Validate()`, `Contains()`, `OpenStream()`, `Read()`, `GetOffset()`, `GetSize()`, `ReadMetadata()`, `ReadBinary()`.
- **Constraint**: Reads directly from package. Zero extraction.

## Phase 4: Loader Integration
**Goal**: PointForge natively transparently loads from either structure.
- **Routing**: `OctreeStore` detects path extension. If `.vxpc`, route to `PackageReader`. Otherwise, standard folder.
- **Constraint**: Everything above the storage layer must remain unchanged. The octree parser should never know where the bytes came from.

## Phase 5: Thumbnails
**Goal**: Embed visual previews directly inside the package.
- Add `thumbnail.jpg`, `preview.jpg`.
- Converter automatically captures a representative thumbnail during indexing. Reader exposes `GetThumbnail()`.

## Phase 6: Project Metadata
**Goal**: Formalize structural project data in binary form.
- **Fields**: Project Name, Author, Company, Description, Tags, Creation Date, Modified Date, Units, Coordinate System, EPSG, Source File, Converter Version, Build Version, UUID.
- Retain JSON export purely for debugging. Store as binary natively.

## Phase 7: Camera Data
**Goal**: Pack user navigation states.
- Support saving: Bookmarks, Saved Views, Camera Paths, Timeline data, and future animation tracks.

## Phase 8: Measurements
**Goal**: Save engineering measurement artifacts inside the package.
- Storage for: Distance, Area, Volume, Polyline, Height, Radius, and future types.

## Phase 9: Annotations
**Goal**: Embed collaboration markers.
- Support: Text, Image, Audio, Issue Marker, Arrow, Label, Pinned Note.

## Phase 10: Multi Cloud
**Goal**: Store entire project scenes inside a single `.vxpc`.
- Support Cloud 1...N.
- Each cloud maintains its own: metadata, hierarchy, octree, bounding box, statistics, transform, visibility.

## Phase 11: Plugin Data
**Goal**: Ensure extensibility for 3rd party tools.
- Implement a `/plugins/` namespace.
- Plugins may store arbitrary binary blobs. Core ignores contents.

## Phase 12: Custom Metadata
**Goal**: Flexible user-defined schema.
- Arbitrary key/value metadata. E.g., `Scanner=BLK360`, `Weather=Sunny`, `Temperature=41`, `Operator=John`.
- No hardcoded schema.

## Phase 13: Streaming Support
**Goal**: Cloud-native access.
- `PackageReader` should support `Read(offset, size)` without loading the entire package.
- Design layout suitable for HTTP Range Requests, NAS, Cloud Storage, and future WebRTC streaming.

## Phase 14: Chunked Octree
**Goal**: Pave the way for out-of-core massive datasets.
- Prepare format for future chunked octree storage (no implementation yet, just layout readiness).
- Design directory so multiple `octree_chunk_X.bin` nodes are supported.

## Phase 15: Compression
**Goal**: Reduce footprint.
- Design API and implement support for `None`, `Reserve`, `LZ4`, `ZSTD`.
- Compression flag exists now; implementation follows later.

## Phase 16: Checksums
**Goal**: Data integrity.
- Compute and store `CRC32` per directory entry.
- Validate while opening to detect corruption.

## Phase 17: Recovery
**Goal**: Fail-safe generation.
- Design format to allow: Atomic Save, Recovery after crash, Directory duplication, Rollback.

## Phase 18: Encryption
**Goal**: Data security.
- Reserve package flags for `AES` (no encryption implementation yet).

## Phase 19: Virtual File System
**Goal**: Fully hierarchical container.
- Package should behave like a filesystem (`/`, `metadata`, `clouds/`, `images/`, `annotations/`, `plugins/`, `project/`) rather than exposing only four files.

## Phase 20: Documentation
**Goal**: Formal specification.
- Create/finalize `docs/vxpc.md`.
- Include binary layout, header, directory, versioning, upgrade strategy, examples, future reserved fields, memory mapping, and streaming workflow.

---

## Testing Verification

Each phase must include dedicated unit tests before proceeding to the next.
Tests must comprehensively verify:
- Package creation and opening
- CRC detection and validation
- Offset and size correctness
- Corruption detection
- Invalid magic handling
- Unsupported version errors
- Missing entries and duplicate filenames
- Backward compatibility with legacy folder structures
