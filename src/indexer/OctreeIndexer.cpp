#include "indexer/OctreeIndexer.h"
#include "indexer/Chunker.h"
#include "indexer/MetadataWriter.h"
#include "common/OctreeFormat.h"
#include "common/PointFormat.h"
#include "common/Log.h"
#include "io/PackageFormat.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
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

// Spatial-hash key for the subsample grid (21 bits per axis). Bit 63 is never
// set (3 * 21 = 63 bits), so FlatCellSet can use all-ones as its empty marker.
inline uint64_t cellKey(int cx, int cy, int cz) {
    auto m = [](int v) -> uint64_t { return (uint64_t)(v & 0x1FFFFF); };
    return (m(cx) << 42) | (m(cy) << 21) | m(cz);
}

// Open-addressing hash set for cell keys. std::unordered_set allocates a heap
// node per insert — with Phase C running on N threads those mallocs all
// serialize on the process heap lock and erase the parallel speedup. One flat
// allocation + linear probing keeps the subsample inner loop allocation-free.
class FlatCellSet {
public:
    explicit FlatCellSet(size_t expected) {
        size_t cap = 16;
        while (cap < expected * 2) cap <<= 1;
        slots_.assign(cap, kEmpty);
        mask_ = cap - 1;
        growAt_ = cap - cap / 4; // 0.75 load factor
    }
    // Returns true if the key was newly inserted.
    bool insert(uint64_t key) {
        if (count_ >= growAt_) grow();
        size_t i = mix(key) & mask_;
        while (slots_[i] != kEmpty) {
            if (slots_[i] == key) return false;
            i = (i + 1) & mask_;
        }
        slots_[i] = key;
        ++count_;
        return true;
    }

private:
    static constexpr uint64_t kEmpty = ~0ull; // unreachable: cellKey uses 63 bits
    static size_t mix(uint64_t k) {           // splitmix64 finalizer
        k ^= k >> 33; k *= 0xff51afd7ed558ccdull;
        k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull;
        k ^= k >> 33;
        return (size_t)k;
    }
    void grow() {
        std::vector<uint64_t> old = std::move(slots_);
        size_t cap = (mask_ + 1) << 1;
        slots_.assign(cap, kEmpty);
        mask_ = cap - 1;
        growAt_ = cap - cap / 4;
        for (uint64_t k : old) {
            if (k == kEmpty) continue;
            size_t i = mix(k) & mask_;
            while (slots_[i] != kEmpty) i = (i + 1) & mask_;
            slots_[i] = k;
        }
    }
    std::vector<uint64_t> slots_;
    size_t mask_ = 0, count_ = 0, growAt_ = 0;
};

