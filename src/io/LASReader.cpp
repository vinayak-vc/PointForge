#include "io/LASReader.h"
#include "common/Log.h"

#ifdef PF_WITH_LAS
#include <laszip_api.h>

namespace pf {

LASReader::LASReader(const std::string& path) {
    laszip_POINTER reader = nullptr;
    if (laszip_create(&reader)) {
        logError("LASReader: laszip_create failed");
        return;
    }
    laszip_BOOL is_compressed = 0;
    if (laszip_open_reader(reader, path.c_str(), &is_compressed)) {
        logError("LASReader: cannot open " + path);
        laszip_destroy(reader);
        return;
    }

    laszip_header* header = nullptr;
    laszip_get_header_pointer(reader, &header);

    pointCount_ = header->extended_number_of_point_records
                      ? header->extended_number_of_point_records
                      : header->number_of_point_records;

    bounds_.min = { header->min_x, header->min_y, header->min_z };
    bounds_.max = { header->max_x, header->max_y, header->max_z };

    // Point data formats that carry RGB colour.
    switch (header->point_data_format) {
        case 2: case 3: case 5: case 7: case 8: case 10: hasColor_ = true; break;
        default: hasColor_ = false; break;
    }

    reader_ = reader;
    ok_ = true;
    logInfo("LASReader: " + path + " (" + std::to_string(pointCount_) +
            " pts, " + (is_compressed ? "LAZ" : "LAS") +
            (hasColor_ ? ", RGB)" : ")"));
}

LASReader::~LASReader() {
    if (reader_) {
        laszip_POINTER r = static_cast<laszip_POINTER>(reader_);
        laszip_close_reader(r);
        laszip_destroy(r);
    }
}

size_t LASReader::read(Point* out, size_t maxPoints) {
    if (!ok_) return 0;
    laszip_POINTER r = static_cast<laszip_POINTER>(reader_);

    laszip_header* header = nullptr;
    laszip_get_header_pointer(r, &header);
    laszip_point* lp = nullptr;
    laszip_get_point_pointer(r, &lp);

    const double sx = header->x_scale_factor, ox = header->x_offset;
    const double sy = header->y_scale_factor, oy = header->y_offset;
    const double sz = header->z_scale_factor, oz = header->z_offset;

    size_t produced = 0;
    while (produced < maxPoints && read_ < pointCount_) {
        if (laszip_read_point(r)) {
            logWarn("LASReader: read_point failed early");
            break;
        }
        Point& p = out[produced++];
        p = Point{};
        p.position = { lp->X * sx + ox, lp->Y * sy + oy, lp->Z * sz + oz };
        p.intensity = lp->intensity;
        p.classification = lp->classification;
        if (hasColor_) {
            p.r = lp->rgb[0]; p.g = lp->rgb[1]; p.b = lp->rgb[2];
            p.hasColor = true;
        }
        ++read_;
    }
    return produced;
}

} // namespace pf

#else // !PF_WITH_LAS

namespace pf {
LASReader::LASReader(const std::string&) {
    logError("LASReader: built without LASzip support (PF_WITH_LAS undefined). "
             "Install the 'laszip' vcpkg port and reconfigure.");
}
LASReader::~LASReader() {}
size_t LASReader::read(Point*, size_t) { return 0; }
} // namespace pf

#endif
