#pragma once
#include "common/AABB.h"
#include "common/OctreeFormat.h"
#include "common/Point.h"
#include "common/PointFormat.h"
#include <glm/glm.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

namespace pf {

// GPU-ready vertex (20 bytes). Position is relative to the octree cube centre.
// intensity + classification are carried so the viewer can colour by them.
#pragma pack(push, 1)
struct GpuVertex {
    float    x, y, z;
    uint8_t  r, g, b, a;
    uint16_t intensity;
    uint8_t  classification;
    uint8_t  pad;
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
    // Both defined in OctreeStore.cpp: pkgReader_ is a unique_ptr to the
    // forward-declared PackageReader, so any TU that instantiates the
    // implicit constructor/destructor would need the complete type.
    OctreeStore();
    ~OctreeStore();

    // Reset store and stop worker threads to allow loading a new point cloud safely
    void clear();

    bool load(const std::string& dir);

    const FileMetadata&            meta()  const { return meta_; }
    const std::vector<NodeRecord>& nodes() const { return nodes_; }
    const NodeCube&                cube(uint32_t i) const { return cubes_[i]; }
    uint32_t                       rootIndex() const { return meta_.rootNodeIndex; }
    glm::dvec3                     cubeCenter() const { return center_; }

    // Node spacing in world units (rootSpacing / 2^level).
    double nodeSpacing(uint8_t level) const;

    // Enqueue an async payload load (no-op if already requested/queued). The
    // frame stamp records when the node was last wanted so the queue can be
    // pruned of requests the camera has since moved past (see purgeStale()).
    void requestLoad(uint32_t nodeIndex, uint64_t frame = 0);
    // Pop one finished load on the main thread; returns false if none ready.
    bool popResult(LoadResult& out);
    // Tell the store a node is no longer resident (so it may be requested again).
    void markEvicted(uint32_t nodeIndex);

    // Drop queued-but-not-yet-started loads that were last wanted more than
    // maxAgeFrames ago, plus any ready results that piled up beyond maxReady.
    // Frees the streaming queues when the view changes faster than disk I/O.
    void purgeStale(uint64_t frame, uint64_t maxAgeFrames, size_t maxReady);

    size_t pendingRequests();

    // ---- synchronous CPU picking (not the streaming path) -----------------
    // Cast a ray (centred space, dir unit) and return the nearest point within
    // a screen tolerance. tolPerDist = pickRadiusPixels / ssFactor: the world
    // tolerance grows linearly with along-ray distance so the pick "disc" stays
    // a fixed pixel size. Reads intersected node payloads off disk on the spot.
    // hitWorld is filled with the picked point in WORLD coords. Returns false
    // if nothing was hit. Bounded by scanning at most maxScanPoints points.
    bool pickPoint(const glm::vec3& rayOriginCentered, const glm::vec3& rayDir,
                   double tolPerDist, glm::dvec3& hitWorld,
                   uint64_t maxScanPoints = 8'000'000ull) const;

    using PointVisitor = std::function<bool(const Point&)>;

    // Synchronous bulk point query for exports. box is in WORLD coordinates.
    // maxDepth < 0 visits every intersecting node; otherwise it includes node
    // payloads up to and including maxDepth as a density/LOD cap. The visitor
    // returns false to stop early. Returns the number of points delivered.
    uint64_t forEachPointInBox(const AABB& box, int maxDepth,
                               const std::atomic<bool>* cancel,
                               const PointVisitor& visitor) const;

    // Cheap preflight count for UI warnings/progress: sums payload counts for
    // intersecting nodes up to maxDepth without reading octree.bin.
    uint64_t estimatePointsInBox(const AABB& box, int maxDepth) const;

private:
    void computeCubes();
    void workerLoop();
    GpuVertex convert(const PackedPoint& p) const;
    Point unpackPoint(const PackedPoint& p) const;
    // Read + (if needed) zstd-decompress one node's packed points via an open
    // stream. Shared by the streaming worker and the synchronous picker.
    bool readNodeInto(std::ifstream& in, const NodeRecord& rec,
                      std::vector<PackedPoint>& raw) const;

    FileMetadata            meta_{};
    std::vector<NodeRecord> nodes_;
    std::vector<NodeCube>   cubes_;
    Quantization            quant_;
    glm::dvec3              center_{0, 0, 0};
    bool                    hasColor_ = false;

    std::string             octreePath_;
    uint64_t                octreePackageOffset_ = 0;
    std::unique_ptr<class PackageReader> pkgReader_;

    // worker
    std::thread             worker_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::deque<uint32_t>    requestQueue_;
    std::vector<LoadResult> ready_;
    std::unordered_set<uint32_t> inflight_;
    std::unordered_map<uint32_t, uint64_t> requestFrame_; // node -> last-wanted frame
    std::atomic<bool>       stop_{false};
};

} // namespace pf
