# vxpc_feature.md — VXPC Package Implementation Plan

> **How to use this doc:** this is the living plan for expanding PointForge's native `.vxpc` package format into a fully-featured, high-performance binary container across 20 phases. Update the status board as phases are implemented. 
> Statuses: `PLANNED` / `IN PROGRESS` / `DONE` / `BLOCKED(reason)`.

## Status board

| Phase | Feature | Status | Notes |
|---|---|---|---|
| 1 | VXPC Core | DONE | Defined extended Header and Directory structures |
| 2 | Package Writer | DONE | Implemented Create, AddFile, AddMemory, WriteHeader, WriteDirectory |
| 3 | Package Reader | DONE | Implemented Open, Validate, OpenStream, GetOffset, GetSize |
| 4 | Loader Integration | DONE | OctreeStore transparently routes paths to folder or .vxpc PackageReader |
| 5 | Thumbnails | DONE | CLI accepts --thumbnail <path>, falls back to synthetic 256x256 RGB projection block inside .vxpc |
| 6 | Project Metadata | DONE | ProjectMetadata struct implemented in OctreeFormat.h and embedded inside the .vxpc container via MetadataWriter |
| 7 | Camera Data | PLANNED | Bookmarks, Views, Paths, Animation |
| 8 | Measurements | PLANNED | Distance, Area, Volume, Polyline |
| 9 | Annotations | PLANNED | Text, Image, Audio, Pins |
| 10 | Multi Cloud | PLANNED | Support multiple point clouds inside one package |
| 11 | Plugin Data | PLANNED | Namespaced arbitrary binary blobs |
| 12 | Custom Metadata | DONE | PackageWriter supports dynamic Key/Value addition serialized as custom_meta.json |
| 13 | Streaming Support | PLANNED | Range-request friendly layout |
| 14 | Chunked Octree | PLANNED | Multi-chunk octree layout support |
| 15 | Compression | DONE | ZSTD compression added to PackageWriter AddMemory/AddFile APIs |
| 16 | Checksums | DONE | Implemented CRC32 computation per-file, computed by Writer, validated by Reader on Read() |
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
**Goal**: Pack user navigation states into the `.vxpc` container.
- **Research & Implementation**: 
  - Data structures: `CameraBookmark` (pos, yaw, pitch, ortho), `CameraPath` (spline control points, timestamps).
  - Currently stored in loose `bookmarks.txt` and `campaths.txt`.
  - To implement: In `PackageWriter`, add an `AddJsonList("bookmarks.json", bookmarks)` API. 
  - During `PackageReader::Open()`, read these blocks into memory. The Viewer UI needs a "Save to VXPC" button, which implies we need an "Append to VXPC" functionality in `PackageWriter` (or a shadow file that gets repacked later).

## Phase 8: Measurements
**Goal**: Save engineering measurement artifacts inside the package.
- **Research & Implementation**: 
  - Measurements (distance, area, polyline) are currently transient or saved to CSV. 
  - Implementation: Define a unified `Measurement` JSON schema. Store as `measurements.json` via `PackageWriter`.
  - Requires the `PackageWriter` to support in-place updates or the application must rewrite the `.vxpc` footer when measurements are added. A simpler approach is storing edits in an external sidecar file until "Save Project" is hit, triggering a repack.

## Phase 9: Annotations
**Goal**: Embed collaboration markers directly in the package.
- **Research & Implementation**: 
  - `annotations.json` is currently written to AppData.
  - Implementation: Move `annotations.json` into the `.vxpc` archive. 
  - Future expansion: Audio and Image attachments. `PackageWriter::AddFile("annotations/image_1.jpg", path)` can embed external files. `PackageReader` can stream these back into ImGui textures (`UTexture2D` in Unity).

## Phase 11: Plugin Data
**Goal**: Ensure extensibility for 3rd party tools without breaking the core parser.
- **Research & Implementation**: 
  - Provide a `/plugins/<vendor>/` namespace convention in the directory entries.
  - Expose a C-API `PF_Package_WritePluginData(pkg, "vendor/data.bin", buffer, size)` for native plugin DLLs (like Unity tools) to inject their own state.
  - Core PointForge completely ignores files prefixed with `/plugins/`.

## Phase 13: Streaming Support
**Goal**: Cloud-native access via HTTP Range Requests.
- **Research & Implementation**: 
  - The `.vxpc` layout (Header -> Data -> Directory) is already streaming-friendly.
  - Implementation: Modify `PackageReader` to accept a `URL` instead of a local file path.
  - Use `libcurl` or Windows HTTP APIs to fetch the last 4KB (to read the directory offset), then fetch the directory, then fulfill `PackageReader::Read(offset, size)` with HTTP `Range: bytes=offset-(offset+size-1)` requests.
  - Needs a caching layer to coalesce small byte reads.

## Phase 20: Documentation
**Goal**: Formal specification.
- **Research & Implementation**: 
  - Create a formal RFC-style Markdown document for `.vxpc`.
  - Document the magic bytes, versioning, compression enum maps, endianness (Little Endian), and memory alignment guarantees.

---

# Long-Term / High Complexity Phases (Keep at Last)

> **Note**: The following phases require massive architectural shifts, multi-month development, or significant refactoring. They are deferred to the end of the roadmap and should not be implemented at this moment.

## Phase 10: Multi Cloud
**Goal**: Store entire project scenes inside a single `.vxpc`.
- **Why it's kept at last**: PointForge's core rendering and octree structures assume 1 root per package. Supporting N roots means rewriting the indexer to merge bounds, offset point coordinates, and maintain a Scene Graph inside the `.vxpc`.

## Phase 14: Chunked Octree
**Goal**: Pave the way for out-of-core massive datasets exceeding 1TB.
- **Why it's kept at last**: Requires splitting `octree.bin` into `octree_chunk_X.bin`. The viewer's `OctreeStore` would need a virtualized LRU cache for chunk files, which significantly complicates the currently elegant memory-mapped streaming parser.

## Phase 17: Recovery & Atomic Saves
**Goal**: Fail-safe generation and crash recovery.
- **Why it's kept at last**: Requires a journaling system inside the `.vxpc`. Modifying a 100GB package safely means either full file duplication (too slow) or append-only block chains (requires a garbage collection phase and complicates read offsets).

## Phase 18: Encryption
**Goal**: Data security via AES.
- **Why it's kept at last**: Streaming decryption of AES-CTR or AES-GCM requires strict block-aligned reads. Random access point-picking would have to read block boundaries, decrypt, and then extract the point. Significant performance penalty.

## Phase 19: Virtual File System
**Goal**: Fully hierarchical container acting like a ZIP tree.
- **Why it's kept at last**: Right now `VXPCDirectoryEntry` is a flat array. Implementing true hierarchy means building a Trie or nested directory tables, matching path resolution logic, and handling relative paths—overkill for our current flat list needs.

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
