#include "viewer/OctreeStore.h"
#include "common/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#ifdef PF_WITH_ZSTD
#include <zstd.h>
#endif

namespace pf {

OctreeStore::~OctreeStore() {
    clear();
}

void OctreeStore::clear() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    
    std::lock_guard<std::mutex> lk(mtx_);
    inflight_.clear();
    requestQueue_.clear();
    requestFrame_.clear();
    ready_.clear();
    nodes_.clear();
    octreePath_.clear();
    stop_ = false; // Reset for next load
}

bool OctreeStore::load(const std::string& dir) {
    // meta.bin
    {
        std::ifstream f(dir + "/meta.bin", std::ios::binary | std::ios::ate);
        if (!f) { logError("OctreeStore: missing meta.bin in " + dir); return false; }
        std::streamsize len = f.tellg();
        f.seekg(0);
        if (len < 104 || len > sizeof(meta_)) {
            logError("OctreeStore: bad meta.bin size"); return false;
        }
        std::memset(&meta_, 0, sizeof(meta_)); // zero init for v1
        f.read(reinterpret_cast<char*>(&meta_), len);
        if (!f || std::memcmp(meta_.magic, "PFO1", 4) != 0) {
            logError("OctreeStore: bad meta.bin magic/size"); return false;
        }
    }
    // hierarchy.bin
    {
        std::ifstream f(dir + "/hierarchy.bin", std::ios::binary | std::ios::ate);
        if (!f) { logError("OctreeStore: missing hierarchy.bin"); return false; }
        std::streamsize bytes = f.tellg();
        f.seekg(0);
        nodes_.resize((size_t)bytes / sizeof(NodeRecord));
        f.read(reinterpret_cast<char*>(nodes_.data()), bytes);
        if (nodes_.size() != meta_.nodeCount)
            logWarn("OctreeStore: nodeCount mismatch (meta " +
                    std::to_string(meta_.nodeCount) + " vs file " +
                    std::to_string(nodes_.size()) + ")");
    }

    octreePath_ = dir + "/octree.bin";
    quant_.scale  = { meta_.scale[0],  meta_.scale[1],  meta_.scale[2]  };
    quant_.offset = { meta_.offset[0], meta_.offset[1], meta_.offset[2] };
    hasColor_ = meta_.hasColor != 0;
    center_ = glm::dvec3(meta_.cubeMin[0] + meta_.cubeSize * 0.5,
                         meta_.cubeMin[1] + meta_.cubeSize * 0.5,
                         meta_.cubeMin[2] + meta_.cubeSize * 0.5);

    computeCubes();

    worker_ = std::thread(&OctreeStore::workerLoop, this);
    logInfo("OctreeStore: loaded " + std::to_string(nodes_.size()) + " nodes, " +
            std::to_string(meta_.pointCount) + " points");
    return true;
}

void OctreeStore::computeCubes() {
    cubes_.assign(nodes_.size(), NodeCube{});
    if (nodes_.empty()) return;

    // Iterative DFS from the root, assigning each node its cube via childCube().
    struct Item { uint32_t idx; double min[3]; double size; };
    std::vector<Item> stack;
    Item root;
    root.idx = meta_.rootNodeIndex;
    root.min[0] = meta_.cubeMin[0]; root.min[1] = meta_.cubeMin[1]; root.min[2] = meta_.cubeMin[2];
    root.size = meta_.cubeSize;
    stack.push_back(root);

    while (!stack.empty()) {
        Item it = stack.back(); stack.pop_back();
        if (it.idx >= nodes_.size()) continue;
        NodeCube& nc = cubes_[it.idx];
        nc.min[0] = it.min[0]; nc.min[1] = it.min[1]; nc.min[2] = it.min[2];
        nc.size = it.size;

        const NodeRecord& rec = nodes_[it.idx];
        for (int o = 0; o < 8; ++o) {
            if (rec.children[o] == kNoChild) continue;
            Item ch; ch.idx = rec.children[o];
            childCube(it.min, it.size, o, ch.min, ch.size);
            stack.push_back(ch);
        }
    }
}

double OctreeStore::nodeSpacing(uint8_t level) const {
    return meta_.rootSpacing / std::pow(2.0, (double)level);
}

