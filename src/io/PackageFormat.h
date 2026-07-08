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
    PackageWriter();
    ~PackageWriter();

    // Phase 2 Writer APIs
    bool Create(const std::string& path);
    bool isValid() const { return valid_; }

    // Low level API (used internally or for streaming large files chunk-by-chunk)
    bool BeginFile(const std::string& filename);
    bool Write(const void* data, size_t size);
    bool EndFile();

    // High level Add APIs
    enum class Compression { None = 0, ZSTD = 1 };
    bool AddFile(const std::string& filename, const std::string& sourcePath, Compression comp = Compression::None);
    bool AddMemory(const std::string& filename, const void* data, size_t size, Compression comp = Compression::None);
    void AddCustomMeta(const std::string& key, const std::string& value);

    // Phase 11: 3rd-party/plugin blobs. Stored under the reserved "plugins/"
    // namespace (e.g. AddPluginData("acme/state.bin", ...) -> "plugins/acme/
    // state.bin"). The core PointForge loader only reads a fixed set of names
    // (meta.bin/hierarchy.bin/octree.bin/...) so it ignores everything here.
    // relPath must not itself start with "plugins/" (it is prepended) and the
    // final name must fit VXPCDirectoryEntry::filename (63 chars + NUL).
    bool AddPluginData(const std::string& relPath, const void* data, size_t size,
                       Compression comp = Compression::None);

    // Phase 7 (repack support): copy the identity fields (uuid, created time,
    // converter version) of an existing package's header into this writer so a
    // repack preserves them. Call after Create(), before adding entries.
    void InheritHeader(const VXPCHeader& src);

    // Phase 7 (repack support): append pre-formed STORED bytes verbatim (already
    // compressed if templ.compression != 0), pushing a directory entry equal to
    // `templ` but with a fresh offset. Used to copy an existing entry during a
    // repack WITHOUT decompress/recompress (so octree.bin etc. are untouched).
    bool AddRawEntry(const VXPCDirectoryEntry& templ, const void* storedBytes, size_t storedSize);

    bool Finalize();

private:
    bool WriteHeader();
    bool WriteDirectory();

    std::string path_;
    void* file_ = nullptr; // FILE*
    bool valid_ = false;

    VXPCDirectoryEntry currentEntry_{};
    bool writingFile_ = false;

    std::vector<VXPCDirectoryEntry> directory_;
    VXPCHeader header_{};
    std::vector<std::pair<std::string, std::string>> customMetadata_;
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

    // Phase 7 (repack support): access the raw header and the STORED bytes of
    // an entry (compressed if the entry is compressed), without decompressing
    // or CRC-checking. Fills outEntry. Used by RepackPackage to copy entries
    // verbatim.
    const VXPCHeader& GetHeader() const { return header_; }
    std::vector<uint8_t> ReadRaw(const std::string& name, VXPCDirectoryEntry& outEntry) const;

    // Phase 11: enumerate directory entries. With an empty prefix returns
    // every filename; otherwise only names starting with `prefix`. Order
    // matches the on-disk directory table.
    std::vector<std::string> ListEntries(const std::string& prefix = "") const;
    // Convenience: entries under the reserved "plugins/" namespace.
    std::vector<std::string> ListPlugins() const { return ListEntries("plugins/"); }

private:
    std::string path_;
    std::vector<VXPCDirectoryEntry> directory_;
    VXPCHeader header_{};
    bool valid_ = false;
};

// Phase 7 (repack support): rewrite an existing `.vxpc` applying `upserts`
// (add-or-replace by name, each ZSTD-compressed) and `removals` (drop by name).
// Every other existing entry is copied VERBATIM (no decompress/recompress, so
// large payloads like octree.bin are untouched). Writes to `path + ".tmp"`
// then atomically renames over `path`; on any failure the original is left
// intact. The caller must ensure `path` is not held open for writing (on
// Windows the rename would fail) — the viewer closes the cloud's store first.
bool RepackPackage(const std::string& path,
                   const std::vector<std::pair<std::string, std::vector<uint8_t>>>& upserts,
                   const std::vector<std::string>& removals = {});

} // namespace pf
