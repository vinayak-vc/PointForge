#pragma once
#include "common/Vec3.h"
#include <cstdint>

namespace pf {

// A single point as produced by the readers and consumed by the indexer.
// Position is double precision (world coordinates). Attributes are normalised
// to the widest common representation; the on-disk packer narrows them to the
// declared attribute layout.
struct Point {
    Vec3d    position;
    uint16_t r = 0, g = 0, b = 0;   // 16-bit colour (LAS/E57 native); 8-bit
                                    // sources are promoted by <<8.
    uint16_t intensity = 0;
    uint8_t  classification = 0;
    bool     hasColor = false;
};

} // namespace pf
