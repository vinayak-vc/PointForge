#pragma once
#include "common/Point.h"
#include "common/AABB.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace pf {

// Streaming point source. Implementations never load the whole file into RAM;
// the indexer pulls points in batches via read(). This is what makes the
// pipeline out-of-core on the input side.
class PointReader {
public:
    virtual ~PointReader() = default;

    // True if the source opened successfully and is usable.
    virtual bool good() const = 0;

    // Total point count if known from a header, else 0 (e.g. streaming text).
    virtual uint64_t pointCount() const = 0;

    // True if exact bounds are available from the file header (LAS/E57 usually).
    virtual bool hasHeaderBounds() const { return false; }
    virtual AABB headerBounds() const { return {}; }

    // Fill `out` with up to `maxPoints` points. Returns the number written.
    // A return value of 0 means end-of-stream. Must be called until it returns 0.
    virtual size_t read(Point* out, size_t maxPoints) = 0;
};

using PointReaderPtr = std::unique_ptr<PointReader>;

} // namespace pf
