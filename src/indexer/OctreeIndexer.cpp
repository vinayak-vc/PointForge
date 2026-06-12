#include "indexer/OctreeIndexer.h"
#include "indexer/Chunker.h"
#include "indexer/MetadataWriter.h"
#include "common/OctreeFormat.h"
#include "common/PointFormat.h"
#include "common/Log.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <algorithm>

#ifdef PF_WITH_ZSTD
#include <zstd.h>
#endif

namespace fs = std::filesystem;

namespace pf {

namespace {

// In-memory node used while building a chunk subtree before it is serialized.
struct LocalNode {
    std::vector<PackedPoint>       retained;
    std::unique_ptr<LocalNode>     child[8];
    int                            level = 0;
};

// Spatial-hash key for the subsample grid (21 bits per axis).
inline uint64_t cellKey(int cx, int cy, int cz) {
    auto m = [](int v) -> uint64_t { return (uint64_t)(v & 0x1FFFFF); };
    return (m(cx) << 42) | (m(cy) << 21) | m(cz);
}

// Grid-subsample a set of points within a cube. Points that occupy a fresh grid
// cell are retained in `retained`; the rest are partitioned into the 8 child
// octants. Coordinates are computed in world space via the quantization.
void subsample(std::vector<PackedPoint>&& pts,
               const Quantization& q,
               const double cubeMin[3], double cubeSize, double spacing,
               std::vector<PackedPoint>& retained,
               std::vector<PackedPoint> childPts[8]) {
    std::unordered_set<uint64_t> occupied;
    occupied.reserve(pts.size() / 2 + 16);
    const double cx = cubeMin[0] + cubeSize * 0.5;
    const double cy = cubeMin[1] + cubeSize * 0.5;
    const double cz = cubeMin[2] + cubeSize * 0.5;

    for (const PackedPoint& pp : pts) {
        Vec3d w = q.unpack(pp);
        int gx = (int)std::floor((w.x - cubeMin[0]) / spacing);
        int gy = (int)std::floor((w.y - cubeMin[1]) / spacing);
        int gz = (int)std::floor((w.z - cubeMin[2]) / spacing);
        if (occupied.insert(cellKey(gx, gy, gz)).second) {
            retained.push_back(pp);
        } else {
            int o = 0;
            if (w.x >= cx) o |= 4;
            if (w.y >= cy) o |= 2;
            if (w.z >= cz) o |= 1;
            childPts[o].push_back(pp);
        }
    }
    pts.clear();
    pts.shrink_to_fit();
}

std::unique_ptr<LocalNode> buildSubtree(std::vector<PackedPoint>&& pts,
                                        const Quantization& q,
                                        const double cubeMin[3], double cubeSize,
                                        int level, double spacing,
                                        const IndexOptions& opts, double minSpacing) {
    auto node = std::make_unique<LocalNode>();
    node->level = level;

    if (pts.size() <= opts.targetLeafSize || level >= opts.maxDepth || spacing <= minSpacing) {
        node->retained = std::move(pts);   // leaf stores everything
        return node;
    }

    std::vector<PackedPoint> childPts[8];
    subsample(std::move(pts), q, cubeMin, cubeSize, spacing, node->retained, childPts);

    for (int o = 0; o < 8; ++o) {
        if (childPts[o].empty()) continue;
        double cMin[3]; double cSize;
        childCube(cubeMin, cubeSize, o, cMin, cSize);
        node->child[o] = buildSubtree(std::move(childPts[o]), q, cMin, cSize,
                                      level + 1, spacing * 0.5, opts, minSpacing);
    }
    return node;
}

// Serialize a chunk subtree depth-first (children before parent) into octree.bin,
// appending NodeRecords to `hierarchy`. Returns the global index of `node`.
uint32_t serialize(const LocalNode* node, FILE* payload, uint64_t& offset,
                   std::vector<NodeRecord>& hierarchy, bool compress) {
    NodeRecord rec{};
    rec.level = (uint8_t)node->level;
    rec.childMask = 0;
    for (int o = 0; o < 8; ++o) {
        if (node->child[o]) {
            rec.children[o] = serialize(node->child[o].get(), payload, offset, hierarchy, compress);
            rec.childMask |= (uint8_t)(1u << o);
        } else {
            rec.children[o] = kNoChild;
        }
    }
    const uint32_t count = (uint32_t)node->retained.size();
    const size_t rawBytes = count * sizeof(PackedPoint);
    rec.pointCount = count;
    rec.byteOffset = offset;

    if (count > 0) {
#ifdef PF_WITH_ZSTD
        if (compress) {
            size_t bound = ZSTD_compressBound(rawBytes);
            std::vector<uint8_t> cbuf(bound);
            size_t cSize = ZSTD_compress(cbuf.data(), bound,
                                         node->retained.data(), rawBytes, 3);
            if (!ZSTD_isError(cSize) && cSize < rawBytes) {
                std::fwrite(cbuf.data(), 1, cSize, payload);
                rec.byteSize = (uint32_t)cSize;
                offset += cSize;
            } else {
                // Compression didn't help — write raw
                std::fwrite(node->retained.data(), sizeof(PackedPoint), count, payload);
                rec.byteSize = (uint32_t)rawBytes;
                offset += rawBytes;
            }
        } else
#endif
        {
            (void)compress; // suppress warning when PF_WITH_ZSTD is not defined
            std::fwrite(node->retained.data(), sizeof(PackedPoint), count, payload);
            rec.byteSize = (uint32_t)rawBytes;
            offset += rawBytes;
        }
    } else {
        rec.byteSize = 0;
    }

    uint32_t idx = (uint32_t)hierarchy.size();
    hierarchy.push_back(rec);
    return idx;
}

// Coarse build over the level-L sample. Above stopLevel it creates ordinary
// internal nodes; at stopLevel it links children to pre-built chunk roots.
struct CoarseCtx {
    const Quantization* q;
    FILE* payload;
    uint64_t* offset;
    std::vector<NodeRecord>* hierarchy;
    const std::unordered_map<uint32_t, uint32_t>* chunkRoots; // chunkIndex -> node index
    double rootMin[3];
    double cellSizeL;   // size of a level-L cell
    int    dim;         // 2^L
    int    stopLevel;   // = L - 1
    double minSpacing;
    const IndexOptions* opts;
};

uint32_t buildCoarse(std::vector<PackedPoint>&& pts, const CoarseCtx& ctx,
                     const double cubeMin[3], double cubeSize,
                     int level, double spacing) {
    NodeRecord rec{};
    rec.level = (uint8_t)level;
    for (int o = 0; o < 8; ++o) rec.children[o] = kNoChild;

    std::vector<PackedPoint> retained;
    std::vector<PackedPoint> childPts[8];
    subsample(std::move(pts), *ctx.q, cubeMin, cubeSize, spacing, retained, childPts);

    if (level == ctx.stopLevel) {
        // children are existing chunk roots; leftover child points are discarded
        // (they already live inside the chunk subtrees).
        for (int o = 0; o < 8; ++o) {
            double cMin[3]; double cSize;
            childCube(cubeMin, cubeSize, o, cMin, cSize);
            // map child cube to its level-L grid cell
            int ccx = (int)std::lround((cMin[0] - ctx.rootMin[0]) / ctx.cellSizeL);
            int ccy = (int)std::lround((cMin[1] - ctx.rootMin[1]) / ctx.cellSizeL);
            int ccz = (int)std::lround((cMin[2] - ctx.rootMin[2]) / ctx.cellSizeL);
            ccx = std::min(std::max(ccx, 0), ctx.dim - 1);
            ccy = std::min(std::max(ccy, 0), ctx.dim - 1);
            ccz = std::min(std::max(ccz, 0), ctx.dim - 1);
            uint32_t chunkIndex = ((uint32_t)ccx * ctx.dim + ccy) * ctx.dim + ccz;
            auto it = ctx.chunkRoots->find(chunkIndex);
            if (it != ctx.chunkRoots->end()) {
                rec.children[o] = it->second;
                rec.childMask |= (uint8_t)(1u << o);
            }
        }
    } else {
        for (int o = 0; o < 8; ++o) {
            if (childPts[o].empty()) continue;
            double cMin[3]; double cSize;
            childCube(cubeMin, cubeSize, o, cMin, cSize);
            rec.children[o] = buildCoarse(std::move(childPts[o]), ctx, cMin, cSize,
                                          level + 1, spacing * 0.5);
            rec.childMask |= (uint8_t)(1u << o);
        }
    }

    const uint32_t count = (uint32_t)retained.size();
    const size_t rawBytes = count * sizeof(PackedPoint);
    rec.pointCount = count;
    rec.byteOffset = *ctx.offset;

    if (count > 0) {
#ifdef PF_WITH_ZSTD
        if (ctx.opts->compress) {
            size_t bound = ZSTD_compressBound(rawBytes);
            std::vector<uint8_t> cbuf(bound);
            size_t cSize = ZSTD_compress(cbuf.data(), bound,
                                         retained.data(), rawBytes, 3);
            if (!ZSTD_isError(cSize) && cSize < rawBytes) {
                std::fwrite(cbuf.data(), 1, cSize, ctx.payload);
                rec.byteSize = (uint32_t)cSize;
                *ctx.offset += cSize;
            } else {
                std::fwrite(retained.data(), sizeof(PackedPoint), count, ctx.payload);
                rec.byteSize = (uint32_t)rawBytes;
                *ctx.offset += rawBytes;
            }
        } else
#endif
        {
            std::fwrite(retained.data(), sizeof(PackedPoint), count, ctx.payload);
            rec.byteSize = (uint32_t)rawBytes;
            *ctx.offset += rawBytes;
        }
    } else {
        rec.byteSize = 0;
    }

    uint32_t idx = (uint32_t)ctx.hierarchy->size();
    ctx.hierarchy->push_back(rec);
    return idx;
}

std::vector<PackedPoint> loadChunk(const std::string& path) {
    std::vector<PackedPoint> pts;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { logError("loadChunk: cannot open " + path); return pts; }
    std::fseek(f, 0, SEEK_END);
    long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    pts.resize((size_t)bytes / sizeof(PackedPoint));
    if (!pts.empty()) std::fread(pts.data(), sizeof(PackedPoint), pts.size(), f);
    std::fclose(f);
    return pts;
}

} // namespace

bool buildOctree(const std::string& inputPath,
                 const std::string& outDir,
                 const IndexOptions& opts) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    const std::string chunkDir = outDir + "/chunks";
    fs::create_directories(chunkDir, ec);

