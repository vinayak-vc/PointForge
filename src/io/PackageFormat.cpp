#include "io/PackageFormat.h"
#include "common/Log.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace pf {

static_assert(sizeof(VXPCHeader) == 128, "VXPCHeader size mismatch");
static_assert(sizeof(VXPCDirectoryEntry) == 104, "VXPCDirectoryEntry size mismatch");

// --- PackageStream Implementation (Phase 1) ---
class FileReaderStream : public PackageStream {
public:
    FileReaderStream(const std::string& path, uint64_t offset, uint64_t size) 
        : size_(size), baseOffset_(offset) {
        f_ = std::fopen(path.c_str(), "rb");
        if (f_) {
#ifdef _WIN32
            _fseeki64(f_, baseOffset_, SEEK_SET);
#else
            fseeko(f_, baseOffset_, SEEK_SET);
#endif
        }
    }
    
    ~FileReaderStream() override {
        if (f_) std::fclose(f_);
    }

    uint64_t Read(void* buffer, uint64_t size) override {
        if (!f_) return 0;
        
        // Prevent reading past the entry's size
        uint64_t maxRead = size_ - currentOffset_;
        uint64_t toRead = (size > maxRead) ? maxRead : size;
        if (toRead == 0) return 0;

        size_t readCount = std::fread(buffer, 1, (size_t)toRead, f_);
        currentOffset_ += readCount;
        return readCount;
    }

    uint64_t GetSize() const override { return size_; }
    uint64_t GetOffset() const override { return currentOffset_; }
    
    void Seek(uint64_t offset) override {
        if (!f_) return;
        currentOffset_ = (offset > size_) ? size_ : offset;
#ifdef _WIN32
        _fseeki64(f_, baseOffset_ + currentOffset_, SEEK_SET);
#else
        fseeko(f_, baseOffset_ + currentOffset_, SEEK_SET);
#endif
    }

private:
    FILE* f_ = nullptr;
    uint64_t size_ = 0;
    uint64_t baseOffset_ = 0;
    uint64_t currentOffset_ = 0;
};

// --- PackageWriter ---

PackageWriter::PackageWriter() {}

PackageWriter::~PackageWriter() {
    if (file_) {
        std::fclose((FILE*)file_);
        file_ = nullptr;
    }
}

bool PackageWriter::Create(const std::string& path) {
    if (valid_) return false;
    path_ = path;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        logError("PackageWriter: cannot open file " + path);
        return false;
    }

    std::memset(&header_, 0, sizeof(header_));
    std::memcpy(header_.magic, "VXPC", 4);
    header_.version = 1;

    file_ = f;
    valid_ = true;

    return WriteHeader();
}

bool PackageWriter::WriteHeader() {
    if (!valid_ || !file_) return false;
    FILE* f = (FILE*)file_;
    
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_SET);
#else
    fseeko(f, 0, SEEK_SET);
#endif

    if (std::fwrite(&header_, sizeof(header_), 1, f) != 1) {
        logError("PackageWriter: failed to write header");
        return false;
    }
    return true;
}

bool PackageWriter::WriteDirectory() {
    if (!valid_ || !file_) return false;
    FILE* f = (FILE*)file_;
    
#ifdef _WIN32
    uint64_t directoryOffset = (uint64_t)_ftelli64(f);
#else
    uint64_t directoryOffset = (uint64_t)ftello(f);
#endif
    uint64_t directorySize = directory_.size() * sizeof(VXPCDirectoryEntry);
    
    if (directorySize > 0) {
        if (std::fwrite(directory_.data(), sizeof(VXPCDirectoryEntry), directory_.size(), f) != directory_.size()) {
            logError("PackageWriter: failed to write directory table");
            return false;
        }
    }

    header_.directoryOffset = directoryOffset;
    header_.directorySize = directorySize;
    header_.entryCount = (uint32_t)directory_.size();

    return WriteHeader();
}

bool PackageWriter::BeginFile(const std::string& filename) {
    if (!valid_ || writingFile_) return false;

    FILE* f = (FILE*)file_;
    
    std::memset(&currentEntry_, 0, sizeof(currentEntry_));
    std::strncpy(currentEntry_.filename, filename.c_str(), sizeof(currentEntry_.filename) - 1);
    currentEntry_.filename[sizeof(currentEntry_.filename) - 1] = '\0';
    
#ifdef _WIN32
    // Move to end of file to append
    _fseeki64(f, 0, SEEK_END);
    currentEntry_.offset = (uint64_t)_ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    currentEntry_.offset = (uint64_t)ftello(f);
#endif

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

    currentEntry_.originalSize += size;
    currentEntry_.compressedSize += size; // Phase 2: No compression yet
    return true;
}

