#pragma once
#include "common/Vec3.h"
#include <limits>
#include <algorithm>

namespace pf {

// Axis-aligned bounding box in double precision.
struct AABB {
    Vec3d min{ std::numeric_limits<double>::max(),
               std::numeric_limits<double>::max(),
               std::numeric_limits<double>::max() };
    Vec3d max{ std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::lowest(),
               std::numeric_limits<double>::lowest() };

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void expand(const Vec3d& p) {
        min = minVec(min, p);
        max = maxVec(max, p);
    }
    void expand(const AABB& b) {
        min = minVec(min, b.min);
        max = maxVec(max, b.max);
    }

    Vec3d size() const { return max - min; }
    Vec3d center() const { return (min + max) * 0.5; }

    // Smallest cube that contains this box, anchored at min, centred on the box.
    // Returns {cubeMin, cubeSize}. Used to define the octree root.
    void toCube(Vec3d& cubeMin, double& cubeSize) const {
        Vec3d s = size();
        cubeSize = std::max({ s.x, s.y, s.z });
        Vec3d c = center();
        cubeMin = { c.x - cubeSize * 0.5,
                    c.y - cubeSize * 0.5,
                    c.z - cubeSize * 0.5 };
    }
};

} // namespace pf