    // ---- phase A+B: chunking ---------------------------------------------
    ChunkSet cs;
    if (!runChunker(inputPath, chunkDir, opts.gridDepth, cs, opts.flushBudget)) {
        logError("buildOctree: chunking failed");
        return false;
    }

    const double rootSpacing = (opts.rootSpacing > 0.0)
                                   ? opts.rootSpacing
                                   : cs.cubeSize / 128.0;
    const double minSpacing  = cs.quant.scale.x; // can't resolve finer than this
    const int    L           = cs.gridDepth;
    const double cellSizeL   = cs.cubeSize / cs.gridDim;
    const double spacingAtL  = rootSpacing / std::pow(2.0, L);

    // ---- phase C: per-chunk subtrees -------------------------------------
    FILE* payload = std::fopen((outDir + "/octree.bin").c_str(), "wb");
    if (!payload) { logError("buildOctree: cannot open octree.bin"); return false; }

    std::vector<NodeRecord> hierarchy;
    uint64_t offset = 0;
    std::unordered_map<uint32_t, uint32_t> chunkRoots;
    std::vector<PackedPoint> coarse; // union of chunk-root samples

    size_t done = 0;
    for (const auto& ch : cs.chunks) {
        std::vector<PackedPoint> pts = loadChunk(ch.path);
        if (pts.empty()) continue;

        double cMin[3] = {
            cs.cubeMin.x + ch.gx * cellSizeL,
            cs.cubeMin.y + ch.gy * cellSizeL,
            cs.cubeMin.z + ch.gz * cellSizeL
        };
        auto subtree = buildSubtree(std::move(pts), cs.quant, cMin, cellSizeL,
                                    L, spacingAtL, opts, minSpacing);
        // collect the chunk-root sample for the coarse build
        coarse.insert(coarse.end(), subtree->retained.begin(), subtree->retained.end());

        uint32_t rootIdx = serialize(subtree.get(), payload, offset, hierarchy, opts.compress);
        chunkRoots[ch.index] = rootIdx;

        if ((++done % 64) == 0)
            logInfo("buildOctree: indexed " + std::to_string(done) + "/" +
                    std::to_string(cs.chunks.size()) + " chunks");
    }

