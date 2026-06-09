#pragma once
#include "io/PointReader.h"
#include <memory>
#include <string>

namespace pf {

// E57 reader backed by libE57Format. Reads each Data3D scan block-by-block and
// applies the per-scan pose (rotation + translation) so multi-scan files land in
// a common coordinate frame. Implemented with pImpl because the libE57Format
// headers/types vary across versions; if PF_WITH_E57 is undefined the reader
// fails gracefully.
class E57Reader : public PointReader {
public:
    explicit E57Reader(const std::string& path);
    ~E57Reader() override;

    bool good() const override;
    uint64_t pointCount() const override;
    size_t read(Point* out, size_t maxPoints) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pf
