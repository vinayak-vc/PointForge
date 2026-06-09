#include "viewer/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace pf {

glm::vec3 Camera::front() const {
    float cy = std::cos(glm::radians(yaw));
    float sy = std::sin(glm::radians(yaw));
    float cp = std::cos(glm::radians(pitch));
    float sp = std::sin(glm::radians(pitch));
    return glm::normalize(glm::vec3(cy * cp, sp, sy * cp));
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(front(), glm::vec3(0, 1, 0)));
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + front(), glm::vec3(0, 1, 0));
}

glm::mat4 Camera::proj() const {
    return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
}

void Camera::addYawPitch(float dYaw, float dPitch) {
    yaw   += dYaw * lookSpeed;
    pitch += dPitch * lookSpeed;
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}

} // namespace pf
