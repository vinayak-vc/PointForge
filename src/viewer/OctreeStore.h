#pragma once
#include "common/OctreeFormat.h"
#include "common/PointFormat.h"
#include <glm/glm.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace pf {

// GPU-ready vertex (16 bytes). Position is relative to the octree cube centre.
#pragma pack(push, 1)
struct GpuVertex {
    float   x, y, z;
    uint8_t r, g, b, a;
};
#pragma pack(pop)

struct NodeCube {
    double min[3];
    double size;
};

struct LoadResult {
    uint32_t nodeIndex;
    std::vector<GpuVertex> verts;
};

// Loads the octree metadata + hierarchy and streams node payloads off disk on a
// background thread. The hierarchy stays resident (small); only requested node
// payloads are read and converted to GPU vertices.
class OctreeStore {
public:
    ~OctreeStore();

    bool load(const std::string& dir);

    const FileMetadata&            meta()  const { return meta_; }
    const std::vector<NodeRecord>& nodes() const { return nodes_; }
    const NodeCube&                cube(uint32_t i) const { return cubes_[i]; }
    uint32_t                       rootIndex() const { return meta_.rootNodeIndex; }
    glm::dvec3                     cubeCenter() const { return center_; }

    // Node spacing in world units (rootSpacing / 2^level).
    double nodeSpacing(uint8_t level) const;

    // Enqueue an async payload load (no-op if already requested/queued).
    void requestLoad(uint32_t nodeIndex);
    // Pop one finished load on the main thread; returns false if none ready.
    bool popResult(LoadResult& out);
    // Tell the store a node is no longer resident (so it may be requested again).
    void markEvicted(uint32_t nodeIndex);

    size_t pendingRequests();

private:
    void computeCubes();
    void workerLoop();
    GpuVertex convert(const PackedPoint& p) const;

    FileMetadata            meta_{};
    std::vector<NodeRecord> nodes_;
    std::vector<NodeCube>   cubes_;
    Quantization            quant_;
    glm::dvec3              center_{0, 0, 0};
    bool                    hasColor_ = false;

    std::string             octreePath_;

    // worker
    std::thread             worker_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::deque<uint32_t>    requestQueue_;
    std::vector<LoadResult> ready_;
    std::unordered_set<uint32_t> inflight_;
    std::atomic<bool>       stop_{false};
};

} // namespace pf
