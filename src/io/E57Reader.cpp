#include "io/E57Reader.h"
#include "common/Log.h"

#ifdef PF_WITH_E57
// libE57Format "Simple API". Header names are stable across 2.x/3.x; the point
// buffer struct (Data3DPointsData_d) gained/renamed a few fields over versions,
// so if your installed version errors here, adjust the buffer setup below.
#include <E57SimpleReader.h>
#include <vector>
#include <cmath>

namespace pf {

namespace {
    constexpr unsigned kBlock = 1u << 16; // 65536 points per read() from the codec
}

struct E57Reader::Impl {
    std::unique_ptr<e57::Reader> reader;
    int    scanCount = 0;
    int    scanIndex = -1;            // current scan
    bool   ok = false;
    uint64_t totalPoints = 0;

    // per-scan streaming state
    std::unique_ptr<e57::CompressedVectorReader> vr;
    e57::Data3DPointsData_d buffers;  // double-precision point buffers
    std::vector<double>   x, y, z, intensity;
    std::vector<uint16_t> r, g, b;
    std::vector<int8_t>   isColorInvalid, isIntensityInvalid;
    unsigned long inBuffer = 0;       // valid points currently in buffers
    unsigned long cursor   = 0;       // next index to emit from buffers

    // pose for the active scan
    bool   hasColor = false;
    bool   hasIntensity = false;
    double R[9] = {1,0,0, 0,1,0, 0,0,1};
    double T[3] = {0,0,0};

    bool openScan(int idx);
    bool advanceScan();
};

static void quatToMatrix(double w, double x, double y, double z, double R[9]) {
    double n = std::sqrt(w*w + x*x + y*y + z*z);
    if (n < 1e-12) { R[0]=R[4]=R[8]=1; R[1]=R[2]=R[3]=R[5]=R[6]=R[7]=0; return; }
    w/=n; x/=n; y/=n; z/=n;
    R[0]=1-2*(y*y+z*z); R[1]=2*(x*y-z*w);   R[2]=2*(x*z+y*w);
    R[3]=2*(x*y+z*w);   R[4]=1-2*(x*x+z*z); R[5]=2*(y*z-x*w);
    R[6]=2*(x*z-y*w);   R[7]=2*(y*z+x*w);   R[8]=1-2*(x*x+y*y);
}

bool E57Reader::Impl::openScan(int idx) {
    e57::Data3D header;
    reader->ReadData3D(idx, header);

    const unsigned long n = static_cast<unsigned long>(header.pointCount);
    const unsigned cap = (n < kBlock && n > 0) ? (unsigned)n : kBlock;

    x.assign(cap, 0.0); y.assign(cap, 0.0); z.assign(cap, 0.0);
    intensity.assign(cap, 0.0);
    r.assign(cap, 0); g.assign(cap, 0); b.assign(cap, 0);
    isColorInvalid.assign(cap, 0); isIntensityInvalid.assign(cap, 0);

    buffers = e57::Data3DPointsData_d();
    buffers.cartesianX = x.data();
    buffers.cartesianY = y.data();
    buffers.cartesianZ = z.data();

    hasColor     = header.pointFields.colorRedField;
    hasIntensity = header.pointFields.intensityField;
    if (hasColor) {
        buffers.colorRed   = r.data();
        buffers.colorGreen = g.data();
        buffers.colorBlue  = b.data();
    }
    if (hasIntensity) buffers.intensity = intensity.data();

    // Pose (scan-local -> global). Defaults to identity if absent.
    quatToMatrix(header.pose.rotation.w, header.pose.rotation.x,
                 header.pose.rotation.y, header.pose.rotation.z, R);
    T[0] = header.pose.translation.x;
    T[1] = header.pose.translation.y;
    T[2] = header.pose.translation.z;

    vr = std::make_unique<e57::CompressedVectorReader>(
            reader->SetUpData3DPointsData(idx, cap, buffers));
    inBuffer = 0;
    cursor   = 0;
    scanIndex = idx;
    return true;
}

bool E57Reader::Impl::advanceScan() {
    if (vr) { vr->close(); vr.reset(); }
    int next = scanIndex + 1;
    if (next >= scanCount) return false;
    return openScan(next);
}

E57Reader::E57Reader(const std::string& path) : impl_(std::make_unique<Impl>()) {
    try {
        impl_->reader = std::make_unique<e57::Reader>(path, e57::ReaderOptions{});
        impl_->scanCount = impl_->reader->GetData3DCount();
        if (impl_->scanCount <= 0) { logError("E57Reader: no scans in " + path); return; }

        // total point count across scans (for progress)
        for (int i = 0; i < impl_->scanCount; ++i) {
            e57::Data3D h; impl_->reader->ReadData3D(i, h);
            impl_->totalPoints += static_cast<uint64_t>(h.pointCount);
        }
        impl_->ok = impl_->openScan(0);
        logInfo("E57Reader: " + path + " (" + std::to_string(impl_->scanCount) +
                " scans, " + std::to_string(impl_->totalPoints) + " pts)");
    } catch (const std::exception& e) {
        logError(std::string("E57Reader: ") + e.what());
        impl_->ok = false;
    }
}

E57Reader::~E57Reader() {
    if (impl_ && impl_->vr) impl_->vr->close();
}

bool E57Reader::good() const { return impl_ && impl_->ok; }
uint64_t E57Reader::pointCount() const { return impl_ ? impl_->totalPoints : 0; }

size_t E57Reader::read(Point* out, size_t maxPoints) {
    if (!good()) return 0;
    Impl& s = *impl_;
    size_t produced = 0;

    while (produced < maxPoints) {
        if (s.cursor >= s.inBuffer) {
            // need a fresh block
            s.inBuffer = s.vr ? s.vr->read() : 0;
            s.cursor = 0;
            if (s.inBuffer == 0) {
                if (!s.advanceScan()) break;     // no more scans
                continue;                         // try reading from new scan
            }
        }
        // emit buffered points, applying pose
        while (produced < maxPoints && s.cursor < s.inBuffer) {
            const unsigned long i = s.cursor++;
            double lx = s.x[i], ly = s.y[i], lz = s.z[i];
            Point& p = out[produced++];
            p = Point{};
            p.position = {
                s.R[0]*lx + s.R[1]*ly + s.R[2]*lz + s.T[0],
                s.R[3]*lx + s.R[4]*ly + s.R[5]*lz + s.T[1],
                s.R[6]*lx + s.R[7]*ly + s.R[8]*lz + s.T[2]
            };
            if (s.hasColor) { p.r = s.r[i]; p.g = s.g[i]; p.b = s.b[i]; p.hasColor = true; }
            if (s.hasIntensity) {
                double v = s.intensity[i];
                if (v <= 1.0) v *= 65535.0;      // normalised -> 16-bit
                if (v < 0) v = 0; if (v > 65535) v = 65535;
                p.intensity = (uint16_t)v;
            }
        }
    }
    return produced;
}

} // namespace pf

#else // !PF_WITH_E57

namespace pf {
struct E57Reader::Impl {};
E57Reader::E57Reader(const std::string&) {
    logError("E57Reader: built without libE57Format (PF_WITH_E57 undefined). "
             "Install the 'libe57format' vcpkg port and reconfigure.");
}
E57Reader::~E57Reader() {}
bool E57Reader::good() const { return false; }
uint64_t E57Reader::pointCount() const { return 0; }
size_t E57Reader::read(Point*, size_t) { return 0; }
} // namespace pf

#endif
