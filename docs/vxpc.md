# VXPC — PointForge Package Format (v1)

> **Status:** formal specification of the format as implemented in
> `src/io/PackageFormat.{h,cpp}` (Phase 20). This document is authoritative and
> is kept in sync with the code; where an earlier draft of this file disagreed
> (a 16-byte header with variable-length names), **this version is correct.**

## 1. Overview

`.vxpc` (ViitorXPC) is a single-file container that stores a PointForge octree
and all its sidecar data (metadata, thumbnails, camera bookmarks, camera paths,
measurements, annotations, 3rd-party plugin blobs). It replaces the legacy
loose-folder layout (`meta.bin` + `hierarchy.bin` + `octree.bin` +
`metadata.json` as separate files) with one shareable file, in the spirit of an
Unreal `.pak` or Unity AssetBundle.

Design goals: **zero-extraction random access** (entries are read in place via
absolute offsets), **single-pass streamable writing** (the directory is written
last, so entry sizes need not be known up front), and **backward compatibility**
(the viewer still loads the old folder layout — see §9).

All multi-byte integers are **little-endian**. All structures are laid out with
natural alignment and are asserted to fixed sizes at compile time
(`static_assert` in `PackageFormat.cpp`); no explicit packing pragma is used, so
producers/consumers on other platforms MUST match the sizes below exactly.

## 2. File layout

```
+-------------------------------+  offset 0
| Header (128 bytes)            |
+-------------------------------+  offset 128
| Payload region                |
|   entry 0 stored bytes        |   (concatenated, no padding between entries)
|   entry 1 stored bytes        |
|   ...                         |
+-------------------------------+  offset = header.directoryOffset
| Directory table               |
|   entryCount * 104 bytes      |
+-------------------------------+  EOF
```

The directory table sits at the end. Its start offset and size are recorded in
the header, so a reader fetches the header, seeks to `directoryOffset`, reads
`entryCount` entries, and can then resolve any file by name to an absolute
offset. A network reader can therefore fetch the header + trailing directory
with two small range requests and then read entries on demand (see Phase 13).

## 3. Header (128 bytes)

`struct VXPCHeader` — offsets are little-endian:

| Offset | Size | Type      | Field              | Notes |
|-------:|-----:|-----------|--------------------|-------|
| 0      | 4    | `char[4]` | `magic`            | `"VXPC"` |
| 4      | 4    | `uint32`  | `version`          | currently `1` |
| 8      | 8    | `uint64`  | `directoryOffset`  | absolute offset of the directory table |
| 16     | 8    | `uint64`  | `directorySize`    | `entryCount * sizeof(VXPCDirectoryEntry)` |
| 24     | 4    | `uint32`  | `entryCount`       | number of directory entries |
| 28     | 4    | `uint32`  | `packageFlags`     | reserved (0) |
| 32     | 16   | `uint8[16]` | `uuid`           | package identity (0 if unset) |
| 48     | 8    | `uint64`  | `createdTime`      | reserved (0 if unset) |
| 56     | 8    | `uint64`  | `modifiedTime`     | reserved (0 if unset) |
| 64     | 4    | `uint32`  | `converterVersion` | reserved (0 if unset) |
| 68     | 60   | `uint32[15]` | `reserved`      | zero-filled padding to 128 bytes |

Total: **128 bytes** (`static_assert(sizeof(VXPCHeader) == 128)`).

A reader MUST reject a file whose `magic != "VXPC"` or `version != 1`.

The `uuid`/`createdTime`/`converterVersion` fields are defined but not yet
populated by the current converter (they read as 0). `RepackPackage` preserves
whatever values are present via `PackageWriter::InheritHeader`.

## 4. Directory entry (104 bytes)

`struct VXPCDirectoryEntry`:

| Offset | Size | Type       | Field            | Notes |
|-------:|-----:|------------|------------------|-------|
| 0      | 64   | `char[64]` | `filename`       | NUL-terminated; ≤63 usable chars |
| 64     | 8    | `uint64`   | `offset`         | absolute offset of the stored bytes |
| 72     | 8    | `uint64`   | `compressedSize` | bytes on disk when compressed |
| 80     | 8    | `uint64`   | `originalSize`   | bytes after decompression |
| 88     | 4    | `uint32`   | `compression`    | see §5 |
| 92     | 4    | `uint32`   | `crc32`          | over the STORED bytes (§6) |
| 96     | 4    | `uint32`   | `flags`          | reserved (0) |
| 100    | 4    | `uint32`   | `userFlags`      | reserved for callers (0) |

Total: **104 bytes** (`static_assert(sizeof(VXPCDirectoryEntry) == 104)`).

The **stored size** on disk is `compressedSize` when `compression != None`,
otherwise `originalSize`. Filenames use `/` as a path separator (flat list, not
a tree — see Phase 19). The writer rejects an empty or ≥64-char name rather than
truncating it (a truncation could alias two entries).

## 5. Compression

`compression` enum: `0 = None`, `1 = ZSTD`.

