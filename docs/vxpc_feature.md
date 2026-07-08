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
| 7 | Camera Data | DONE | bookmarks.json + campaths.json in the package; `RepackPackage` (verbatim copy + upsert/remove); viewer File > "Save Camera Data to Package" + load-on-open; pftest `testRepack` |
| 8 | Measurements | DONE | measurements.json (polyline) in the package via the shared repack; loaded for the first cloud; folded into File > "Save Project Data to Package"; pftest measurements round-trip |
| 9 | Annotations | DONE | annotations.json (per-cloud) embedded via repack; authoritative on open; in File > "Save Project Data to Package"; pftest round-trip incl. quoted-label JSON safety. Image/audio attachments deferred |
| 10 | Multi Cloud | DONE (packaging) | N clouds in one `.vxpc` under `clouds/<i>/` + top-level `scene.json`; `combineClouds()` packer + `pfconvert --combine`; `OctreeStore::load(dir, prefix)`; viewer opens the manifest into the #6 scene. pftest `testMultiCloudPackage` + real 2-cloud viewer load. Re-index-time merging still deferred |
| 11 | Plugin Data | DONE | `plugins/` namespace via `AddPluginData`; reader `ListEntries`/`ListPlugins`; core ignores non-fixed names; over-long filename guard; pftest `testPluginData` |
| 12 | Custom Metadata | DONE | PackageWriter supports dynamic Key/Value addition serialized as custom_meta.json |
| 13 | Streaming Support | DONE (reader) | `PackageReader` opens `http(s)://` URLs via WinHTTP Range requests + 64 KiB LRU block cache (`ByteSource` abstraction; file + HTTP). pftest loopback-server round-trip. Remote octree PAYLOAD streaming in the viewer = documented follow-up |
| 14 | Chunked Octree | PLANNED | Multi-chunk octree layout support |
| 15 | Compression | DONE | ZSTD compression added to PackageWriter AddMemory/AddFile APIs |
| 16 | Checksums | DONE | Implemented CRC32 computation per-file, computed by Writer, validated by Reader on Read() |
| 17 | Recovery | PLANNED | Crash recovery, rollback, atomic saves |
| 18 | Encryption | PLANNED | AES reserved flags |
| 19 | Virtual File System | PLANNED | True VFS hierarchy (`/clouds/`, `/images/`, `/plugins/`) |
| 20 | Documentation | DONE | `docs/vxpc.md` rewritten as the authoritative v1 spec (128-byte header, 104-byte entry, compression/CRC/endianness, well-known entries, repack, back-compat) — matches the code, replaces the stale 16-byte-header draft |

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

## Phase 7: Camera Data — DONE
**Goal**: Pack user navigation states into the `.vxpc` container.
- **Implemented**:
  - **Repack infrastructure (shared by phases 8/9)** — `pf::RepackPackage(path,
    upserts, removals)`: copies every surviving entry VERBATIM via
    `PackageReader::ReadRaw` + `PackageWriter::AddRawEntry` (no decompress/
    recompress, so `octree.bin` bytes are byte-identical), applies ZSTD-
    compressed upserts (add-or-replace) and removals, writes `path + ".tmp"`
    then atomically renames over the original. `InheritHeader` preserves the
    uuid/created-time/converter-version identity fields.
  - **Camera data** — bookmarks serialize to `bookmarks.json`, the camera path
    to `campaths.json` (nlohmann::json). On loading a `.vxpc`, the viewer reads
    these (if present) and they are AUTHORITATIVE for that cloud; the AppData
    TSV files remain the per-machine store.
  - **Viewer** — File > "Save Camera Data to Package" (enabled only for a
    loaded `.vxpc`): closes the active cloud to release the OS file handle,
    repacks with the current bookmarks/campath JSON, reloads, and restores the
    camera pose. (The streaming store holds `octree.bin` open, so an in-place
    rewrite of the loaded package requires releasing it first — hence the
    close/repack/reload cycle rather than a live append.)
  - Tests: `pftest testRepack` — on a real converted package, asserts
    `octree.bin` is copied byte-identically, added JSON entries round-trip
    (CRC-checked), the octree still loads, then a second repack verifies
    upsert-replace (net-zero count) and removal (−1). Live: a real Tikal-13
    `.vxpc` loads through the new hook and still exports video.
