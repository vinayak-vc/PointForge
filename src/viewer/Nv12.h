#pragma once
#include <algorithm>
#include <cstdint>

namespace pf {

// RGB (tightly packed, BOTTOM-UP rows — glReadPixels origin) -> NV12
// (top-down Y plane + interleaved half-res UV plane). BT.601 integer math.
// Shared by the WebRTC live-stream encoder (RemoteServer.cpp) and the MP4
// exporter (VideoExporter.cpp) so both produce identical colour.
//
// nv12 must hold w*h + (w*h)/2 bytes. w and h should be even (H.264/NV12
// chroma subsampling); odd trailing row/column simply keeps the previous
// chroma sample.
inline void rgbToNv12BottomUp(const uint8_t* rgb, int w, int h, uint8_t* nv12) {
    uint8_t* yPlane  = nv12;
    uint8_t* uvPlane = nv12 + (size_t)w * h;

    for (int y = 0; y < h; ++y) {
        const uint8_t* srcRow = rgb + (size_t)(h - 1 - y) * w * 3; // flip to top-down
        uint8_t* dstY  = yPlane + (size_t)y * w;
        uint8_t* dstUV = uvPlane + (size_t)(y / 2) * w;

        for (int x = 0; x < w; ++x) {
            const int r = srcRow[x * 3 + 0];
            const int g = srcRow[x * 3 + 1];
            const int b = srcRow[x * 3 + 2];

            const int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            dstY[x] = (uint8_t)std::clamp(Y, 0, 255);

            if (y % 2 == 0 && x % 2 == 0 && x + 1 < w) {
                const int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                const int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                dstUV[x]     = (uint8_t)std::clamp(U, 0, 255);
                dstUV[x + 1] = (uint8_t)std::clamp(V, 0, 255);
            }
        }
    }
}

} // namespace pf
