#pragma once
#include "io/PointReader.h"
#include <cstdio>
#include <string>

namespace pf {

// Parser for whitespace/comma-separated text point clouds: .pts, .xyz, .txt, .csv
//
// Column layout is auto-detected from the first data line:
//   3 cols            -> x y z
//   4 cols            -> x y z intensity            (PTS style)
//   6 cols            -> x y z r g b
//   7 cols            -> x y z intensity r g b      (PTS style)
// A leading line containing a single integer (PTS point count) is consumed.
// '#' or '//' comment lines and blank lines are skipped.
class TextReader : public PointReader {
public:
    explicit TextReader(const std::string& path);
    ~TextReader() override;

    bool good() const override { return file_ != nullptr; }
    uint64_t pointCount() const override { return declaredCount_; }
    size_t read(Point* out, size_t maxPoints) override;

private:
    bool parseLine(const char* line, Point& p) const;
    void detectColumns(const char* line);

    FILE*    file_ = nullptr;
    uint64_t declaredCount_ = 0;  // from a PTS count header, else 0
    int      columns_ = 0;        // 0 until detected
    bool     colorIsByte_ = true; // assume 0..255 colour in text; promoted to 16-bit
    char     lineBuf_[1024];
};

} // namespace pf