bool PackageWriter::EndFile() {
    if (!valid_ || !writingFile_) return false;

    directory_.push_back(currentEntry_);
    writingFile_ = false;
    return true;
}

bool PackageWriter::AddFile(const std::string& filename, const std::string& sourcePath) {
    if (!valid_ || writingFile_) return false;

    FILE* in = std::fopen(sourcePath.c_str(), "rb");
    if (!in) {
        logError("PackageWriter: failed to read source file " + sourcePath);
        return false;
    }

    if (!BeginFile(filename)) {
        std::fclose(in);
        return false;
    }

    char buffer[8192];
    size_t bytesRead;
    while ((bytesRead = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (!Write(buffer, bytesRead)) {
            std::fclose(in);
            EndFile();
            return false;
        }
    }

    std::fclose(in);
    return EndFile();
}

bool PackageWriter::AddMemory(const std::string& filename, const void* data, size_t size) {
    if (!BeginFile(filename)) return false;
    if (!Write(data, size)) {
        EndFile();
        return false;
    }
    return EndFile();
}

bool PackageWriter::Finalize() {
    if (!valid_) return false;
    if (writingFile_) EndFile();

    if (!WriteDirectory()) return false;

    FILE* f = (FILE*)file_;
    std::fclose(f);
    file_ = nullptr;
    valid_ = false; // Finalized

    return true;
}

// --- PackageReader ---

PackageReader::PackageReader() {}
PackageReader::~PackageReader() {}

bool PackageReader::Open(const std::string& path) {
    path_ = path;
    
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    if (std::fread(&header_, sizeof(header_), 1, f) != 1) {
        std::fclose(f);
        return false;
    }

    if (std::memcmp(header_.magic, "VXPC", 4) != 0 || header_.version != 1) {
        std::fclose(f);
        return false;
    }

    if (header_.entryCount > 0) {
        directory_.resize(header_.entryCount);
        
#ifdef _WIN32
        _fseeki64(f, header_.directoryOffset, SEEK_SET);
#else
        fseeko(f, header_.directoryOffset, SEEK_SET);
#endif
        if (std::fread(directory_.data(), sizeof(VXPCDirectoryEntry), header_.entryCount, f) != header_.entryCount) {
            std::fclose(f);
            directory_.clear();
            return false;
        }
    }

    std::fclose(f);
    valid_ = true;
    return true;
}

bool PackageReader::Validate() const {
    if (!valid_) return false;
    if (std::memcmp(header_.magic, "VXPC", 4) != 0) return false;
    if (header_.version != 1) return false;
    return true;
}

bool PackageReader::Contains(const std::string& name) const {
    if (!valid_) return false;
    for (const auto& entry : directory_) {
        if (std::strcmp(entry.filename, name.c_str()) == 0) return true;
    }
    return false;
}

VXPCDirectoryEntry PackageReader::GetEntry(const std::string& name) const {
    if (valid_) {
        for (const auto& entry : directory_) {
            if (std::strcmp(entry.filename, name.c_str()) == 0) return entry;
        }
    }
    return VXPCDirectoryEntry{};
}

uint64_t PackageReader::GetOffset(const std::string& name) const {
    return GetEntry(name).offset;
}

uint64_t PackageReader::GetSize(const std::string& name) const {
    return GetEntry(name).originalSize;
}

std::unique_ptr<PackageStream> PackageReader::OpenStream(const std::string& name) const {
    if (!valid_ || !Contains(name)) return nullptr;
    VXPCDirectoryEntry entry = GetEntry(name);
    return std::make_unique<FileReaderStream>(path_, entry.offset, entry.originalSize);
}

std::vector<uint8_t> PackageReader::Read(const std::string& name) const {
    std::vector<uint8_t> buffer;
    if (!valid_) return buffer;

    VXPCDirectoryEntry entry = GetEntry(name);
    if (entry.originalSize == 0 && entry.offset == 0 && std::strcmp(entry.filename, name.c_str()) != 0) {
        return buffer;
    }

    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return buffer;

#ifdef _WIN32
    _fseeki64(f, entry.offset, SEEK_SET);
#else
    fseeko(f, entry.offset, SEEK_SET);
#endif
    buffer.resize(entry.originalSize);

    if (entry.originalSize > 0) {
        if (std::fread(buffer.data(), 1, entry.originalSize, f) != entry.originalSize) {
            buffer.clear();
        }
    }

    std::fclose(f);
    return buffer;
}

} // namespace pf