- **Note**: single camera path per cloud (`campaths.json` stores the `keys`
  array directly), matching the viewer's one-`CamPath`-per-cloud model.

## Phase 8: Measurements — DONE
**Goal**: Save engineering measurement artifacts inside the package.
- **Implemented**:
  - `measurements.json` schema `{version, measurements:[{type:"polyline",
    points:[[x,y,z],...]}]}`. The viewer's `measurePts` is a single scene-
    global world-space polyline, so one polyline object is written; the schema
    is an array so area/volume types can be added later without a break.
  - Written via the Phase 7 `RepackPackage` — no new writer plumbing. The
    File > "Save Project Data to Package" action (renamed from "Save Camera
    Data") now packs bookmarks + campath + measurements in a single repack.
  - Loaded on open, but only when it is the first/only cloud (measurePts is
    scene-global — a second cloud must not clobber an active measurement).
  - Tests: `pftest testRepack` upserts + round-trips a `measurements.json`
    alongside the camera JSON, proving multi-entry repack and octree-verbatim
    coexistence. Full build + CTest green.
- **Deferred**: area/volume measurement types (the viewer only has the
  distance polyline today).

## Phase 9: Annotations — DONE
**Goal**: Embed collaboration markers directly in the package.
- **Implemented**:
  - Package `annotations.json` is per-cloud: `{version, annotations:[{p:[x,y,z],
    label, color:[r,g,b]}]}` — distinct from the multi-cloud AppData
    `annotations.json` (`{clouds:[{dir, items}]}`), which stays the per-machine
    store. Labels are sanitized (no tab/newline, ≤96 chars) both ways.
  - Loaded on open and authoritative for that cloud; written via the shared
    `RepackPackage` as part of File > "Save Project Data to Package" (now
    bookmarks + campath + measurements + annotations in one repack).
  - Tests: `pftest testRepack` upserts + round-trips an `annotations.json`
    whose label contains escaped quotes (JSON-safety via nlohmann). Full build
    + CTest green.
- **Deferred**: image/audio attachments (`AddFile("plugins/…" or "media/…")`
  + ImGui/Unity texture streaming) — the Phase 11 plugin namespace + `AddFile`
  already provide the embed primitive; wire the UI when the feature is needed.

## Phase 11: Plugin Data — DONE
**Goal**: Ensure extensibility for 3rd party tools without breaking the core parser.
- **Implemented**:
  - `PackageWriter::AddPluginData(relPath, data, size, comp)` — prepends the
    reserved `plugins/` namespace (idempotent if the caller already did), so
    the "core ignores plugins/" contract can't be sidestepped.
  - `PackageReader::ListEntries(prefix="")` enumerates directory filenames in
    on-disk order; `ListPlugins()` = `ListEntries("plugins/")`.
  - Core-ignore is inherent: `OctreeStore` reads only a fixed name set
    (meta.bin/hierarchy.bin/octree.bin/…), so `plugins/*` is untouched — a
    package carrying plugin blobs still loads. Asserted by pftest.
  - Robustness: `BeginFile` now rejects empty / ≥64-char names (the filename
    field is 64 bytes) instead of silently truncating and risking an alias.
  - Tests: `pftest testPluginData` — write core + two plugin blobs (both
    compression modes), reopen, assert listing/filtering, CRC-checked payload
    round-trip, over-long-name rejection, and the core-ignore contract.
- **Deferred follow-up**: the Unity/native `PF_Package_WritePluginData` C-API
  binding — `pfunity` doesn't currently link `PackageWriter`; wire it when a
  plugin actually needs write access. The pfcore API above is the foundation.

## Phase 13: Streaming Support — DONE (reader level)
**Goal**: Cloud-native access via HTTP Range Requests.
- **Implemented**:
  - `ByteSource` abstraction behind `PackageReader` (`openByteSource(path)`
    factory): `FileByteSource` for local paths, `HttpByteSource` for
    `http(s)://` URLs. `PackageReader::Open` routes by scheme; `Read`/`ReadRaw`/
    `ListEntries`/`OpenStream` all work over either source unchanged.
  - `HttpByteSource` uses **WinHTTP** (OS-native, no new external/vcpkg dep,
    `#pragma comment(lib,"winhttp.lib")` — same pattern as VideoExporter's
    Media Foundation). It discovers total size from a 1-byte ranged GET's
    `Content-Range`, then serves reads from a **64 KiB block LRU cache** (≤256
    blocks / 16 MiB); a miss fetches the covering span in ONE ranged GET and
    serves directly from it (correct even for spans larger than the cache).
  - `OpenStream` reimplemented as a source-agnostic `MemoryStream` (the old
    file-only stream had no callers and mis-handled compressed entries).
  - Tests: `pftest testHttpStreaming` — a minimal WinSock loopback HTTP/1.1
    server serves the real converted `.vxpc` with `Range` support; open over
    `http://127.0.0.1:<port>/…`, assert directory count matches, `meta.bin`
    decompress+CRC over a ranged read, and `octree.bin` raw bytes byte-match
    the local file. Windows-only (matches the WinHTTP client).
- **Behavioural note**: `PackageReader` now holds its source open for its
  lifetime (was per-call `fopen`). Callers that rename/repack a file must close
  any reader on it first — `RepackPackage` and the viewer save path already do.
- **Deferred follow-up**: streaming the octree PAYLOAD over HTTP in the viewer
  (loading a remote cloud end-to-end). The reader supports URL range reads
  today; the remaining work is routing `OctreeStore`'s streaming worker
  (its own `ifstream` on `octree.bin`) through a `ByteSource` — intentionally
  left out of this phase to avoid touching the crash-sensitive streaming path.

## Phase 20: Documentation — DONE
**Goal**: Formal specification.
- **Implemented**: `docs/vxpc.md` rewritten as the authoritative v1 spec —
  file layout, the 128-byte header and 104-byte directory entry (field-by-field
  offsets), compression enum (None/ZSTD + incompressible fallback, octree.bin
  stored raw), CRC-32 over stored bytes, little-endian + fixed-size guarantees,
  the well-known entry table (core + project sidecars + `plugins/`),
  read/write/repack APIs, and folder back-compat. Corrects the earlier draft
  (which described a 16-byte header with variable-length names).

---

# Long-Term / High Complexity Phases (Keep at Last)

> **Note**: The following phases require massive architectural shifts, multi-month development, or significant refactoring. They are deferred to the end of the roadmap and should not be implemented at this moment.

## Phase 10: Multi Cloud — DONE (packaging approach)
**Goal**: Store entire project scenes inside a single `.vxpc`.
- **Implemented (packaging, not re-indexing)**: rather than rewrite the indexer
  to merge N roots into one octree, several already-converted single-cloud
  `.vxpc` files are *packed* into one multi-cloud package:
  - Each source's entries are copied VERBATIM under a `clouds/<i>/` namespace
    (via `ReadRaw`/`AddRawEntry` — no re-(de)compression), plus a top-level
    `scene.json` manifest `{version, clouds:[{prefix, name}]}`.
  - `pf::combineClouds(outPath, sources)` in pfcore; `pfconvert --combine --out
    <scene.vxpc> <a.vxpc> <b.vxpc> …` CLI. Nested multi-cloud sources rejected.
  - `OctreeStore::load(dir, prefix)` reads a namespaced sub-cloud
    (`clouds/<i>/meta.bin` etc., octree offset from `clouds/<i>/octree.bin`);
    empty prefix = the single-cloud layout (backward compatible).
  - The viewer detects `scene.json` on open and adds each member as its own
    `SceneCloud` (reusing the #6 multi-cloud SCENE machinery: per-cloud renderer,
    shared scene origin, frame-all/visibility). `SceneCloud` gained `name`
    (manifest label) + `pkgPrefix`.
  - Tests: `pftest testMultiCloudPackage` — combine two clouds, reopen, assert
    `scene.json` + namespaced entries, then load each via `load(dir, prefix)`
    and run full `verifyStructure` (DFS + boxed query + pick) through the
    namespaced octree offset. Live: two real Tikal `.vxpc` combined (584 MB)
    and both loaded + rendered in the viewer.
- **Still deferred**: (a) re-index-time merging into a single unified octree
  (the doc's original framing — merge bounds/coords into one root); (b)
  per-cloud sidecar (bookmarks/measure/annotation) save into a multi-cloud
  package — the save action is guarded off for package members for now.

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
