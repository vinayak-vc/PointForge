#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pf {

struct PackageEntry {
    char     filename[64];
    uint64_t offset;
    uint64_t size;
    uint64_t originalSize;
    uint32_t compressionType;
    uint32_t flags;
};

class PackageWriter {
public:
    explicit PackageWriter(const std::string& path);
    ~PackageWriter();

    bool isValid() const { return valid_; }

    // Begin writing a new file entry. The previous file must be finished.
    bool BeginFile(const std::string& filename);

    // Write chunk to the currently open file.
    bool Write(const void* data, size_t size);

    // End current file.
    bool EndFile();

    // Writes the directory table and finalizes the package.
    bool Finalize();

private:
    std::string path_;
    void* file_ = nullptr; // FILE* opaque to avoid leaking <cstdio>
    bool valid_ = false;

    PackageEntry currentEntry_{};
    bool writingFile_ = false;

    std::vector<PackageEntry> directory_;
};

class PackageReader {
public:
    PackageReader();
    ~PackageReader();

    bool Open(const std::string& path);
    bool Contains(const std::string& name) const;

    PackageEntry GetEntry(const std::string& name) const;
    
    // Returns offset to the start of the file data inside the package
    uint64_t GetOffset(const std::string& name) const;
    
    // Returns the size of the file data
    uint64_t GetSize(const std::string& name) const;

    // Reads the entire file into a buffer
    std::vector<uint8_t> Read(const std::string& name) const;

private:
    std::string path_;
    std::vector<PackageEntry> directory_;
    bool valid_ = false;
};

} // namespace pf