    // ---- phase C2: coarse tree -------------------------------------------
    uint32_t rootNodeIndex;
    if (L == 0) {
        // no coarse levels: the single chunk root is the tree root
        rootNodeIndex = chunkRoots.empty() ? 0 : chunkRoots.begin()->second;
    } else {
        CoarseCtx ctx;
        ctx.q = &cs.quant;
        ctx.payload = payload;
        ctx.offset = &offset;
        ctx.hierarchy = &hierarchy;
        ctx.chunkRoots = &chunkRoots;
        ctx.rootMin[0] = cs.cubeMin.x; ctx.rootMin[1] = cs.cubeMin.y; ctx.rootMin[2] = cs.cubeMin.z;
        ctx.cellSizeL = cellSizeL;
        ctx.dim = cs.gridDim;
        ctx.stopLevel = L - 1;
        ctx.minSpacing = minSpacing;
        ctx.opts = &opts;

        double rMin[3] = { cs.cubeMin.x, cs.cubeMin.y, cs.cubeMin.z };
        rootNodeIndex = buildCoarse(std::move(coarse), ctx, rMin, cs.cubeSize, 0, rootSpacing);
    }

    std::fclose(payload);

    // ---- metadata ---------------------------------------------------------
    FileMetadata meta{};
    std::memcpy(meta.magic, "PFO1", 4);
    meta.version = 2;
    meta.pointCount = cs.pointCount;
    meta.bbMin[0] = cs.bounds.min.x; meta.bbMin[1] = cs.bounds.min.y; meta.bbMin[2] = cs.bounds.min.z;
    meta.bbMax[0] = cs.bounds.max.x; meta.bbMax[1] = cs.bounds.max.y; meta.bbMax[2] = cs.bounds.max.z;
    meta.cubeMin[0] = cs.cubeMin.x; meta.cubeMin[1] = cs.cubeMin.y; meta.cubeMin[2] = cs.cubeMin.z;
    meta.cubeSize = cs.cubeSize;
    meta.scale[0] = cs.quant.scale.x; meta.scale[1] = cs.quant.scale.y; meta.scale[2] = cs.quant.scale.z;
    meta.offset[0] = cs.quant.offset.x; meta.offset[1] = cs.quant.offset.y; meta.offset[2] = cs.quant.offset.z;
    meta.rootSpacing = rootSpacing;
    meta.bytesPerPoint = kBytesPerPoint;
    meta.hasColor = cs.hasColor ? 1u : 0u;
    meta.nodeCount = (uint32_t)hierarchy.size();
    meta.rootNodeIndex = rootNodeIndex;
    meta.hasClassification = cs.hasClassification ? 1u : 0u;
    meta.compressionType = opts.compress ? 1u : 0u;

    bool ok = writeMetaBin(outDir, meta) &&
              writeMetadataJson(outDir, meta) &&
              writeHierarchy(outDir, hierarchy);

    // ---- cleanup ----------------------------------------------------------
    if (!opts.keepChunks) fs::remove_all(chunkDir, ec);

    logInfo("buildOctree: wrote " + std::to_string(hierarchy.size()) + " nodes, " +
            std::to_string(cs.pointCount) + " points, root index " +
            std::to_string(rootNodeIndex));
    return ok;
}

} // namespace pf