// Grid-subsample a set of points within a cube. Points that occupy a fresh grid
// cell are retained in `retained`; the rest are partitioned into the 8 child
// octants. Coordinates are computed in world space via the quantization.
void subsample(std::vector<PackedPoint>&& pts,
               const Quantization& q,
               const double cubeMin[3], double cubeSize, double spacing,
               std::vector<PackedPoint>& retained,
               std::vector<PackedPoint> childPts[8]) {
    FlatCellSet occupied(pts.size() / 2 + 16);
    const double cx = cubeMin[0] + cubeSize * 0.5;
    const double cy = cubeMin[1] + cubeSize * 0.5;
    const double cz = cubeMin[2] + cubeSize * 0.5;

    for (const PackedPoint& pp : pts) {
        Vec3d w = q.unpack(pp);
        int gx = (int)std::floor((w.x - cubeMin[0]) / spacing);
        int gy = (int)std::floor((w.y - cubeMin[1]) / spacing);
        int gz = (int)std::floor((w.z - cubeMin[2]) / spacing);
        if (occupied.insert(cellKey(gx, gy, gz))) {
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

// Compress-or-copy one node's points onto the end of an in-memory payload
// blob, filling rec.byteOffset (blob-relative) and rec.byteSize.
void appendPayloadToBlob(const std::vector<PackedPoint>& pts, bool compress,
                         std::vector<uint8_t>& blob, NodeRecord& rec) {
    const uint32_t count = (uint32_t)pts.size();
    const size_t rawBytes = count * sizeof(PackedPoint);
    rec.pointCount = count;
    rec.byteOffset = blob.size();
    if (count == 0) { rec.byteSize = 0; return; }
#ifdef PF_WITH_ZSTD
    if (compress) {
        size_t bound = ZSTD_compressBound(rawBytes);
        std::vector<uint8_t> cbuf(bound);
        size_t cSize = ZSTD_compress(cbuf.data(), bound, pts.data(), rawBytes, 3);
        if (!ZSTD_isError(cSize) && cSize < rawBytes) {
            blob.insert(blob.end(), cbuf.data(), cbuf.data() + cSize);
            rec.byteSize = (uint32_t)cSize;
            return;
        }
        // Compression didn't help — fall through to raw
    }
#endif
    (void)compress; // suppress warning when PF_WITH_ZSTD is not defined
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(pts.data());
    blob.insert(blob.end(), raw, raw + rawBytes);
    rec.byteSize = (uint32_t)rawBytes;
}

// Serialize a chunk subtree depth-first (children before parent) into an
// in-memory blob + chunk-LOCAL records (children indices and byteOffsets are
// relative to this chunk; the coordinator rebases them when splicing into the
// global hierarchy/file). Pure function — safe to run on worker threads.
uint32_t serializeLocal(const LocalNode* node, std::vector<uint8_t>& blob,
                        std::vector<NodeRecord>& records, bool compress) {
    NodeRecord rec{};
    rec.level = (uint8_t)node->level;
    rec.childMask = 0;
    for (int o = 0; o < 8; ++o) {
        if (node->child[o]) {
            rec.children[o] = serializeLocal(node->child[o].get(), blob, records, compress);
            rec.childMask |= (uint8_t)(1u << o);
        } else {
            rec.children[o] = kNoChild;
        }
    }
    appendPayloadToBlob(node->retained, compress, blob, rec);
    uint32_t idx = (uint32_t)records.size();
    records.push_back(rec);
    return idx;
}

// Everything Phase C produces for one chunk, built entirely off the shared
// state so N chunks can build concurrently. The coordinator thread splices
// results into hierarchy/octree.bin IN CHUNK ORDER, which keeps the output
// byte-identical to a sequential build (verified by pftest).
struct ChunkResult {
    uint32_t                 chunkIndex = 0;
    std::vector<NodeRecord>  records;       // chunk-local indices/offsets
    std::vector<uint8_t>     blob;          // payload bytes for this subtree
    std::vector<PackedPoint> coarseSamples; // chunk root's retained set
    uint32_t                 localRootIdx = 0;
    bool                     empty = true;  // chunk file was empty/unreadable
};

// Coarse build over the level-L sample. Above stopLevel it creates ordinary
// internal nodes; at stopLevel it links children to pre-built chunk roots.
struct CoarseCtx {
    const Quantization* q;
    FILE* payload;
    PackageWriter* pkg;
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
                if (ctx.pkg) ctx.pkg->Write(cbuf.data(), cSize);
                else std::fwrite(cbuf.data(), 1, cSize, ctx.payload);
                rec.byteSize = (uint32_t)cSize;
                *ctx.offset += cSize;
            } else {
                if (ctx.pkg) ctx.pkg->Write(retained.data(), count * sizeof(PackedPoint));
                else std::fwrite(retained.data(), sizeof(PackedPoint), count, ctx.payload);
                rec.byteSize = (uint32_t)rawBytes;
                *ctx.offset += rawBytes;
            }
        } else
#endif
        {
            if (ctx.pkg) ctx.pkg->Write(retained.data(), count * sizeof(PackedPoint));
            else std::fwrite(retained.data(), sizeof(PackedPoint), count, ctx.payload);
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
    bool isPackage = (outDir.length() >= 5 && outDir.substr(outDir.length() - 5) == ".vxpc");
    std::string workDir = isPackage ? fs::path(outDir).parent_path().string() : outDir;
    
    fs::create_directories(workDir, ec);
    const std::string chunkDir = workDir + "/chunks";
    fs::create_directories(chunkDir, ec);

    // ---- phase A+B: chunking ---------------------------------------------
    ChunkSet cs;
    if (!runChunker(inputPath, chunkDir, opts.gridDepth, cs, opts.flushBudget, opts.progressCb)) {
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
    std::unique_ptr<PackageWriter> pkg;
    FILE* payload = nullptr;
    if (isPackage) {
        pkg = std::make_unique<PackageWriter>();
        if (!pkg->Create(outDir)) { logError("buildOctree: cannot create package " + outDir); return false; }
        pkg->BeginFile("octree.bin");
    } else {
        payload = std::fopen((outDir + "/octree.bin").c_str(), "wb");
        if (!payload) { logError("buildOctree: cannot open octree.bin"); return false; }
    }

    std::vector<NodeRecord> hierarchy;
    uint64_t offset = 0;
    std::unordered_map<uint32_t, uint32_t> chunkRoots;
    std::vector<PackedPoint> coarse; // union of chunk-root samples

    // Phase C runs on a worker pool: chunks are spatially independent, so each
    // worker builds a full ChunkResult (subtree + serialized blob) with zero
    // shared state. This single coordinator thread is the ONLY writer to
    // hierarchy/octree.bin/chunkRoots/coarse — it drains results strictly in
    // chunk order, so the output is byte-identical to a sequential build.
    const size_t total = cs.chunks.size();
    unsigned hw = std::thread::hardware_concurrency();
    size_t nThreads = opts.threads > 0 ? (size_t)opts.threads
                                       : (size_t)std::max(1u, hw ? hw : 4u);
    nThreads = std::min({nThreads, total ? total : (size_t)1, (size_t)64});
    // Bound RAM: at most this many built-but-unspliced results alive at once
    // (in-order draining means a slow early chunk can park later results here).
    const size_t inFlightCap = nThreads + 2;

    std::mutex mx;
    std::condition_variable cvProduce, cvConsume;
    std::map<size_t, ChunkResult> readyResults; // seq index -> result
    size_t nextToBuild = 0;
    size_t nextToConsume = 0;
    bool   aborted = false;   // user cancel OR failure — stop everything
    bool   failed  = false;   // an exception was caught (distinct log/report)
    std::string failReason;

    // Stop all workers and wake everyone. Callable from any thread.
    auto signalAbort = [&](const char* why) {
        std::lock_guard<std::mutex> lk(mx);
        aborted = true;
        if (why) { failed = true; if (failReason.empty()) failReason = why; }
        cvProduce.notify_all();
        cvConsume.notify_all();
    };

    auto buildOne = [&](size_t seq) -> ChunkResult {
        const auto& ch = cs.chunks[seq];
        ChunkResult res;
        res.chunkIndex = ch.index;
        std::vector<PackedPoint> pts = loadChunk(ch.path);
        if (pts.empty()) return res; // stays empty
        double cMin[3] = {
            cs.cubeMin.x + ch.gx * cellSizeL,
            cs.cubeMin.y + ch.gy * cellSizeL,
            cs.cubeMin.z + ch.gz * cellSizeL
        };
        auto subtree = buildSubtree(std::move(pts), cs.quant, cMin, cellSizeL,
                                    L, spacingAtL, opts, minSpacing);
        res.coarseSamples = subtree->retained; // copy: subtree serializes below
        res.localRootIdx = serializeLocal(subtree.get(), res.blob, res.records,
                                          opts.compress);
        res.empty = false;
        return res;
    };

    // Exceptions must not escape the thread callable (std::terminate would
    // take down the whole viewer — conversion runs in-process via JobQueue).
    // A chunk this size can genuinely throw bad_alloc; fail the JOB instead.
    auto workerFn = [&]() {
        try {
            for (;;) {
                size_t seq;
                {
                    std::unique_lock<std::mutex> lk(mx);
                    cvProduce.wait(lk, [&] {
                        return aborted || nextToBuild >= total ||
                               nextToBuild - nextToConsume < inFlightCap;
                    });
                    if (aborted || nextToBuild >= total) return;
                    seq = nextToBuild++;
                }
                if (opts.cancel && opts.cancel->load()) {
                    signalAbort(nullptr);
                    return;
                }
                ChunkResult res = buildOne(seq);
                {
                    std::lock_guard<std::mutex> lk(mx);
                    readyResults.emplace(seq, std::move(res));
                    cvConsume.notify_all();
                }
            }
        } catch (const std::exception& e) {
            signalAbort(e.what());
        } catch (...) {
            signalAbort("unknown exception in chunk worker");
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(nThreads);
    for (size_t i = 0; i < nThreads; ++i) workers.emplace_back(workerFn);
    if (total > 0)
        logInfo("buildOctree: phase C on " + std::to_string(nThreads) + " thread(s), " +
                std::to_string(total) + " chunks");
    const auto phaseCStart = std::chrono::steady_clock::now();

    // The coordinator can throw too (hierarchy/coarse growth = bad_alloc).
    // Catch everything so the workers are ALWAYS joined before unwinding —
    // destroying a joinable std::thread is std::terminate.
    size_t done = 0;
    try {
        while (nextToConsume < total) {
            ChunkResult res;
            {
                std::unique_lock<std::mutex> lk(mx);
                cvConsume.wait(lk, [&] {
                    return aborted || readyResults.count(nextToConsume) != 0;
                });
                if (aborted) break;
                auto it = readyResults.find(nextToConsume);
                res = std::move(it->second);
                readyResults.erase(it);
                ++nextToConsume;
                cvProduce.notify_all(); // an in-flight slot freed up
            }
            if (opts.cancel && opts.cancel->load()) {
                signalAbort(nullptr);
                break;
            }

            ++done;
            if (!res.empty) {
                // Rebase chunk-local records into the global hierarchy + file.
                const uint32_t base = (uint32_t)hierarchy.size();
                for (NodeRecord& rec : res.records) {
                    for (int o = 0; o < 8; ++o)
                        if (rec.children[o] != kNoChild) rec.children[o] += base;
                    rec.byteOffset += offset;
                }
                if (!res.blob.empty()) {
                    if (isPackage) {
                        if (!pkg->Write(res.blob.data(), res.blob.size()))
                            signalAbort("octree.bin package write failed");
                    } else if (std::fwrite(res.blob.data(), 1, res.blob.size(), payload) != res.blob.size()) {
                        signalAbort("octree.bin write failed (disk full?)");
                    }
                }
                offset += res.blob.size();
                hierarchy.insert(hierarchy.end(), res.records.begin(), res.records.end());
                chunkRoots[res.chunkIndex] = base + res.localRootIdx;
                coarse.insert(coarse.end(), res.coarseSamples.begin(), res.coarseSamples.end());
            }

            if ((done % 64) == 0 || done == total) {
                std::string msg = "buildOctree: indexed " + std::to_string(done) + "/" +
                                  std::to_string(total) + " chunks";
                logInfo(msg);
                if (opts.progressCb) {
                    float pct = total == 0 ? 1.0f : (float)done / total;
                    // Subtree building maps from 0.5 to 1.0
                    opts.progressCb(0.5f + pct * 0.5f, msg);
                }
            }
        }
    } catch (const std::exception& e) {
        signalAbort(e.what());
    } catch (...) {
        signalAbort("unknown exception in chunk coordinator");
    }

    for (auto& w : workers) w.join();

    if (aborted) {
        if (isPackage) pkg->EndFile();
        else if (payload) std::fclose(payload);
        if (failed) logError("buildOctree: failed — " + failReason);
        else        logWarn("buildOctree: cancelled by user");
        return false;
    }
    if (total > 0) {
        double secs = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - phaseCStart).count();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "buildOctree: phase C took %.2fs", secs);
        logInfo(buf);
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
        ctx.pkg = pkg.get();
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

    if (isPackage) pkg->EndFile();
    else if (payload) std::fclose(payload);

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

    ProjectMetadata pmeta{};
    std::strncpy(pmeta.projectName, "PointForge Project", sizeof(pmeta.projectName) - 1);
    std::strncpy(pmeta.author, "Unknown", sizeof(pmeta.author) - 1);
    std::strncpy(pmeta.company, "Unknown", sizeof(pmeta.company) - 1);
    std::strncpy(pmeta.description, "Converted by PointForge", sizeof(pmeta.description) - 1);
    std::strncpy(pmeta.units, "Meters", sizeof(pmeta.units) - 1);
    std::strncpy(pmeta.coordinateSystem, "Local", sizeof(pmeta.coordinateSystem) - 1);
    pmeta.converterVersion = 1;
    pmeta.buildVersion = 1;

    bool ok = writeMetaBin(outDir, meta, pkg.get()) &&
              writeProjectMetadataBin(outDir, pmeta, pkg.get()) &&
              writeMetadataJson(outDir, meta, pkg.get()) &&
              writeHierarchy(outDir, hierarchy, pkg.get());

    if (isPackage && pkg) {
        if (!pkg->Finalize()) {
            logError("buildOctree: failed to finalize package");
            ok = false;
        }
    }

    // ---- cleanup ----------------------------------------------------------
    if (!opts.keepChunks) fs::remove_all(chunkDir, ec);

    logInfo("buildOctree: wrote " + std::to_string(hierarchy.size()) + " nodes, " +
            std::to_string(cs.pointCount) + " points, root index " +
            std::to_string(rootNodeIndex));
    return ok;
}

} // namespace pf
