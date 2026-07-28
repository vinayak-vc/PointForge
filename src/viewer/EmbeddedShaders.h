#pragma once
// Auto-vendored copies of shaders/point.vert and shaders/point.frag, embedded so
// the standalone viewer needs no external shaders/ folder. Keep in sync with the
// files under shaders/ (the source of truth for editing).
namespace pf {

inline const char* kPointVertSrc = R"GLSL(#version 330 core

// Per-vertex: position is relative to the octree cube centre (kept in float for
// GPU precision; the centre is re-added on the CPU side via the view matrix).
layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inColor;
layout(location = 2) in float inIntensity;   // normalised 0..1 (from uint16)
layout(location = 3) in float inClass;       // ASPRS class code 0..255

uniform mat4  uMVP;
uniform float uPointSize;    // base size in pixels
uniform float uAttenuation;  // 0 = constant pixel size, 1 = perspective attenuation
uniform float uViewportH;    // viewport height in pixels (for attenuation)

out vec3  vColor;
out vec3  vWorldPos;
out float vIntensity;
out float vClass;

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
    vIntensity = inIntensity;
    vClass = inClass;
}
)GLSL";

inline const char* kPointFragSrc = R"GLSL(#version 330 core

in vec3  vColor;
in vec3  vWorldPos;
in float vIntensity;
in float vClass;
out vec4 fragColor;

uniform int  uRound;      // 1 = draw circular points, 0 = square
uniform int  uColorMode;  // 0 True, 1 Elevation, 2 Solid, 3 Intensity, 4 Classification
uniform vec2 uZBounds;    // For elevation (minZ, maxZ) relative to center
uniform vec3 uSolidColor; // For solid color mode

uniform vec3 uClipMin;    // For clipping planes
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

// ASPRS LAS classification palette (common codes; others -> yellowish).
vec3 classColor(float c) {
    int k = int(c + 0.5);
    if (k == 2) return vec3(0.55, 0.35, 0.17); // ground
    if (k == 3) return vec3(0.30, 0.70, 0.25); // low vegetation
    if (k == 4) return vec3(0.20, 0.55, 0.18); // medium vegetation
    if (k == 5) return vec3(0.10, 0.40, 0.12); // high vegetation
    if (k == 6) return vec3(0.85, 0.20, 0.20); // building
    if (k == 7) return vec3(1.00, 0.40, 0.70); // low point / noise
    if (k == 9) return vec3(0.20, 0.45, 0.95); // water
    if (k == 1) return vec3(0.70, 0.70, 0.70); // unclassified
    if (k == 0) return vec3(0.50, 0.50, 0.50); // created, never classified
    return vec3(0.90, 0.80, 0.30);             // other
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
    } else if (uColorMode == 3) {
        outColor = turbo(vIntensity);
    } else if (uColorMode == 4) {
        outColor = classColor(vClass);
    }

    fragColor = vec4(outColor, 1.0);
}
)GLSL";

// ---- Post-process: fullscreen triangle + Eye-Dome Lighting -----------------
// One pass that copies the offscreen colour buffer to the screen and, when
// uEdlOn, darkens depth discontinuities for much better depth perception on
// uncoloured clouds. No vertex buffer needed (gl_VertexID fullscreen triangle).
inline const char* kEdlVertSrc = R"GLSL(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

inline const char* kEdlFragSrc = R"GLSL(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform vec2  uTexel;     // 1/width, 1/height
uniform float uStrength;  // EDL darkening strength
uniform float uRadius;    // neighbour sample distance in pixels
uniform int   uEdlOn;

void main() {
    vec3 c = texture(uColor, vUv).rgb;
    if (uEdlOn == 0) { fragColor = vec4(c, 1.0); return; }

    float d = texture(uDepth, vUv).r;
    if (d >= 1.0) { fragColor = vec4(c, 1.0); return; } // background

    // Sum positive depth differences to 4 neighbours -> response at silhouettes.
    vec2 o[4] = vec2[4](vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1));
    float sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        float dn = texture(uDepth, vUv + o[i] * uTexel * uRadius).r;
        sum += max(0.0, d - dn);
    }
    float response = sum * 0.25;
    float shade = exp(-uStrength * response * 4000.0);
    fragColor = vec4(c * shade, 1.0);
}
)GLSL";

