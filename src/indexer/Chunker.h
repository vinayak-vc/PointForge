#pragma once
#include "common/AABB.h"
#include "common/PointFormat.h"
#include <cstdint>
#include <string>
#include <vector>

namespace pf {

// Result of the chunking phase: the octree cube, quantization, and the set of
// occupied coarse cells, each spilled to its own file on disk.
struct ChunkSet {
    AABB         bounds;          // true data AABB
    Vec3d        cubeMin;         // octree root cube origin
    double       cubeSize = 0;    // octree root cube edge length
    Quantization quant;           // scale/offset used for PackedPoint
    uint64_t     pointCount = 0;
    bool         hasColor = false;

    int          gridDepth = 0;   // L; grid is (2^L)^3 cells
    int          gridDim = 1;     // 2^L
    std::string  chunkDir;        // directory holding <index>.bin chunk files

    struct Chunk {
        uint32_t index;           // (gx*gridDim + gy)*gridDim + gz
        uint32_t gx, gy, gz;
        uint64_t count;
        std::string path;
    };
    std::vector<Chunk> chunks;    // only occupied cells
};

// Phase A + B. Streams the source twice if needed:
//   - if the reader exposes header bounds we skip the bounds pass;
//   - otherwise one pass computes the global AABB;
//   - a second pass bins every point (as a PackedPoint) into its coarse cell file.
// Memory is bounded by a configurable flush budget, never by the input size.
//
// `gridDepth` (L) controls chunk granularity: larger L => more, smaller chunks
// (so each chunk fits in RAM for the indexing phase). Returns false on error.
bool runChunker(const std::string& inputPath,
                const std::string& chunkDir,
                int gridDepth,
                ChunkSet& out,
                uint64_t flushPointBudget = 16u * 1024u * 1024u);

} // namespace pf
