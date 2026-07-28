#pragma once
#include <GL/glew.h>
#include "io/SplatReader.h"
#include "viewer/Shader.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace pf {

class SplatRenderer {
public:
    SplatRenderer();
    ~SplatRenderer();

    // Initialize GL resources and compile shaders
    bool init();

    // Free GPU buffers and reset
    void clear();

    // Upload splats to GPU instanced vertex buffer
    void upload(const SplatCloudData& data);

    // Sort splats back-to-front relative to view matrix
    void sortSplats(const glm::mat4& view);

    // Render loaded splats
    void draw(const glm::mat4& proj, const glm::mat4& view, const glm::vec2& viewportSize);

    size_t splatCount() const { return count_; }
    bool isReady() const { return initialized_ && count_ > 0; }

private:
#pragma pack(push, 1)
    struct GpuSplatRecord {
        glm::vec3 position;
        glm::vec3 scale;
        glm::vec4 rot; // w, x, y, z
        float opacity;
        glm::vec3 color;
    };
#pragma pack(pop)

    struct DistanceIndex {
        float depth;
        uint32_t index;
    };

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint instanceVbo_ = 0;

    size_t count_ = 0;
    bool initialized_ = false;
    Shader shader_;

    std::vector<GpuSplatRecord> records_;        // canonical (upload) order
    std::vector<GpuSplatRecord> sortedRecords_;  // back-to-front scratch buffer
    glm::mat4 lastSortView_{0.0f};
};

} // namespace pf
