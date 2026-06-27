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
    // Z-up: yaw=0 looks +Y. Moving mouse right increases yaw (clockwise -> +X)
    return glm::normalize(glm::vec3(sy * cp, cy * cp, sp));
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(front(), glm::vec3(0, 0, 1))); // Z is up
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + front(), glm::vec3(0, 0, 1)); // Z is up
}

glm::mat4 Camera::proj() const {
    if (isOrtho) {
        float right = orthoSize * aspect;
        return glm::ortho(-right, right, -orthoSize, orthoSize, nearZ, farZ);
    }
    return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
}

void Camera::addYawPitch(float dYaw, float dPitch) {
    yaw   += dYaw * lookSpeed;
    pitch += dPitch * lookSpeed;
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}

void Camera::screenRay(float mx, float my, int w, int h,
                       glm::vec3& outOrigin, glm::vec3& outDir) const {
    // Pixel -> normalised device coords (y flips: pixel 0 is top of window).
    float ndcX = (w > 0) ? (2.0f * mx / (float)w - 1.0f) : 0.0f;
    float ndcY = (h > 0) ? (1.0f - 2.0f * my / (float)h) : 0.0f;

    glm::mat4 inv = glm::inverse(viewProj());
    glm::vec4 nearH = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f); // near plane
    glm::vec4 farH  = inv * glm::vec4(ndcX, ndcY,  1.0f, 1.0f); // far plane
    glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
    glm::vec3 farP  = glm::vec3(farH)  / farH.w;

    outOrigin = nearP;
    outDir    = glm::normalize(farP - nearP);
}

} // namespace pf
