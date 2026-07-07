#include "io/PackageFormat.h"
#include "common/Log.h"

#include <cstdio>
#include <cstring>

namespace pf {

struct PackageHeader {
    char     magic[4];       // "VXPC"
    uint32_t version;
    uint64_t directoryOffset;
    uint64_t directorySize;
    uint32_t entryCount;
    uint8_t  reserved[36];   // Pad to 64 bytes
};

static_assert(sizeof(PackageHeader) == 64, "PackageHeader size mismatch");
static_assert(sizeof(PackageEntry) == 96, "PackageEntry size mismatch");

PackageWriter::PackageWriter(const std::string& path) : path_(path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        logError("PackageWriter: cannot open file " + path);
        return;
    }

    PackageHeader header{};
    std::memcpy(header.magic, "VXPC", 4);
    header.version = 1;
    header.directoryOffset = 0;
    header.directorySize = 0;
    header.entryCount = 0;

    if (std::fwrite(&header, sizeof(header), 1, f) != 1) {
        logError("PackageWriter: failed to write header to " + path);
        std::fclose(f);
        return;
    }

    file_ = f;
    valid_ = true;
}

PackageWriter::~PackageWriter() {
    if (file_) {
        std::fclose((FILE*)file_);
        file_ = nullptr;
    }
}

bool PackageWriter::BeginFile(const std::string& filename) {
    if (!valid_ || writingFile_) return false;

    FILE* f = (FILE*)file_;
    
    std::memset(&currentEntry_, 0, sizeof(currentEntry_));
    std::strncpy(currentEntry_.filename, filename.c_str(), sizeof(currentEntry_.filename) - 1);
    
    // Ensure null termination
    currentEntry_.filename[sizeof(currentEntry_.filename) - 1] = '\0';
    
#ifdef _WIN32
    currentEntry_.offset = (uint64_t)_ftelli64(f);
#else
    currentEntry_.offset = (uint64_t)ftello(f);
#endif
    currentEntry_.size = 0;
    currentEntry_.originalSize = 0;
    currentEntry_.compressionType = 0;
    currentEntry_.flags = 0;

    writingFile_ = true;
    return true;
}

bool PackageWriter::Write(const void* data, size_t size) {
    if (!valid_ || !writingFile_) return false;
    if (size == 0) return true;

    FILE* f = (FILE*)file_;
    if (std::fwrite(data, 1, size, f) != size) {
        logError("PackageWriter: failed to write chunk");
        return false;
    }

    currentEntry_.size += size;
    currentEntry_.originalSize += size;
    return true;
}

bool PackageWriter::EndFile() {
    if (!valid_ || !writingFile_) return false;

    directory_.push_back(currentEntry_);
    writingFile_ = false;
    return true;
}

bool PackageWriter::Finalize() {
    if (!valid_) return false;
    if (writingFile_) EndFile();

    FILE* f = (FILE*)file_;
    
#ifdef _WIN32
    uint64_t directoryOffset = (uint64_t)_ftelli64(f);
#else
    uint64_t directoryOffset = (uint64_t)ftello(f);
#endif
    uint64_t directorySize = directory_.size() * sizeof(PackageEntry);
    
    if (directorySize > 0) {
        if (std::fwrite(directory_.data(), sizeof(PackageEntry), directory_.size(), f) != directory_.size()) {
            logError("PackageWriter: failed to write directory table");
            return false;
        }
    }

    std::fseek(f, 0, SEEK_SET);

    PackageHeader header{};
    std::memcpy(header.magic, "VXPC", 4);
    header.version = 1;
    header.directoryOffset = directoryOffset;
    header.directorySize = directorySize;
    header.entryCount = (uint32_t)directory_.size();

    if (std::fwrite(&header, sizeof(header), 1, f) != 1) {
        logError("PackageWriter: failed to update header");
        return false;
    }

    std::fclose(f);
    file_ = nullptr;
    valid_ = false; // Finalized

    return true;
}

PackageReader::PackageReader() {}

PackageReader::~PackageReader() {}

bool PackageReader::Open(const std::string& path) {
    path_ = path;
    
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    PackageHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    if (std::memcmp(header.magic, "VXPC", 4) != 0 || header.version != 1) {
        std::fclose(f);
        return false;
    }

    if (header.entryCount > 0) {
        directory_.resize(header.entryCount);
        
#ifdef _WIN32
        _fseeki64(f, header.directoryOffset, SEEK_SET);
#else
        fseeko(f, header.directoryOffset, SEEK_SET);
#endif
        if (std::fread(directory_.data(), sizeof(PackageEntry), header.entryCount, f) != header.entryCount) {
            std::fclose(f);
            directory_.clear();
            return false;
        }
    }

    std::fclose(f);
    valid_ = true;
    return true;
}

bool PackageReader::Contains(const std::string& name) const {
    if (!valid_) return false;
    for (const auto& entry : directory_) {
        if (std::strcmp(entry.filename, name.c_str()) == 0) return true;
    }
    return false;
}

PackageEntry PackageReader::GetEntry(const std::string& name) const {
    if (valid_) {
        for (const auto& entry : directory_) {
            if (std::strcmp(entry.filename, name.c_str()) == 0) return entry;
        }
    }
    return PackageEntry{};
}

uint64_t PackageReader::GetOffset(const std::string& name) const {
    return GetEntry(name).offset;
}

uint64_t PackageReader::GetSize(const std::string& name) const {
    return GetEntry(name).size;
}

std::vector<uint8_t> PackageReader::Read(const std::string& name) const {
    std::vector<uint8_t> buffer;
    if (!valid_) return buffer;

    PackageEntry entry = GetEntry(name);
    if (entry.size == 0 && entry.offset == 0 && std::strcmp(entry.filename, name.c_str()) != 0) {
        // Not found
        return buffer;
    }

    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return buffer;

#ifdef _WIN32
    _fseeki64(f, entry.offset, SEEK_SET);
#else
    fseeko(f, entry.offset, SEEK_SET);
#endif
    buffer.resize(entry.size);

    if (entry.size > 0) {
        if (std::fread(buffer.data(), 1, entry.size, f) != entry.size) {
            buffer.clear();
        }
    }

    std::fclose(f);
    return buffer;
}

} // namespace pf
