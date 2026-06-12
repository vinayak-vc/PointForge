#pragma once
#include <string>
#include <cstdint>

namespace pf {

struct IndexOptions {
    int      gridDepth     = 4;      // L: chunk grid is (2^L)^3
    double   rootSpacing   = 0.0;    // 0 => auto (cubeSize / 128)
    uint32_t targetLeafSize = 50000; // node becomes a leaf at/below this many points
    int      maxDepth      = 24;     // hard cap on octree depth
    uint64_t flushBudget   = 16u * 1024u * 1024u; // chunker memory budget (points)
    bool     keepChunks    = false;  // keep intermediate chunk files for debugging
    bool     compress      = false;  // zstd per-node compression of octree.bin payloads
};

// Full importer pipeline: chunk -> build chunk subtrees -> build coarse tree ->
// write meta.bin / metadata.json / hierarchy.bin / octree.bin into `outDir`.
// Returns false on error. Out-of-core: peak memory is bounded by the largest
// single chunk plus the coarse (level-L) sample, never by the whole input.
bool buildOctree(const std::string& inputPath,
                 const std::string& outDir,
                 const IndexOptions& opts);

} // namespace pf
