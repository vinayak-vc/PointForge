#include "viewer/PointRenderer.h"
#include <cstddef>
#include <limits>

namespace pf {

PointRenderer::~PointRenderer() {
    clear();
}

void PointRenderer::clear() {
    for (auto& pair : nodes_) destroy(pair.second);
    nodes_.clear();
    totalBytes_ = 0;
    totalPoints_ = 0;
}

void PointRenderer::destroy(GpuNode& n) {
    if (n.vbo) glDeleteBuffers(1, &n.vbo);
    if (n.vao) glDeleteVertexArrays(1, &n.vao);
    totalBytes_  -= n.bytes;
    totalPoints_ -= (uint64_t)n.count;
    n = GpuNode{};
}

void PointRenderer::upload(uint32_t idx, const std::vector<GpuVertex>& verts) {
    if (nodes_.count(idx)) return;          // already resident
    if (verts.empty()) { nodes_[idx] = GpuNode{}; return; }

    GpuNode n;
    n.count = (GLsizei)verts.size();
    n.bytes = verts.size() * sizeof(GpuVertex);

    glGenVertexArrays(1, &n.vao);
    glGenBuffers(1, &n.vbo);
    glBindVertexArray(n.vao);
    glBindBuffer(GL_ARRAY_BUFFER, n.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n.bytes, verts.data(), GL_STATIC_DRAW);

    // location 0: position (3 x float)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                          (void*)offsetof(GpuVertex, x));
    // location 1: colour. We read 3 normalised uint8 (RGB) into the shader's
    // vec3 inColor; GpuVertex.a is only 4-byte alignment padding and is unused.
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuVertex),
                          (void*)offsetof(GpuVertex, r));
    // location 2: intensity (uint16 -> normalised float 0..1).
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(GpuVertex),
                          (void*)offsetof(GpuVertex, intensity));
    // location 3: classification (uint8 -> float code 0..255, not normalised).
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GpuVertex),
                          (void*)offsetof(GpuVertex, classification));

    glBindVertexArray(0);

    totalBytes_  += n.bytes;
    totalPoints_ += (uint64_t)n.count;
    nodes_[idx] = n;
}

void PointRenderer::draw(uint32_t idx, uint64_t frame) {
    auto it = nodes_.find(idx);
    if (it == nodes_.end()) return;
    it->second.lastUsed = frame;
    if (it->second.count == 0) return;
    glBindVertexArray(it->second.vao);
    glDrawArrays(GL_POINTS, 0, it->second.count);
}

void PointRenderer::evictToBudget(size_t maxBytes, uint64_t frame, OctreeStore& store) {
    while (totalBytes_ > maxBytes) {
        // find least-recently-used node that is not in use this frame
        uint32_t victim = 0;
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        bool found = false;
        for (auto& kv : nodes_) {
            if (kv.second.lastUsed >= frame) continue; // visible this frame; keep
            if (kv.second.lastUsed < oldest) { oldest = kv.second.lastUsed; victim = kv.first; found = true; }
        }
        if (!found) break; // everything resident is needed this frame
        destroy(nodes_[victim]);
        nodes_.erase(victim);
        store.markEvicted(victim);
    }
}

} // namespace pf
