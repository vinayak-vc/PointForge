#include "io/ReaderFactory.h"
#include "io/TextReader.h"
#include "io/PLYReader.h"
#include "io/LASReader.h"
#include "io/E57Reader.h"
#include "common/Log.h"
#include <algorithm>
#include <cctype>

namespace pf {

namespace {
    std::string extOf(const std::string& path) {
        auto dot = path.find_last_of('.');
        if (dot == std::string::npos) return "";
        std::string e = path.substr(dot + 1);
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        return e;
    }

    template <typename T>
    PointReaderPtr make(const std::string& path) {
        auto r = std::make_unique<T>(path);
        if (!r->good()) return nullptr;
        return r;
    }
}

PointReaderPtr openPointReader(const std::string& path) {
    const std::string ext = extOf(path);

    if (ext == "las" || ext == "laz") return make<LASReader>(path);
    if (ext == "e57")                 return make<E57Reader>(path);
    if (ext == "ply")                 return make<PLYReader>(path);
    if (ext == "pts" || ext == "xyz" || ext == "txt" || ext == "csv")
        return make<TextReader>(path);

    logError("openPointReader: unsupported extension '." + ext + "' for " + path);
    return nullptr;
}

} // namespace pf
