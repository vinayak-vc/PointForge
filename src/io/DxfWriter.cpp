#include "io/DxfWriter.h"
#include <iomanip>

namespace pf {

bool DxfWriter::begin(const std::string& path, int sliceAxis) {
    sliceAxis_ = sliceAxis;
    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_) {
        error_ = "could not open output file";
        return false;
    }

    out_ << std::fixed << std::setprecision(6);
    out_ << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n";
    out_ << "0\nSECTION\n2\nENTITIES\n";
    return true;
}

void DxfWriter::writeOutline(const AABB& box) {
    if (!out_ || !box.valid()) return;

    Vec3d corners[4];
    if (sliceAxis_ == 0) {
        corners[0] = Vec3d(box.min.x, box.min.y, box.min.z);
        corners[1] = Vec3d(box.min.x, box.max.y, box.min.z);
        corners[2] = Vec3d(box.min.x, box.max.y, box.max.z);
        corners[3] = Vec3d(box.min.x, box.min.y, box.max.z);
    } else if (sliceAxis_ == 1) {
        corners[0] = Vec3d(box.min.x, box.min.y, box.min.z);
        corners[1] = Vec3d(box.max.x, box.min.y, box.min.z);
        corners[2] = Vec3d(box.max.x, box.min.y, box.max.z);
        corners[3] = Vec3d(box.min.x, box.min.y, box.max.z);
    } else {
        corners[0] = Vec3d(box.min.x, box.min.y, box.min.z);
        corners[1] = Vec3d(box.max.x, box.min.y, box.min.z);
        corners[2] = Vec3d(box.max.x, box.max.y, box.min.z);
        corners[3] = Vec3d(box.min.x, box.max.y, box.min.z);
    }

    out_ << "0\nPOLYLINE\n8\nSLICE_BOX\n66\n1\n70\n1\n";
    for (int i = 0; i < 4; ++i) {
        out_ << "0\nVERTEX\n8\nSLICE_BOX\n10\n" << projectedU(corners[i])
             << "\n20\n" << projectedV(corners[i]) << "\n30\n0\n";
    }
    out_ << "0\nSEQEND\n";
}

void DxfWriter::writePoint(const Point& point) {
    if (!out_) return;

    out_ << "0\nPOINT\n8\nPF_CLASS_" << (uint32_t)point.classification
         << "\n62\n" << layerColor(point.classification)
         << "\n10\n" << projectedU(point)
         << "\n20\n" << projectedV(point)
         << "\n30\n0\n";
}

bool DxfWriter::finish() {
    if (!out_) return false;

    out_ << "0\nENDSEC\n0\nEOF\n";
    out_.close();
    if (!out_) {
        error_ = "failed while writing output file";
        return false;
    }
    return true;
}

double DxfWriter::projectedU(const Point& point) const {
    return projectedU(point.position);
}

double DxfWriter::projectedV(const Point& point) const {
    return projectedV(point.position);
}

double DxfWriter::projectedU(const Vec3d& point) const {
    if (sliceAxis_ == 0) return point.y;
    return point.x;
}

double DxfWriter::projectedV(const Vec3d& point) const {
    if (sliceAxis_ == 2) return point.y;
    return point.z;
}

int DxfWriter::layerColor(uint8_t classification) const {
    static const int colors[] = { 7, 1, 3, 4, 5, 2, 6, 8, 9, 30, 40, 50, 60, 70, 140, 200 };
    return colors[classification % (sizeof(colors) / sizeof(colors[0]))];
}

} // namespace pf
