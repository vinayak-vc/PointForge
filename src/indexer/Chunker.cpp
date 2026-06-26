#include "indexer/Chunker.h"
#include "io/ReaderFactory.h"
#include "common/Log.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

namespace pf {

namespace {
    constexpr size_t kReadBatch = 1u << 16; // points pulled from reader per call

    std::string chunkPath(const std::string& dir, uint32_t index) {
        return dir + "/" + std::to_string(index) + ".bin";
    }

    // Compute quantization that keeps relative coordinates within int32 range.
    Quantization makeQuant(const Vec3d& cubeMin, double cubeSize) {
        Quantization q;
        q.offset = cubeMin;
        double s = std::max(0.001, cubeSize / 2.0e9); // < 2^31 quantized span
        q.scale = { s, s, s };
        return q;
    }
}

bool runChunker(const std::string& inputPath,
                const std::string& chunkDir,
                int gridDepth,
                ChunkSet& out,
                uint64_t flushPointBudget,
                std::function<void(float, const std::string&)> progressCb) {
    // ---- determine bounds (and count if available) -----------------------
    AABB bounds;
    {
        PointReaderPtr r = openPointReader(inputPath);
        if (!r) return false;
        if (r->hasHeaderBounds()) {
            bounds = r->headerBounds();
            logInfo("Chunker: using header bounds");
        } else {
            logInfo("Chunker: scanning for bounds (no header bounds)...");
            std::vector<Point> buf(kReadBatch);
            size_t n;
            uint64_t seen = 0;
            uint64_t expectedPoints = r->pointCount();
            while ((n = r->read(buf.data(), buf.size())) > 0) {
                for (size_t i = 0; i < n; ++i) bounds.expand(buf[i].position);
                seen += n;
                if (progressCb && (seen % (1024 * 1024) < buf.size() || seen == expectedPoints)) {
                    float pct = expectedPoints > 0 ? (float)seen / expectedPoints : 0.0f;
                    progressCb(pct * 0.1f, "Scanning bounds: " + std::to_string(seen) + " points");
                }
            }
            logInfo("Chunker: bounds pass saw " + std::to_string(seen) + " points");
        }
    }
    if (!bounds.valid()) { logError("Chunker: empty / invalid bounds"); return false; }

    // ---- cube + quantization ---------------------------------------------
    out.bounds = bounds;
    bounds.toCube(out.cubeMin, out.cubeSize);
    // guard against a degenerate (zero-size) cube
    if (out.cubeSize <= 0) out.cubeSize = 1.0;
    out.quant = makeQuant(out.cubeMin, out.cubeSize);
    out.gridDepth = gridDepth;
    out.gridDim = 1 << gridDepth;
    out.chunkDir = chunkDir;

    const int    dim = out.gridDim;
    const size_t cellCount = (size_t)dim * dim * dim;
    const double cellSize = out.cubeSize / dim;

    // ---- chunking pass ----------------------------------------------------
    std::vector<std::vector<PackedPoint>> buffers(cellCount);
    std::vector<uint64_t> counts(cellCount, 0);
    std::vector<char>     created(cellCount, 0);
    uint64_t buffered = 0, total = 0, globalSeen = 0, inRAM = 0;
    bool anyColor = false;
    out.hasClassification = false;

    PointReaderPtr r = openPointReader(inputPath);
    if (!r) return false;
    uint64_t totalPts = r->pointCount();
    if (totalPts == 0) totalPts = out.pointCount; // fallback if reader doesn't know

    auto flushAll = [&]() {
        for (size_t c = 0; c < cellCount; ++c) {
            if (buffers[c].empty()) continue;
            FILE* f = std::fopen(chunkPath(chunkDir, (uint32_t)c).c_str(),
                                 created[c] ? "ab" : "wb");
            if (!f) { logError("Chunker: cannot write chunk " + std::to_string(c)); continue; }
            std::fwrite(buffers[c].data(), sizeof(PackedPoint), buffers[c].size(), f);
            std::fclose(f);
            created[c] = 1;
            buffers[c].clear();
            buffers[c].shrink_to_fit();
        }
        buffered = 0;
        inRAM = 0;
    };

    std::vector<Point> buf(kReadBatch);
    size_t n;
    while ((n = r->read(buf.data(), buf.size())) > 0) {
        for (size_t i = 0; i < n; ++i) {
            const Point& p = buf[i];
            if (p.hasColor) anyColor = true;
            if (p.classification != 0) out.hasClassification = true;

            int gx = (int)((p.position.x - out.cubeMin.x) / cellSize);
            int gy = (int)((p.position.y - out.cubeMin.y) / cellSize);
            int gz = (int)((p.position.z - out.cubeMin.z) / cellSize);
            gx = std::min(std::max(gx, 0), dim - 1);
            gy = std::min(std::max(gy, 0), dim - 1);
            gz = std::min(std::max(gz, 0), dim - 1);
            size_t idx = ((size_t)gx * dim + gy) * dim + gz;

            buffers[idx].push_back(out.quant.pack(p));
            counts[idx]++;
            ++buffered;
            ++total;
        }
        inRAM += n;
        globalSeen += n;
        
        if (progressCb && (globalSeen % (1024 * 512) < buf.size() || globalSeen == totalPts)) {
            float pct = totalPts > 0 ? (float)globalSeen / totalPts : 0.0f;
            progressCb(0.1f + pct * 0.4f, "Chunking: " + std::to_string(globalSeen) + " points");
        }

        if (inRAM >= flushPointBudget) flushAll();
    }
    flushAll();

    out.pointCount = total;
    out.hasColor = anyColor;

    // ---- collect occupied chunks -----------------------------------------
    for (size_t c = 0; c < cellCount; ++c) {
        if (counts[c] == 0) continue;
        ChunkSet::Chunk ch;
        ch.index = (uint32_t)c;
        ch.gz = (uint32_t)(c % dim);
        ch.gy = (uint32_t)((c / dim) % dim);
        ch.gx = (uint32_t)(c / ((size_t)dim * dim));
        ch.count = counts[c];
        ch.path = chunkPath(chunkDir, (uint32_t)c);
        out.chunks.push_back(ch);
    }

    logInfo("Chunker: " + std::to_string(total) + " points into " +
            std::to_string(out.chunks.size()) + " occupied chunks (grid " +
            std::to_string(dim) + "^3)");
    return total > 0;
}

} // namespace pf
