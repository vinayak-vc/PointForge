#include "io/PLYReader.h"
#include "common/Log.h"
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <algorithm>

namespace pf {

namespace {
    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        return s;
    }

    // Read a value of arbitrary type from a byte pointer, with endian swap.
    template <typename T>
    T readLE(const unsigned char* p, bool swap) {
        T v;
        std::memcpy(&v, p, sizeof(T));
        if (swap) {
            unsigned char* b = reinterpret_cast<unsigned char*>(&v);
            std::reverse(b, b + sizeof(T));
        }
        return v;
    }
}

PLYReader::Type PLYReader::parseType(const std::string& s) {
    std::string t = lower(s);
    if (t == "char"   || t == "int8")    return Type::I8;
    if (t == "uchar"  || t == "uint8")   return Type::U8;
    if (t == "short"  || t == "int16")   return Type::I16;
    if (t == "ushort" || t == "uint16")  return Type::U16;
    if (t == "int"    || t == "int32")   return Type::I32;
    if (t == "uint"   || t == "uint32")  return Type::U32;
    if (t == "float"  || t == "float32") return Type::F32;
    if (t == "double" || t == "float64") return Type::F64;
    return Type::Invalid;
}

int PLYReader::typeSize(PLYReader::Type t) {
    using T = PLYReader::Type;
    switch (t) {
        case T::I8: case T::U8: return 1;
        case T::I16: case T::U16: return 2;
        case T::I32: case T::U32: case T::F32: return 4;
        case T::F64: return 8;
        default: return 0;
    }
}

PLYReader::PLYReader(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) { logError("PLYReader: cannot open " + path); return; }
    ok_ = parseHeader();
    if (ok_) recordBuf_.resize(static_cast<size_t>(stride_) * 4096);
}

PLYReader::~PLYReader() { if (file_) std::fclose(file_); }

bool PLYReader::parseHeader() {
    char line[512];
    if (!std::fgets(line, sizeof(line), file_)) return false;
    if (std::strncmp(line, "ply", 3) != 0) { logError("PLYReader: not a PLY file"); return false; }

    std::string currentElement;
    uint64_t skipBeforeVertex = 0;   // bytes (binary) of elements preceding vertex
    int      skipStride = 0;
    uint64_t skipCount = 0;
    bool     sawVertex = false;

    while (std::fgets(line, sizeof(line), file_)) {
        std::istringstream ss(line);
        std::string kw; ss >> kw;
        if (kw == "format") {
            std::string fmt; ss >> fmt;
            if (fmt == "ascii") format_ = Format::Ascii;
            else if (fmt == "binary_little_endian") format_ = Format::BinaryLE;
            else if (fmt == "binary_big_endian") format_ = Format::BinaryBE;
        } else if (kw == "element") {
            std::string name; uint64_t count = 0;
            ss >> name >> count;
            currentElement = lower(name);
            if (currentElement == "vertex") { vertexCount_ = count; sawVertex = true; }
            else if (!sawVertex) { skipCount = count; skipStride = 0; } // accumulate to skip later
        } else if (kw == "property") {
            std::string t1; ss >> t1;
            if (lower(t1) == "list") {
                std::string countT, valT, name; ss >> countT >> valT >> name;
                if (currentElement == "vertex") {
                    logError("PLYReader: list properties in vertex element are unsupported");
                    return false;
                }
                // list in a skipped element -> we cannot fixed-skip; only allowed
                // if no such element precedes vertex.
                if (!sawVertex) { logError("PLYReader: element with list before vertex unsupported"); return false; }
            } else {
                // Re-parse the whole line cleanly: "property <type> <name>".
                std::istringstream ssp(line);
                std::string kw2, type2, name2; ssp >> kw2 >> type2 >> name2;
                Prop p;
                p.name = lower(name2);
                p.type = parseType(type2);
                p.size = typeSize(p.type);
                if (currentElement == "vertex") {
                    vprops_.push_back(p);
                } else if (!sawVertex) {
                    skipStride += p.size;
                }
            }
        } else if (kw == "end_header") {
            break;
        }
    }

    if (!sawVertex || vprops_.empty()) { logError("PLYReader: no vertex element/properties"); return false; }

    // Compute stride and resolve property indices.
    stride_ = 0;
    for (size_t i = 0; i < vprops_.size(); ++i) {
        const std::string& n = vprops_[i].name;
        int idx = static_cast<int>(i);
        if      (n == "x") ix_ = idx;
        else if (n == "y") iy_ = idx;
        else if (n == "z") iz_ = idx;
        else if (n == "red"   || n == "r") ir_ = idx;
        else if (n == "green" || n == "g") ig_ = idx;
        else if (n == "blue"  || n == "b") ib_ = idx;
        else if (n == "intensity" || n == "scalar_intensity") iIntensity_ = idx;
        stride_ += vprops_[i].size;
    }
    if (ix_ < 0 || iy_ < 0 || iz_ < 0) { logError("PLYReader: missing x/y/z"); return false; }

    // Skip any whole elements that appeared before vertex (binary only; for
    // ASCII we'd skip lines, but leading non-vertex elements are vanishingly
    // rare so we only support vertex-first ASCII).
    if (skipCount > 0) {
        if (format_ == Format::Ascii) {
            logError("PLYReader: non-vertex element before vertex not supported for ASCII");
            return false;
        }
        std::fseek(file_, static_cast<long>(skipCount * (uint64_t)skipStride), SEEK_CUR);
    }
    (void)skipBeforeVertex;
    return true;
}

