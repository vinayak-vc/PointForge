#pragma once

#include "common/AABB.h"
#include "common/Point.h"
#include <cstdint>
#include <fstream>
#include <string>

namespace pf {

class DxfWriter {
public:
    DxfWriter() = default;
    DxfWriter(const DxfWriter&) = delete;
    DxfWriter& operator=(const DxfWriter&) = delete;

    bool begin(const std::string& path, int sliceAxis);
    void writeOutline(const AABB& box);
    void writePoint(const Point& point);
    bool finish();

    const std::string& error() const { return error_; }

private:
    double projectedU(const Point& point) const;
    double projectedV(const Point& point) const;
    double projectedU(const Vec3d& point) const;
    double projectedV(const Vec3d& point) const;
    int layerColor(uint8_t classification) const;

    std::ofstream out_;
    std::string error_;
    int sliceAxis_ = 2;
};

} // namespace pf
