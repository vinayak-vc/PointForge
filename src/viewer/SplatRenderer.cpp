#include "viewer/SplatRenderer.h"
#include "viewer/EmbeddedShaders.h"
#include "common/Log.h"
#include <algorithm>
#include <cmath>

namespace pf {

SplatRenderer::SplatRenderer() = default;

SplatRenderer::~SplatRenderer() {
    clear();
}

void SplatRenderer::clear() {
    if (instanceVbo_) { glDeleteBuffers(1, &instanceVbo_); instanceVbo_ = 0; }
    if (quadVbo_) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (quadVao_) { glDeleteVertexArrays(1, &quadVao_); quadVao_ = 0; }
    records_.clear();
    sortedRecords_.clear();
    count_ = 0;
    initialized_ = false;
}

bool SplatRenderer::init() {
    if (initialized_) return true;

    if (!shader_.loadFromSource(kSplatVertSrc, kSplatFragSrc)) {
        logError("SplatRenderer: Failed to compile splat shaders");
        return false;
    }

    // Static 2D quad geometry (-1,-1) to (1,1)
    static const float kQuadCorners[8] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glGenBuffers(1, &instanceVbo_);

    glBindVertexArray(quadVao_);

    // Location 0: Quad Corner (2 floats, per-vertex)
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadCorners), kQuadCorners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Instanced Attributes (locations 1..5)
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);

    // Location 1: vec3 position
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuSplatRecord), (void*)offsetof(GpuSplatRecord, position));
    glVertexAttribDivisor(1, 1);

    // Location 2: vec3 scale
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GpuSplatRecord), (void*)offsetof(GpuSplatRecord, scale));
    glVertexAttribDivisor(2, 1);

    // Location 3: vec4 rot
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GpuSplatRecord), (void*)offsetof(GpuSplatRecord, rot));
    glVertexAttribDivisor(3, 1);

    // Location 4: float opacity
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(GpuSplatRecord), (void*)offsetof(GpuSplatRecord, opacity));
    glVertexAttribDivisor(4, 1);

    // Location 5: vec3 color
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(GpuSplatRecord), (void*)offsetof(GpuSplatRecord, color));
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    initialized_ = true;
    logInfo("SplatRenderer: Initialized instanced GPU splatting pipeline");
    return true;
}

void SplatRenderer::upload(const SplatCloudData& data) {
    if (!initialized_ && !init()) return;

    if (data.splats.empty()) {
        clear();
        return;
    }

    records_.clear();
    records_.reserve(data.splats.size());

    for (const auto& s : data.splats) {
        GpuSplatRecord rec;
        rec.position = glm::vec3(s.position.x, s.position.y, s.position.z);
        rec.scale = glm::vec3(s.scale.x, s.scale.y, s.scale.z);
        rec.rot = glm::vec4(s.quaternion[0], s.quaternion[1], s.quaternion[2], s.quaternion[3]);
        rec.opacity = s.opacity;
        rec.color = glm::vec3(s.color[0], s.color[1], s.color[2]);
        records_.push_back(rec);
    }

    // Instance VBO is re-sorted in place each time the camera moves
    // (see sortSplats), so hint DYNAMIC_DRAW rather than STATIC_DRAW.
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, records_.size() * sizeof(GpuSplatRecord), records_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    count_ = records_.size();
    sortedRecords_.resize(count_);

    // Force a re-sort on the next draw regardless of the cached view matrix.
    lastSortView_ = glm::mat4(0.0f);

    logInfo("SplatRenderer: Uploaded " + std::to_string(count_) + " splats to GPU");
}

void SplatRenderer::sortSplats(const glm::mat4& view) {
    if (records_.empty() || count_ == 0) return;

    // Fast check if view matrix changed significantly
    if (view == lastSortView_) return;
    lastSortView_ = view;

    std::vector<DistanceIndex> depthPairs(count_);

    // Camera-space depth z = (V * p).z. Row 2 of the column-major view matrix.
    glm::vec4 viewZ(view[0][2], view[1][2], view[2][2], view[3][2]);

    for (size_t i = 0; i < count_; ++i) {
        const auto& pos = records_[i].position;
        depthPairs[i].depth = viewZ.x * pos.x + viewZ.y * pos.y + viewZ.z * pos.z + viewZ.w;
        depthPairs[i].index = static_cast<uint32_t>(i);
    }

    // Back-to-front: the camera looks down -Z, so the most negative depth is
    // farthest and must be drawn first for correct "over" alpha compositing.
    std::sort(depthPairs.begin(), depthPairs.end(), [](const DistanceIndex& a, const DistanceIndex& b) {
        return a.depth < b.depth;
    });

    // glDrawArraysInstanced walks the instance VBO in gl_InstanceID order and
    // ignores any bound element buffer, so the sorted order must live in the
    // instance VBO itself. Reorder from the canonical records_ and re-upload.
    sortedRecords_.resize(count_);
    for (size_t i = 0; i < count_; ++i) {
        sortedRecords_[i] = records_[depthPairs[i].index];
    }

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count_ * sizeof(GpuSplatRecord), sortedRecords_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SplatRenderer::draw(const glm::mat4& proj, const glm::mat4& view, const glm::vec2& viewportSize) {
    if (!initialized_ || count_ == 0) return;

    // Per-frame back-to-front depth sort
    sortSplats(view);

    shader_.use();
    shader_.setMat4("uProj", &proj[0][0]);
    shader_.setMat4("uView", &view[0][0]);
    shader_.setVec2("uViewportSize", viewportSize.x, viewportSize.y);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); // Don't write depth for alpha-blended splats

    glBindVertexArray(quadVao_);

    // Instance VBO is pre-sorted back-to-front by sortSplats().
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count_));

    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace pf
