#version 330 core

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
