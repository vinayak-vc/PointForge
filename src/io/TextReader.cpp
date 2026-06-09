#include "io/TextReader.h"
#include "common/Log.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>

namespace pf {

namespace {
    // Split a line on whitespace and commas into up to `maxTok` double tokens.
    int tokenize(const char* line, double* vals, int maxTok) {
        int n = 0;
        const char* p = line;
        while (*p && n < maxTok) {
            while (*p && (std::isspace((unsigned char)*p) || *p == ',')) ++p;
            if (!*p) break;
            char* end = nullptr;
            double v = std::strtod(p, &end);
            if (end == p) break; // not a number
            vals[n++] = v;
            p = end;
        }
        return n;
    }

    bool isCommentOrBlank(const char* line) {
        const char* p = line;
        while (*p && std::isspace((unsigned char)*p)) ++p;
        if (*p == '\0') return true;
        if (*p == '#') return true;
        if (p[0] == '/' && p[1] == '/') return true;
        return false;
    }
}

TextReader::TextReader(const std::string& path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        logError("TextReader: cannot open " + path);
        return;
    }
    // Peek the first non-comment line. If it is a single integer, treat it as a
    // PTS point-count header and consume it. Otherwise rewind so the data line
    // is read normally.
    long startPos = std::ftell(file_);
    while (std::fgets(lineBuf_, sizeof(lineBuf_), file_)) {
        if (isCommentOrBlank(lineBuf_)) { startPos = std::ftell(file_); continue; }
        double vals[8];
        int n = tokenize(lineBuf_, vals, 8);
        if (n == 1) {
            declaredCount_ = static_cast<uint64_t>(vals[0]);
            // next data line begins here
        } else {
            // not a count header; put it back
            std::fseek(file_, startPos, SEEK_SET);
        }
        break;
    }
}

TextReader::~TextReader() {
    if (file_) std::fclose(file_);
}

void TextReader::detectColumns(const char* line) {
    double vals[8];
    int n = tokenize(line, vals, 8);
    columns_ = n;
    if (n == 6 || n == 7) {
        // Heuristic: if any colour-looking value exceeds 1.0 we treat colour as
        // 0..255 byte range; values in [0,1] are treated as normalised floats.
        int cstart = (n == 7) ? 4 : 3;
        bool anyAboveOne = false;
        for (int i = cstart; i < n; ++i) if (vals[i] > 1.5) anyAboveOne = true;
        colorIsByte_ = anyAboveOne;
    }
    logInfo("TextReader: detected " + std::to_string(n) + " columns per line");
}

bool TextReader::parseLine(const char* line, Point& p) const {
    double vals[8];
    int n = tokenize(line, vals, 8);
    if (n < 3) return false;

    p = Point{};
    p.position = { vals[0], vals[1], vals[2] };

    auto setColor = [&](int i0) {
        double rr = vals[i0], gg = vals[i0 + 1], bb = vals[i0 + 2];
        if (colorIsByte_) { rr *= 256.0; gg *= 256.0; bb *= 256.0; }       // 0..255 -> 0..65535
        else              { rr *= 65535.0; gg *= 65535.0; bb *= 65535.0; } // 0..1   -> 0..65535
        auto clamp16 = [](double v) -> uint16_t {
            if (v < 0) v = 0; if (v > 65535) v = 65535; return (uint16_t)(v);
        };
        p.r = clamp16(rr); p.g = clamp16(gg); p.b = clamp16(bb);
        p.hasColor = true;
    };

    switch (columns_) {
        case 3: break;
        case 4: p.intensity = (uint16_t)vals[3]; break;
        case 6: setColor(3); break;
        case 7: p.intensity = (uint16_t)vals[3]; setColor(4); break;
        default:
            // Unknown wider layout: take xyz, and if >=6 cols assume last 3 are colour.
            if (n >= 6) setColor(n - 3);
            break;
    }
    return true;
}

size_t TextReader::read(Point* out, size_t maxPoints) {
    if (!file_) return 0;
    size_t count = 0;
    while (count < maxPoints && std::fgets(lineBuf_, sizeof(lineBuf_), file_)) {
        if (isCommentOrBlank(lineBuf_)) continue;
        if (columns_ == 0) detectColumns(lineBuf_);
        if (parseLine(lineBuf_, out[count])) ++count;
    }
    return count;
}

} // namespace pf
