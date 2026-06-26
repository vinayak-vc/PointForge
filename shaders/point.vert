#version 330 core

// Per-vertex: position is relative to the octree cube centre (kept in float for
// GPU precision; the centre is re-added on the CPU side via the view matrix).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

uniform mat4  uMVP;
uniform float uPointSize;    // base size in pixels
uniform float uAttenuation;  // 0 = constant pixel size, 1 = perspective attenuation
uniform float uViewportH;    // viewport height in pixels (for attenuation)

out vec3 vColor;
out vec3 vWorldPos;

void main() {
    vec4 clip = uMVP * vec4(inPos, 1.0);
    gl_Position = clip;

    float size = uPointSize;
    if (uAttenuation > 0.5 && clip.w > 0.0) {
        // larger when near, smaller when far
        size = uPointSize * (uViewportH * 0.001) / clip.w;
    }
    gl_PointSize = clamp(size, 1.0, 64.0);

    vColor = inColor;
    vWorldPos = inPos;
}
