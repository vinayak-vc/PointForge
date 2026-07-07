# VXPC - PointForge Package Format

## Overview
The `.vxpc` (ViitorXPC) format is a custom high-performance package format designed for PointForge. It is used to store point cloud octree data in a single, streamable file. It provides extremely fast loading, zero extraction overhead, and memory-map friendly direct random access.

It functions similarly to Unreal `.pak` files or Unity AssetBundles, avoiding the overhead of thousands of loose files.

## Architecture & Layout

The `.vxpc` file consists of three main sections:
1. **Header**: Magic bytes and versioning information.
2. **Payload**: The concatenated contents of all stored files (e.g., `meta.bin`, `hierarchy.bin`, `octree.bin`, `metadata.json`).
3. **Directory Table**: A lookup table at the end of the file that maps file names to their byte offset and size within the payload.

### Header (16 bytes)
The header is located at the very beginning of the file.

| Offset | Type     | Description |
|--------|----------|-------------|
| 0      | `char[4]` | Magic bytes: `"VXPC"` |
| 4      | `uint32`  | Version number (currently `1`) |
| 8      | `uint64`  | Directory offset (absolute offset to the start of the Directory Table) |

### Payload Data
Immediately following the header, files are appended sequentially. 
Data is uncompressed at the package level (though individual files like `octree.bin` may use internal compression like ZSTD). There is no padding between files by default.

### Directory Table
The directory table starts at the `Directory offset` specified in the header.

**Table Structure:**
| Offset | Type     | Description |
|--------|----------|-------------|
| 0      | `uint32` | Number of entries (`N`) |
| 4...   | `Entry`  | `N` consecutive directory entries |

**Entry Structure:**
| Type      | Description |
|-----------|-------------|
| `uint16`  | Length of the filename string (`L`) |
| `char[L]` | Filename (e.g., `"octree.bin"`) |
| `uint64`  | Absolute byte offset of the file data from the start of the package |
| `uint64`  | Size of the file data in bytes |

## Usage in PointForge

- `PackageWriter`: Used during conversion (`pfconvert`) to append streams sequentially and build the directory table.
- `PackageReader`: Used at runtime by the `OctreeStore` to parse the header and directory. It keeps the directory in memory and provides the absolute file offset for reading `octree.bin` nodes.

## Benefits
- **Streamable**: Since the directory table uses absolute offsets and is at the end of the file, it can be built in a single pass without knowing file sizes in advance.
- **Random Access**: Because the lookup table provides absolute offsets, you can `seek()` directly to the payload.
- **Backward Compatibility**: PointForge natively supports both legacy folder layouts (with loose files) and `.vxpc` packaged files.
