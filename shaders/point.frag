#version 330 core

in vec3 vColor;
out vec4 fragColor;

uniform int uRound; // 1 = draw circular points, 0 = square

void main() {
    if (uRound == 1) {
        vec2 c = gl_PointCoord * 2.0 - 1.0;
        if (dot(c, c) > 1.0) discard;
    }
    fragColor = vec4(vColor, 1.0);
}