GpuVertex OctreeStore::convert(const PackedPoint& p) const {
    Vec3d w = quant_.unpack(p);
    GpuVertex v;
    v.x = (float)(w.x - center_.x);
    v.y = (float)(w.y - center_.y);
    v.z = (float)(w.z - center_.z);
    if (hasColor_) {
        v.r = (uint8_t)(p.r >> 8);
        v.g = (uint8_t)(p.g >> 8);
        v.b = (uint8_t)(p.b >> 8);
    } else {
        uint8_t g = (uint8_t)(p.intensity >> 8);
        if (g == 0) g = 180; // visible default when no colour/intensity
        v.r = v.g = v.b = g;
    }
    v.a = 255;
    return v;
}

void OctreeStore::requestLoad(uint32_t nodeIndex, uint64_t frame) {
    std::lock_guard<std::mutex> lk(mtx_);
    requestFrame_[nodeIndex] = frame; // refresh "last wanted" even if already queued
    if (inflight_.count(nodeIndex)) return;
    inflight_.insert(nodeIndex);
    requestQueue_.push_back(nodeIndex);
    cv_.notify_one();
}

void OctreeStore::purgeStale(uint64_t frame, uint64_t maxAgeFrames, size_t maxReady) {
    std::lock_guard<std::mutex> lk(mtx_);
    // Drop queued-but-not-started loads the camera has moved past. Entries are
    // removed from inflight_ too so they can be re-requested if seen again.
    if (frame > maxAgeFrames) {
        const uint64_t cutoff = frame - maxAgeFrames;
        std::deque<uint32_t> kept;
        for (uint32_t idx : requestQueue_) {
            auto it = requestFrame_.find(idx);
            if (it != requestFrame_.end() && it->second >= cutoff) {
                kept.push_back(idx);
            } else {
                inflight_.erase(idx);
                requestFrame_.erase(idx);
            }
        }
        requestQueue_.swap(kept);
    }
    // Cap ready results: a fast-moving view can produce uploads quicker than the
    // main thread drains them. Drop the oldest (front) — the newest are the most
    // likely to still be on screen. markEvicted-style cleanup keeps inflight_ sane.
    if (ready_.size() > maxReady) {
        size_t drop = ready_.size() - maxReady;
        for (size_t i = 0; i < drop; ++i) {
            inflight_.erase(ready_[i].nodeIndex);
            requestFrame_.erase(ready_[i].nodeIndex);
        }
        ready_.erase(ready_.begin(), ready_.begin() + drop);
    }
}

bool OctreeStore::popResult(LoadResult& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (ready_.empty()) return false;
    out = std::move(ready_.back());
    ready_.pop_back();
    return true;
}

void OctreeStore::markEvicted(uint32_t nodeIndex) {
    std::lock_guard<std::mutex> lk(mtx_);
    inflight_.erase(nodeIndex);
    requestFrame_.erase(nodeIndex);
}

size_t OctreeStore::pendingRequests() {
    std::lock_guard<std::mutex> lk(mtx_);
    return requestQueue_.size();
}

bool OctreeStore::readNodeInto(std::ifstream& in, const NodeRecord& rec,
                               std::vector<PackedPoint>& raw) const {
    raw.resize(rec.pointCount);
    if (!rec.pointCount) return true;

    const size_t expectedRawBytes = rec.pointCount * sizeof(PackedPoint);
    in.seekg((std::streamoff)rec.byteOffset);
    if (rec.byteSize < expectedRawBytes) {
#ifdef PF_WITH_ZSTD
        std::vector<uint8_t> cbuf(rec.byteSize);
        in.read(reinterpret_cast<char*>(cbuf.data()), rec.byteSize);
        size_t dSize = ZSTD_decompress(raw.data(), expectedRawBytes, cbuf.data(), rec.byteSize);
        if (ZSTD_isError(dSize) || dSize != expectedRawBytes) {
            logError("OctreeStore: ZSTD decompress failed");
            return false;
        }
#else
        logError("OctreeStore: compressed node found, but built without ZSTD support");
        return false;
#endif
    } else {
        in.read(reinterpret_cast<char*>(raw.data()), rec.byteSize);
    }
    return (bool)in;
}

