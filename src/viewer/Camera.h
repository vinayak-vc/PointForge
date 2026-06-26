#pragma once
#include <glm/glm.hpp>

namespace pf {

// First-person fly camera operating in "centred" space (world minus the octree
// cube centre) so coordinates stay small and float-friendly.
class Camera {
public:
    glm::vec3 position{0, 0, 0};
    float yaw   = -90.0f;   // degrees
    float pitch = 0.0f;
    float fovY  = 60.0f;    // degrees
    float nearZ = 0.05f;
    float farZ  = 100000.0f;
    float aspect = 16.0f / 9.0f;
    bool  isOrtho = false;
    float orthoSize = 100.0f; // Half-height of ortho view
    float moveSpeed = 10.0f;     // units/sec
    float lookSpeed = 0.15f;     // degrees/pixel

    glm::vec3 front() const;
    glm::vec3 right() const;
    glm::mat4 view() const;
    glm::mat4 proj() const;
    glm::mat4 viewProj() const { return proj() * view(); }

    void addYawPitch(float dYaw, float dPitch);
    void moveLocal(const glm::vec3& delta) { position += delta; }
};

} // namespace pf
