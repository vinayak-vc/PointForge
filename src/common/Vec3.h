#pragma once
#include <cmath>
#include <cstdint>

namespace pf {

// Lightweight double-precision 3-vector used throughout the importer. We keep
// our own (rather than leaning on GLM) for the I/O / indexing side because
// (a) it must be double precision for large UTM-style coordinates, and
// (b) it keeps pfcore free of a hard GLM dependency in headers that the
//     readers include. The viewer uses GLM for GPU-facing math.
struct Vec3d {
    double x = 0.0, y = 0.0, z = 0.0;

    Vec3d() = default;
    Vec3d(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3d operator*(double s) const { return {x * s, y * s, z * s}; }

    double operator[](int i) const { return (&x)[i]; }
    double& operator[](int i) { return (&x)[i]; }

    double maxComponent() const { return std::fmax(x, std::fmax(y, z)); }
    double minComponent() const { return std::fmin(x, std::fmin(y, z)); }
};

inline Vec3d minVec(const Vec3d& a, const Vec3d& b) {
    return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
}
inline Vec3d maxVec(const Vec3d& a, const Vec3d& b) {
    return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
}

} // namespace pf
