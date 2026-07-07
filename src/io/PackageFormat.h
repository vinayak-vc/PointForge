#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace pf {

// Phase 1: VXPC Core Structures

struct VXPCHeader {
    char     magic[4] = {'V','X','P','C'};
    uint32_t version = 1;
    uint64_t directoryOffset = 0;
    uint64_t directorySize = 0;
    uint32_t entryCount = 0;
    uint32_t packageFlags = 0;
    uint8_t  uuid[16] = {0};
    uint64_t createdTime = 0;
    uint64_t modifiedTime = 0;
    uint32_t converterVersion = 0;
    uint32_t reserved[15] = {0}; // Padding for future use to reach 128 bytes
};

struct VXPCDirectoryEntry {
    char     filename[64] = {0};
    uint64_t offset = 0;
    uint64_t compressedSize = 0;
    uint64_t originalSize = 0;
    uint32_t compression = 0;
    uint32_t crc32 = 0;
    uint32_t flags = 0;
    uint32_t userFlags = 0;
};

// Abstract Stream Interface for VFS
class PackageStream {
public:
    virtual ~PackageStream() = default;
    virtual uint64_t Read(void* buffer, uint64_t size) = 0;
    virtual uint64_t GetSize() const = 0;
    virtual uint64_t GetOffset() const = 0;
    virtual void Seek(uint64_t offset) = 0;
};

// Abstract VFS Interface
class VirtualFileSystem {
public:
    virtual ~VirtualFileSystem() = default;
    virtual bool Open(const std::string& path) = 0;
    virtual bool Contains(const std::string& name) const = 0;
    virtual std::unique_ptr<PackageStream> OpenStream(const std::string& name) const = 0;
    virtual std::vector<uint8_t> Read(const std::string& name) const = 0;
};

class PackageWriter {
public:
    explicit PackageWriter(const std::string& path);
    ~PackageWriter();

    bool isValid() const { return valid_; }

    // Phase 2 Writer APIs
    bool BeginFile(const std::string& filename);
    bool Write(const void* data, size_t size);
    bool EndFile();
    bool Finalize();

private:
    std::string path_;
    void* file_ = nullptr; // FILE*
    bool valid_ = false;

    VXPCDirectoryEntry currentEntry_{};
    bool writingFile_ = false;

    std::vector<VXPCDirectoryEntry> directory_;
    VXPCHeader header_{};
};

class PackageReader : public VirtualFileSystem {
public:
    PackageReader();
    ~PackageReader() override;

    // VFS Implementation
    bool Open(const std::string& path) override;
    bool Contains(const std::string& name) const override;
    std::unique_ptr<PackageStream> OpenStream(const std::string& name) const override;
    std::vector<uint8_t> Read(const std::string& name) const override;

    // Phase 3 Reader APIs
    bool Validate() const;
    VXPCDirectoryEntry GetEntry(const std::string& name) const;
    uint64_t GetOffset(const std::string& name) const;
    uint64_t GetSize(const std::string& name) const;

private:
    std::string path_;
    std::vector<VXPCDirectoryEntry> directory_;
    VXPCHeader header_{};
    bool valid_ = false;
};

} // namespace pf