void OctreeStore::workerLoop() {
    std::ifstream in(octreePath_, std::ios::binary);
    if (!in) { logError("OctreeStore worker: cannot open " + octreePath_); return; }

    std::vector<PackedPoint> raw;
    for (;;) {
        uint32_t idx;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&]{ return stop_ || !requestQueue_.empty(); });
            if (stop_) return;
            idx = requestQueue_.front();
            requestQueue_.pop_front();
        }
        if (idx >= nodes_.size()) continue;
        const NodeRecord& rec = nodes_[idx];

        readNodeInto(in, rec, raw);

        LoadResult res;
        res.nodeIndex = idx;
        res.verts.resize(rec.pointCount);
        for (uint32_t i = 0; i < rec.pointCount; ++i) res.verts[i] = convert(raw[i]);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            ready_.push_back(std::move(res));
        }
    }
}

// Slab test: does the ray (origin O, unit dir D) hit AABB [mn,mx]? Returns the
// entry distance tEnter (>=0, clamped) so callers can prune by along-ray order.
static bool rayAabb(const glm::dvec3& O, const glm::dvec3& D,
                    const glm::dvec3& mn, const glm::dvec3& mx, double& tEnter) {
    double t0 = 0.0, t1 = std::numeric_limits<double>::max();
    for (int a = 0; a < 3; ++a) {
        double d = D[a];
        if (std::fabs(d) < 1e-12) {                 // ray parallel to slab
            if (O[a] < mn[a] || O[a] > mx[a]) return false;
        } else {
            double inv = 1.0 / d;
            double ta = (mn[a] - O[a]) * inv;
            double tb = (mx[a] - O[a]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::fmax(t0, ta);
            t1 = std::fmin(t1, tb);
            if (t0 > t1) return false;
        }
    }
    tEnter = t0;
    return true;
}

bool OctreeStore::pickPoint(const glm::vec3& rayOriginCentered, const glm::vec3& rayDir,
                            double tolPerDist, glm::dvec3& hitWorld,
                            uint64_t maxScanPoints) const {
    if (nodes_.empty() || octreePath_.empty()) return false;

    // Work in world space: cubes_ and unpacked points are world coords, but the
    // camera ray arrives centred (world - cubeCentre), so re-add the centre.
    const glm::dvec3 O = glm::dvec3(rayOriginCentered) + center_;
    glm::dvec3 D = glm::normalize(glm::dvec3(rayDir));

    std::ifstream in(octreePath_, std::ios::binary);
    if (!in) { logError("OctreeStore::pickPoint: cannot open " + octreePath_); return false; }

    std::vector<PackedPoint> raw;
    uint64_t scanned = 0;
    double bestT = std::numeric_limits<double>::max();
    bool   found = false;

    // Iterative DFS over nodes whose cube the ray intersects. The MNO stores
    // points at every level, so we test points in each intersected node, not
    // just leaves — the coarse levels still give a usable hit when zoomed out.
    std::vector<uint32_t> stack;
    stack.push_back(rootIndex());
    while (!stack.empty() && scanned < maxScanPoints) {
        uint32_t idx = stack.back(); stack.pop_back();
        if (idx >= nodes_.size()) continue;
        const NodeCube& nc = cubes_[idx];
        glm::dvec3 mn(nc.min[0], nc.min[1], nc.min[2]);
        glm::dvec3 mx = mn + glm::dvec3(nc.size);
        double tEnter;
        if (!rayAabb(O, D, mn, mx, tEnter)) continue;
        if (tEnter > bestT) continue; // whole cube is past the current best hit

        const NodeRecord& rec = nodes_[idx];
        if (rec.pointCount && readNodeInto(in, rec, raw)) {
            for (uint32_t i = 0; i < rec.pointCount; ++i) {
                glm::dvec3 P(raw[i].x * quant_.scale.x + quant_.offset.x,
                             raw[i].y * quant_.scale.y + quant_.offset.y,
                             raw[i].z * quant_.scale.z + quant_.offset.z);
                glm::dvec3 v = P - O;
                double t = glm::dot(v, D);
                if (t <= 0.0 || t >= bestT) continue;       // behind eye / not closer
                double perp = glm::length(v - D * t);
                if (perp <= t * tolPerDist) { bestT = t; hitWorld = P; found = true; }
            }
            scanned += rec.pointCount;
        }

        for (int o = 0; o < 8; ++o)
            if (rec.children[o] != kNoChild) stack.push_back(rec.children[o]);
    }
    return found;
}

} // namespace pf