size_t PLYReader::read(Point* out, size_t maxPoints) {
    if (!ok_) return 0;
    return (format_ == Format::Ascii) ? readAscii(out, maxPoints)
                                      : readBinary(out, maxPoints);
}

size_t PLYReader::readBinary(Point* out, size_t maxPoints) {
    const bool swap = (format_ == Format::BinaryBE);
    size_t produced = 0;

    while (produced < maxPoints && vertexRead_ < vertexCount_) {
        size_t want = std::min(maxPoints - produced, recordBuf_.size() / (size_t)stride_);
        want = std::min<uint64_t>(want, vertexCount_ - vertexRead_);
        size_t got = std::fread(recordBuf_.data(), stride_, want, file_);
        if (got == 0) break;

        for (size_t v = 0; v < got; ++v) {
            const unsigned char* base = recordBuf_.data() + v * stride_;
            // compute per-property byte offsets on the fly
            auto readProp = [&](int propIndex) -> double {
                int off = 0;
                for (int k = 0; k < propIndex; ++k) off += vprops_[k].size;
                const unsigned char* p = base + off;
                switch (vprops_[propIndex].type) {
                    case Type::I8:  return (double)readLE<int8_t>(p, swap);
                    case Type::U8:  return (double)readLE<uint8_t>(p, swap);
                    case Type::I16: return (double)readLE<int16_t>(p, swap);
                    case Type::U16: return (double)readLE<uint16_t>(p, swap);
                    case Type::I32: return (double)readLE<int32_t>(p, swap);
                    case Type::U32: return (double)readLE<uint32_t>(p, swap);
                    case Type::F32: return (double)readLE<float>(p, swap);
                    case Type::F64: return (double)readLE<double>(p, swap);
                    default: return 0.0;
                }
            };
            Point& pt = out[produced++];
            pt = Point{};
            pt.position = { readProp(ix_), readProp(iy_), readProp(iz_) };
            if (ir_ >= 0 && ig_ >= 0 && ib_ >= 0) {
                // colour stored as 0..255 (uchar) typically -> promote to 16-bit
                bool byteColor = (vprops_[ir_].size == 1);
                double r = readProp(ir_), g = readProp(ig_), b = readProp(ib_);
                if (byteColor) { r *= 256.0; g *= 256.0; b *= 256.0; }
                pt.r = (uint16_t)std::min(65535.0, std::max(0.0, r));
                pt.g = (uint16_t)std::min(65535.0, std::max(0.0, g));
                pt.b = (uint16_t)std::min(65535.0, std::max(0.0, b));
                pt.hasColor = true;
            }
            if (iIntensity_ >= 0) pt.intensity = (uint16_t)readProp(iIntensity_);
        }
        vertexRead_ += got;
        if (got < want) break;
    }
    return produced;
}

size_t PLYReader::readAscii(Point* out, size_t maxPoints) {
    size_t produced = 0;
    char line[1024];
    while (produced < maxPoints && vertexRead_ < vertexCount_ &&
           std::fgets(line, sizeof(line), file_)) {
        std::istringstream ss(line);
        std::vector<double> vals(vprops_.size(), 0.0);
        for (size_t i = 0; i < vprops_.size(); ++i) ss >> vals[i];
        Point& pt = out[produced++];
        pt = Point{};
        pt.position = { vals[ix_], vals[iy_], vals[iz_] };
        if (ir_ >= 0 && ig_ >= 0 && ib_ >= 0) {
            bool byteColor = (vprops_[ir_].size == 1);
            double r = vals[ir_], g = vals[ig_], b = vals[ib_];
            if (byteColor) { r *= 256.0; g *= 256.0; b *= 256.0; }
            pt.r = (uint16_t)std::min(65535.0, std::max(0.0, r));
            pt.g = (uint16_t)std::min(65535.0, std::max(0.0, g));
            pt.b = (uint16_t)std::min(65535.0, std::max(0.0, b));
            pt.hasColor = true;
        }
        if (iIntensity_ >= 0) pt.intensity = (uint16_t)vals[iIntensity_];
        vertexRead_++;
    }
    return produced;
}

} // namespace pf
