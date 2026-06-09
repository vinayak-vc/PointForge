#pragma once
#include "io/PointReader.h"
#include <cstdio>
#include <string>
#include <vector>

namespace pf {

// Streaming PLY reader (binary little/big endian and ASCII). It parses the
// header, locates the vertex element's x/y/z and optional colour/intensity
// properties, then reads vertex records incrementally so even huge PLY files
// stay out-of-core. (List properties inside the vertex element are not
// supported; faces and other trailing elements are simply ignored.)
class PLYReader : public PointReader {
public:
    explicit PLYReader(const std::string& path);
    ~PLYReader() override;

    bool good() const override { return ok_; }
    uint64_t pointCount() const override { return vertexCount_; }
    size_t read(Point* out, size_t maxPoints) override;

private:
    enum class Format { Ascii, BinaryLE, BinaryBE };
    enum class Type { I8, U8, I16, U16, I32, U32, F32, F64, Invalid };

    struct Prop {
        std::string name;
        Type type = Type::Invalid;
        int  size = 0;
    };

    bool parseHeader();
    size_t readBinary(Point* out, size_t maxPoints);
    size_t readAscii(Point* out, size_t maxPoints);
    static Type parseType(const std::string& s);
    static int  typeSize(Type t);

    FILE*  file_ = nullptr;
    bool   ok_ = false;
    Format format_ = Format::Ascii;

    std::vector<Prop> vprops_;   // vertex properties, in file order
    int    stride_ = 0;          // bytes per vertex (binary)
    // resolved property indices (-1 if absent)
    int ix_=-1, iy_=-1, iz_=-1, ir_=-1, ig_=-1, ib_=-1, iIntensity_=-1;

    uint64_t vertexCount_ = 0;
    uint64_t vertexRead_  = 0;
    std::vector<unsigned char> recordBuf_;
};

} // namespace pf
