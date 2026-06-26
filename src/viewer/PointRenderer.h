#pragma once
#include <GL/glew.h>
#include "viewer/OctreeStore.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pf {

// Owns the GPU-resident node VBOs and enforces a memory budget via LRU eviction.
class PointRenderer {
public:
    ~PointRenderer();

    // Destroy all GPU resources and clear node mappings
    void clear();

    bool isResident(uint32_t idx) const { return nodes_.count(idx) != 0; }
    void upload(uint32_t idx, const std::vector<GpuVertex>& verts);
    void draw(uint32_t idx, uint64_t frame);   // assumes shader + uniforms bound
    void evictToBudget(size_t maxBytes, uint64_t frame, OctreeStore& store);

    size_t residentBytes()  const { return totalBytes_; }
    size_t residentNodes()  const { return nodes_.size(); }
    uint64_t pointsOnGpu()  const { return totalPoints_; }

private:
    struct GpuNode {
        GLuint   vao = 0, vbo = 0;
        GLsizei  count = 0;
        size_t   bytes = 0;
        uint64_t lastUsed = 0;
    };
    void destroy(GpuNode& n);

    std::unordered_map<uint32_t, GpuNode> nodes_;
    size_t   totalBytes_  = 0;
    uint64_t totalPoints_ = 0;
};

} // namespace pf