// ---- Measurement polyline: flat-colour lines + point markers, drawn as real
// scene geometry (into the same offscreen FBO the point cloud renders to) so
// it's captured by the web-remote video stream, not just an ImGui overlay
// only the PC's own screen would see.
inline const char* kLineVertSrc = R"GLSL(#version 330 core
layout(location = 0) in vec3 inPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(inPos, 1.0);
    gl_PointSize = 8.0;
}
)GLSL";

inline const char* kLineFragSrc = R"GLSL(#version 330 core
out vec4 fragColor;
uniform vec3 uColor;
void main() {
    fragColor = vec4(uColor, 1.0);
}
)GLSL";

// ---- 3D Gaussian Splatting Shaders -----------------------------------------
inline const char* kSplatVertSrc = R"GLSL(#version 330 core
layout(location = 0) in vec2 inQuadCorner;

layout(location = 1) in vec3 inPos;
layout(location = 2) in vec3 inScale;
layout(location = 3) in vec4 inRot; // w, x, y, z
layout(location = 4) in float inOpacity;
layout(location = 5) in vec3 inColor;

uniform mat4 uProj;
uniform mat4 uView;
uniform vec2 uViewportSize;

out vec3 vColor;
out float vOpacity;
out vec3 vConic;
out vec2 vCoord;

mat3 quaternionToRotation(vec4 q) {
    float w = q.x, x = q.y, y = q.z, z = q.w;
    return mat3(
        1.0 - 2.0*(y*y + z*z), 2.0*(x*y - w*z),     2.0*(x*z + w*y),
        2.0*(x*y + w*z),     1.0 - 2.0*(x*x + z*z), 2.0*(y*z - w*x),
        2.0*(x*z - w*y),     2.0*(y*z + w*x),     1.0 - 2.0*(x*x + y*y)
    );
}

void main() {
    vColor = inColor;
    vOpacity = inOpacity;

    vec4 camPos = uView * vec4(inPos, 1.0);
    if (camPos.z >= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }

    mat3 R = quaternionToRotation(inRot);
    mat3 S = mat3(
        inScale.x, 0.0, 0.0,
        0.0, inScale.y, 0.0,
        0.0, 0.0, inScale.z
    );
    mat3 M = R * S;
    mat3 Sigma = M * transpose(M);

    mat3 W = mat3(uView);

    float focalX = uProj[0][0] * uViewportSize.x * 0.5;
    float focalY = uProj[1][1] * uViewportSize.y * 0.5;

    float z = camPos.z;
    float invZ = 1.0 / z;
    float invZ2 = invZ * invZ;

    mat3 J = mat3(
        focalX * invZ, 0.0, 0.0,
        0.0, focalY * invZ, 0.0,
        -focalX * camPos.x * invZ2, -focalY * camPos.y * invZ2, 0.0
    );

    // 2D covariance: Sigma' = J W Sigma W^T J^T. Here J and W are in true
    // (non-transposed) form, so A = J*W and cov2D = A Sigma A^T.
    mat3 T = J * W;
    mat3 cov2D_3 = T * Sigma * transpose(T);

    float covA = cov2D_3[0][0] + 0.3;
    float covB = cov2D_3[0][1];
    float covC = cov2D_3[1][1] + 0.3;

    float det = covA * covC - covB * covB;
    if (det <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }

    float invDet = 1.0 / det;
    vConic = vec3(covC * invDet, -covB * invDet, covA * invDet);

    float mid = 0.5 * (covA + covC);
    float lambda = sqrt(max(0.1, mid * mid - det));
    float radius = ceil(3.0 * sqrt(max(mid + lambda, mid - lambda)));

    vec4 clipPos = uProj * camPos;
    vec2 pointCenter = clipPos.xy / clipPos.w;
    vec2 offset = inQuadCorner * radius * 2.0 / uViewportSize;

    vCoord = inQuadCorner * radius;
    gl_Position = vec4(pointCenter + offset, clipPos.z / clipPos.w, 1.0);
}
)GLSL";

inline const char* kSplatFragSrc = R"GLSL(#version 330 core
in vec3 vColor;
in float vOpacity;
in vec3 vConic;
in vec2 vCoord;

out vec4 fragColor;

void main() {
    float d2 = vConic.x * vCoord.x * vCoord.x + 2.0 * vConic.y * vCoord.x * vCoord.y + vConic.z * vCoord.y * vCoord.y;
    float power = -0.5 * d2;
    if (power > 0.0) discard;

    float alpha = min(0.99, vOpacity * exp(power));
    if (alpha < 1.0 / 255.0) discard;

    fragColor = vec4(vColor * alpha, alpha);
}
)GLSL";

} // namespace pf