- ZSTD entries are compressed at level 3. If ZSTD errors or the payload is
  incompressible (`compressedSize >= originalSize`), the writer silently falls
  back to `None` for that entry (so `compressedSize == originalSize` and the
  bytes are stored verbatim).
- `octree.bin` is stored **uncompressed** at the package level (`compression =
  None`): its per-node payloads already carry their own optional ZSTD, and
  storing it raw keeps the streaming reader's absolute-offset math trivial.
- A reader built without ZSTD support MUST refuse to decode a `compression == 1`
  entry (it cannot recover the bytes).

## 6. Integrity (CRC-32)

Each entry carries a CRC-32 (`crc32`, standard reflected polynomial
`0xEDB88320`) computed over the **stored** bytes — i.e. the compressed bytes for
a ZSTD entry, the raw bytes otherwise. `PackageReader::Read` recomputes the CRC
over the bytes read from disk and returns empty on mismatch (corruption
detection). `ReadRaw` returns the stored bytes without decompressing or
CRC-checking (used by `RepackPackage` to copy entries verbatim).

## 7. Well-known entries

Core (written by `pfconvert`/`OctreeIndexer`; the loader reads these by name):

| Name              | Compression | Contents |
|-------------------|-------------|----------|
| `meta.bin`        | ZSTD        | `FileMetadata` (magic `PFO1`) |
| `hierarchy.bin`   | ZSTD        | array of `NodeRecord` |
| `octree.bin`      | None        | concatenated per-node `PackedPoint` payloads |
| `metadata.json`   | ZSTD        | human-readable copy of the metadata |
| `project.bin`     | ZSTD        | `ProjectMetadata` (Phase 6) |
| `custom_meta.json`| ZSTD        | dynamic key/value pairs (Phase 12) |
| `thumbnail.jpg` / `thumbnail.raw` | (jpg: None / raw: ZSTD) | preview image (Phase 5) |

Project sidecars (written by the viewer's "Save Project Data to Package"):

| Name                | Contents |
|---------------------|----------|
| `bookmarks.json`    | camera bookmarks (Phase 7) |
| `campaths.json`     | camera-path keyframes (Phase 7) |
| `measurements.json` | measurement polyline(s) (Phase 8) |
| `annotations.json`  | per-cloud annotation pins (Phase 9) |

3rd-party (Phase 11): anything under the reserved **`plugins/`** namespace
(e.g. `plugins/<vendor>/<file>`). The core loader ignores every name outside the
core set above, so plugin blobs never interfere with loading.

## 8. Read / write / repack

- **Write** (`PackageWriter`): `Create` writes a placeholder header;
  `AddMemory`/`AddFile`/`BeginFile`+`Write`+`EndFile` append payloads and
  accumulate directory entries; `Finalize` writes any `custom_meta.json`, the
  directory table, then rewrites the header with the final
  `directoryOffset`/`directorySize`/`entryCount`.
- **Read** (`PackageReader`): `Open` validates the header and loads the
  directory; `Read(name)` returns decompressed + CRC-checked bytes; `ReadRaw`
  returns stored bytes; `Contains`/`GetOffset`/`GetSize`/`GetEntry` query the
  directory; `ListEntries(prefix)`/`ListPlugins()` enumerate names; `OpenStream`
  returns a seekable `PackageStream` bounded to one entry.
- **Repack** (`RepackPackage(path, upserts, removals)`): copies every surviving
  entry verbatim (via `ReadRaw`+`AddRawEntry` — no re-(de)compression, so large
  payloads like `octree.bin` are byte-identical), applies ZSTD upserts
  (add-or-replace) and removals, writes `path + ".tmp"`, then atomically renames
  over the original. The caller must ensure the file is not held open for
  writing (on Windows the rename would fail); the viewer closes the cloud's
  streaming store first.

## 9. Backward compatibility

`OctreeStore::load(dir)` routes by extension: a `.vxpc` path opens a
`PackageReader`; any other path is treated as a legacy folder of loose files.
The octree parsing above the storage layer is identical in both cases — for the
package it seeks to `GetOffset("octree.bin") + node.byteOffset`; for the folder
it seeks to `node.byteOffset` in `octree.bin`. `pfconvert` now emits `.vxpc`
(an `--out` path without the extension becomes `<out>/scan.vxpc`); folder
reading remains supported for existing datasets.

## 10. Versioning & extension rules

- The `version` field is bumped only on an incompatible header/entry-layout
  change. New well-known entry names are **not** a version bump (readers ignore
  unknown names).
- Producers MUST zero `reserved`/`flags`/`userFlags`. Consumers MUST ignore
  non-zero reserved bits they don't understand rather than failing.
- Endianness is little-endian; there is no big-endian variant.

## 11. Deferred (not in v1)

Per `docs/vxpc_feature.md` §"Long-Term / High Complexity": multi-cloud packages
(10), chunked `octree.bin` (14), crash-recovery/atomic journaling (17),
AES encryption (18), and a true hierarchical VFS/trie directory (19). HTTP
range-request streaming (13) is planned but needs an HTTP client dependency.
