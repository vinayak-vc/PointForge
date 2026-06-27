#pragma once
// Auto-vendored copies of shaders/point.vert and shaders/point.frag, embedded so
// the standalone viewer needs no external shaders/ folder. Keep in sync with the
// files under shaders/ (the source of truth for editing).
namespace pf {

inline const char* kPointVertSrc = R"GLSL(#version 330 core

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
)GLSL";

inline const char* kPointFragSrc = R"GLSL(#version 330 core

in vec3 vColor;
in vec3 vWorldPos;
out vec4 fragColor;

uniform int uRound; // 1 = draw circular points, 0 = square
uniform int uColorMode; // 0 = True Color, 1 = Elevation, 2 = Solid Color
uniform vec2 uZBounds; // For elevation (minZ, maxZ) relative to center
uniform vec3 uSolidColor; // For solid color mode

uniform vec3 uClipMin; // For clipping planes
uniform vec3 uClipMax;

// Simple colormap (Turbo-like)
vec3 turbo(float x) {
    x = clamp(x, 0.0, 1.0);
    vec4 k = vec4(0.13, 0.99, 0.97, -0.49);
    vec4 c = vec4(
        3.24 * x - 2.15,
        -5.5 * x * x + 6.32 * x - 0.72,
        -4.5 * x * x + 1.25 * x + 0.88,
        1.0
    );
    // Approximation for a nice jet/turbo map
    return clamp(vec3(c.x, c.y, c.z), 0.0, 1.0);
}

void main() {
    // Clipping planes
    if (vWorldPos.x < uClipMin.x || vWorldPos.x > uClipMax.x ||
        vWorldPos.y < uClipMin.y || vWorldPos.y > uClipMax.y ||
        vWorldPos.z < uClipMin.z || vWorldPos.z > uClipMax.z) {
        discard;
    }

    if (uRound == 1) {
        vec2 c = gl_PointCoord * 2.0 - 1.0;
        if (dot(c, c) > 1.0) discard;
    }

    vec3 outColor = vColor;
    if (uColorMode == 1) {
        float z = vWorldPos.z;
        float range = uZBounds.y - uZBounds.x;
        float t = range > 0.001 ? (z - uZBounds.x) / range : 0.5;
        outColor = turbo(t);
    } else if (uColorMode == 2) {
        outColor = uSolidColor;
    }

    fragColor = vec4(outColor, 1.0);
}
)GLSL";

} // namespace pf
