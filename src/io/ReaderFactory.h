#pragma once
#include "io/PointReader.h"
#include <string>

namespace pf {

// Opens the appropriate reader based on the file extension. Returns nullptr (and
// logs) if the format is unrecognised or its support library was not compiled in.
// Supported: .las/.laz (PF_WITH_LAS), .e57 (PF_WITH_E57), .ply (PF_WITH_PLY),
//            .pts/.xyz/.txt/.csv (always).
PointReaderPtr openPointReader(const std::string& path);

} // namespace pf
