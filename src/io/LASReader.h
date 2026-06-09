#pragma once
#include "io/PointReader.h"
#include <string>

namespace pf {

// LAS / LAZ reader backed by the LASzip C API (laszip_api). Streams points one
// at a time from the codec, so compressed LAZ at any size stays out-of-core.
// If the project was built without LASzip (PF_WITH_LAS undefined), construction
// fails gracefully and good() returns false.
class LASReader : public PointReader {
public:
    explicit LASReader(const std::string& path);
    ~LASReader() override;

    bool good() const override { return ok_; }
    uint64_t pointCount() const override { return pointCount_; }
    bool hasHeaderBounds() const override { return ok_; }
    AABB headerBounds() const override { return bounds_; }
    size_t read(Point* out, size_t maxPoints) override;

private:
    void*    reader_ = nullptr;   // laszip_POINTER (opaque to avoid leaking the header here)
    bool     ok_ = false;
    bool     hasColor_ = false;
    uint64_t pointCount_ = 0;
    uint64_t read_ = 0;
    AABB     bounds_;
};

} // namespace pf
