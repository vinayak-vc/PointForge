#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace pf {

// One camera-path keyframe: a bookmark pose (centred space, like CamBookmark)
// plus a time on the path's timeline. Persisted per-cloud in campaths.txt
// (TSV, same AppData pattern as bookmarks.txt — see main.cpp).
struct CamKey {
    double t = 0.0;          // seconds on the timeline
    float  px = 0, py = 0, pz = 0;
    float  yaw = 0, pitch = 0;
    int    ortho = 0;        // discrete: not interpolated, previous key wins
    float  orthoSize = 100.0f;
};

// Keyframed camera path with smooth interpolation:
//   position  — non-uniform Catmull-Rom (finite-difference tangents), so the
//               curve passes through every key without overshoot artifacts at
//               unevenly spaced times
//   yaw       — unwrapped to the shortest way between consecutive keys, then
//               interpolated with the same spline (no 350°->10° long spin)
//   pitch     — same spline (already continuous, clamp at sample time)
//   orthoSize — same spline; ortho flag is a step function (previous key)
// Keys are kept sorted by t; sample() clamps outside [first.t, last.t].
struct CamPath {
    std::vector<CamKey> keys;

    double duration() const { return keys.empty() ? 0.0 : keys.back().t; }

    void sortKeys() {
        std::stable_sort(keys.begin(), keys.end(),
                         [](const CamKey& a, const CamKey& b) { return a.t < b.t; });
    }

    bool sample(double t, CamKey& out) const {
        if (keys.empty()) return false;
        if (keys.size() == 1 || t <= keys.front().t) { out = keys.front(); out.t = t; return true; }
        if (t >= keys.back().t) { out = keys.back(); out.t = t; return true; }

        // Segment [k1, k2] containing t.
        size_t k2 = 1;
        while (k2 < keys.size() - 1 && keys[k2].t <= t) ++k2;
        if (keys[k2].t <= t) { out = keys.back(); out.t = t; return true; }
        const size_t k1 = k2 - 1;
        const size_t k0 = (k1 == 0) ? k1 : k1 - 1;
        const size_t k3 = std::min(k2 + 1, keys.size() - 1);

        // Yaw must interpolate the short way round: unwrap the four control
        // yaws into one continuous sequence anchored on k1.
        float y0 = keys[k0].yaw, y1 = keys[k1].yaw, y2 = keys[k2].yaw, y3 = keys[k3].yaw;
        auto unwrapTo = [](float ref, float a) {
            while (a - ref > 180.0f)  a -= 360.0f;
            while (a - ref < -180.0f) a += 360.0f;
            return a;
        };
        y0 = unwrapTo(y1, y0);
        y2 = unwrapTo(y1, y2);
        y3 = unwrapTo(y2, y3);

        const double t0 = keys[k0].t, t1 = keys[k1].t, t2 = keys[k2].t, t3 = keys[k3].t;
        const double h = t2 - t1;
        const double s = (h > 1e-9) ? (t - t1) / h : 0.0;

        // Hermite basis.
        const double s2 = s * s, s3 = s2 * s;
        const double h00 = 2 * s3 - 3 * s2 + 1;
        const double h10 = s3 - 2 * s2 + s;
        const double h01 = -2 * s3 + 3 * s2;
        const double h11 = s3 - s2;

        // Finite-difference tangent of channel value v at k1/k2. Duplicate-time
        // neighbours (t span ~0) degrade to a zero tangent rather than dividing
        // by zero.
        auto herm = [&](double v0, double v1, double v2, double v3) -> double {
            const double d10 = t2 - t0, d21 = t3 - t1;
            const double m1 = (d10 > 1e-9) ? (v2 - v0) / d10 : 0.0;
            const double m2 = (d21 > 1e-9) ? (v3 - v1) / d21 : 0.0;
            return h00 * v1 + h10 * h * m1 + h01 * v2 + h11 * h * m2;
        };

        out.t = t;
        out.px = (float)herm(keys[k0].px, keys[k1].px, keys[k2].px, keys[k3].px);
        out.py = (float)herm(keys[k0].py, keys[k1].py, keys[k2].py, keys[k3].py);
        out.pz = (float)herm(keys[k0].pz, keys[k1].pz, keys[k2].pz, keys[k3].pz);
        out.yaw = (float)herm(y0, y1, y2, y3);
        out.pitch = std::clamp((float)herm(keys[k0].pitch, keys[k1].pitch,
                                           keys[k2].pitch, keys[k3].pitch),
                               -89.0f, 89.0f);
        out.orthoSize = std::max(0.01f, (float)herm(keys[k0].orthoSize, keys[k1].orthoSize,
                                                    keys[k2].orthoSize, keys[k3].orthoSize));
        out.ortho = keys[k1].ortho;   // discrete channel: hold until the next key
        return true;
    }
};

} // namespace pf
