#pragma once
#include <cstdint>

namespace pf {

// ---------------------------------------------------------------------------
// On-disk octree format (v1), shared by the indexer (writer) and viewer (reader).
// See docs/ARCHITECTURE.md for the rationale.
//
// A converted cloud is a directory containing:
//   meta.bin        FileMetadata (this header) — fixed binary, easy to load
//   metadata.json   same info, human-readable (written for inspection only)
//   hierarchy.bin   nodeCount * NodeRecord, depth-first, root at index 0
//   octree.bin      concatenated per-node PackedPoint payloads
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// Phase 6: Structured Project Metadata
struct ProjectMetadata {
    char     projectName[128];
    char     author[64];
    char     company[64];
    char     description[256];
    char     tags[256];
    char     units[32];           // e.g., "Meters"
    char     coordinateSystem[64];
    char     epsg[32];            // e.g., "EPSG:32633"
    char     sourceFile[256];
    
    uint64_t creationDate;
    uint64_t modifiedDate;
    uint32_t converterVersion;
    uint32_t buildVersion;
    uint8_t  uuid[16];
    
    uint32_t reserved[32]; // Padding for future fields
};

struct FileMetadata {
    char     magic[4];        // "PFO1"
    uint32_t version;         // = 2
    uint64_t pointCount;      // total points across all nodes
    double   bbMin[3];        // true AABB of the data (world coords)
    double   bbMax[3];
    double   cubeMin[3];      // octree root cube origin (world coords)
    double   cubeSize;        // octree root cube edge length
    double   scale[3];        // quantization scale (PackedPoint -> world)
    double   offset[3];       // quantization offset
    double   rootSpacing;     // sample spacing at the root node
    uint32_t bytesPerPoint;   // == sizeof(PackedPoint)
    uint32_t hasColor;        // 1 if colour is meaningful
    uint32_t nodeCount;       // number of NodeRecord entries in hierarchy.bin
    uint32_t rootNodeIndex;   // index of the root node within hierarchy.bin
    uint32_t hasClassification; // 1 if classification codes are meaningful (v2+)
    uint32_t compressionType;   // 0 = none, 1 = zstd per-node (v2+)
};

// Child indices are stored explicitly (not as a contiguous firstChild + rank),
// because the out-of-core builder writes chunk subtrees and coarse nodes in
// separate phases, so a node's children are not necessarily contiguous on disk.
struct NodeRecord {
    uint8_t  level;           // 0 = root
    uint8_t  childMask;       // bit o set => children[o] != kNoChild (redundant, handy)
    uint16_t reserved;
    uint32_t pointCount;      // points stored in THIS node
    uint64_t byteOffset;      // payload offset within octree.bin
    uint32_t byteSize;        // payload size in bytes (pointCount * bytesPerPoint)
    uint32_t children[8];     // global node index per octant, or kNoChild
};

#pragma pack(pop)

constexpr uint32_t kNoChild = 0xFFFFFFFFu;

static_assert(sizeof(NodeRecord) == 52, "NodeRecord layout changed unexpectedly");

// Subdivide a cube into the cube of octant o (numbering: (x<<2)|(y<<1)|z).
inline void childCube(const double parentMin[3], double parentSize,
                      int o, double childMin[3], double& childSize) {
    childSize = parentSize * 0.5;
    childMin[0] = parentMin[0] + ((o & 4) ? childSize : 0.0);
    childMin[1] = parentMin[1] + ((o & 2) ? childSize : 0.0);
    childMin[2] = parentMin[2] + ((o & 1) ? childSize : 0.0);
}

// Octant numbering helper: bit layout (x<<2)|(y<<1)|z, 0 = low half.
inline int octantOf(double px, double py, double pz,
                    double cx, double cy, double cz) {
    int o = 0;
    if (px >= cx) o |= 4;
    if (py >= cy) o |= 2;
    if (pz >= cz) o |= 1;
    return o;
}

} // namespace pf
