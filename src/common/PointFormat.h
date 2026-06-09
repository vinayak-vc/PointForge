#pragma once
#include "common/Point.h"
#include "common/Vec3.h"
#include <cstdint>
#include <cstring>
#include <cmath>

namespace pf {

// Fixed on-disk per-point record used by v1 of the octree format.
// Layout (little-endian, tightly packed, 20 bytes):
//   int32  x, y, z      quantized position = round((world - offset) / scale)
//   uint16 r, g, b
//   uint16 intensity
//
// Positions are stored relative to `offset` with `scale` (LAS convention) so we
// keep full source precision without paying for doubles on disk or on the GPU.
#pragma pack(push, 1)
struct PackedPoint {
    int32_t  x, y, z;
    uint16_t r, g, b;
    uint16_t intensity;
};
#pragma pack(pop)

static_assert(sizeof(PackedPoint) == 20, "PackedPoint must be tightly packed to 20 bytes");

constexpr uint32_t kBytesPerPoint = sizeof(PackedPoint);

// Quantization parameters shared by writer and reader.
struct Quantization {
    Vec3d offset{0, 0, 0};
    Vec3d scale{0.001, 0.001, 0.001}; // 1 mm default

    PackedPoint pack(const Point& p) const {
        PackedPoint out;
        out.x = static_cast<int32_t>(std::llround((p.position.x - offset.x) / scale.x));
        out.y = static_cast<int32_t>(std::llround((p.position.y - offset.y) / scale.y));
        out.z = static_cast<int32_t>(std::llround((p.position.z - offset.z) / scale.z));
        out.r = p.r; out.g = p.g; out.b = p.b;
        out.intensity = p.intensity;
        return out;
    }

    Vec3d unpack(const PackedPoint& p) const {
        return { p.x * scale.x + offset.x,
                 p.y * scale.y + offset.y,
                 p.z * scale.z + offset.z };
    }
};

} // namespace pf
