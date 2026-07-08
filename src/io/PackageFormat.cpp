#include "io/PackageFormat.h"
#include "common/Log.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <unordered_set>
#include <deque>
#include <map>
#include <algorithm>

#ifdef PF_WITH_ZSTD
#include <zstd.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#define PF_HAS_CRYPTO 1
#endif

namespace pf {

// ---- Phase 18: AES-256-GCM at-rest crypto (Windows CNG / BCrypt) ----------
// PBKDF2-HMAC-SHA256 key derivation, per-entry random 12-byte IV, 16-byte GCM
// auth tag. Stored encrypted entry = IV(12) | tag(16) | ciphertext.
// Framing sizes are unconditional so ReadRaw/Read compute stored lengths even
// on a build with no crypto backend (it still won't be able to decrypt).
static constexpr int kIvLen = 12, kTagLen = 16;
static constexpr int kEncFrame = kIvLen + kTagLen;   // 28
#ifdef PF_HAS_CRYPTO
static constexpr int kKeyLen = 32, kSaltLen = 16;
static constexpr uint32_t kPbkdf2Iterations = 100000;
#define PF_NT_OK(s) (((NTSTATUS)(s)) >= 0)

static bool cryptoRandom(uint8_t* buf, size_t n) {
    return PF_NT_OK(BCryptGenRandom(nullptr, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

static bool cryptoDeriveKey(const std::string& password, const uint8_t* salt, size_t saltLen,
                            uint32_t iterations, uint8_t* outKey, size_t keyLen) {
    BCRYPT_ALG_HANDLE hPrf = nullptr;
    if (!PF_NT_OK(BCryptOpenAlgorithmProvider(&hPrf, BCRYPT_SHA256_ALGORITHM, nullptr,
                                              BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return false;
    NTSTATUS s = BCryptDeriveKeyPBKDF2(
        hPrf, (PUCHAR)password.data(), (ULONG)password.size(),
        (PUCHAR)salt, (ULONG)saltLen, (ULONGLONG)iterations, outKey, (ULONG)keyLen, 0);
    BCryptCloseAlgorithmProvider(hPrf, 0);
    return PF_NT_OK(s);
}

// AES-256-GCM. Encrypt: caller provides a fresh random iv (kIvLen); fills tag
// (kTagLen) + out (== inLen). Decrypt: verifies tag, fails on mismatch.
static bool aesGcm(bool encrypt, const uint8_t* key,
                   const uint8_t* iv, uint8_t* tag,
                   const uint8_t* in, size_t inLen, uint8_t* out) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    bool ok = false;
    if (!PF_NT_OK(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
    do {
        if (!PF_NT_OK(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                        (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) break;
        if (!PF_NT_OK(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                                 (PUCHAR)key, kKeyLen, 0))) break;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = (PUCHAR)iv;   info.cbNonce = kIvLen;
        info.pbTag   = tag;          info.cbTag   = kTagLen;
        ULONG done = 0;
        NTSTATUS s = encrypt
            ? BCryptEncrypt(hKey, (PUCHAR)in, (ULONG)inLen, &info, nullptr, 0, out, (ULONG)inLen, &done, 0)
            : BCryptDecrypt(hKey, (PUCHAR)in, (ULONG)inLen, &info, nullptr, 0, out, (ULONG)inLen, &done, 0);
        ok = PF_NT_OK(s) && done == inLen;
    } while (false);
    if (hKey) BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

// Fixed keycheck plaintext (16 bytes) encrypted under the derived key so a
// reader can validate the password before touching real entries.
static const uint8_t kKeyCheckPlain[16] = {
    'V','X','P','C','-','K','E','Y','C','H','E','C','K','!','!','!'
};
#endif // PF_HAS_CRYPTO

static_assert(sizeof(VXPCHeader) == 128, "VXPCHeader size mismatch");
static_assert(sizeof(VXPCDirectoryEntry) == 104, "VXPCDirectoryEntry size mismatch");

static uint32_t computeCrc32(uint32_t crc, const void* buf, size_t size) {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) c = 0xedb88320 ^ (c >> 1);
                else c = c >> 1;
            }
            table[i] = c;
        }
        initialized = true;
    }

    const uint8_t* p = (const uint8_t*)buf;
    crc = crc ^ ~0U;
    while (size--) {
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ ~0U;
}

// --- PackageStream: in-memory (source-agnostic; Phase 13) ---
// Holds a fully-materialised entry; works whether the bytes came from a file
// or an HTTP range. OpenStream had no callers, so this replaces the old
// file-only stream without affecting anything downstream.
class MemoryStream : public PackageStream {
public:
    explicit MemoryStream(std::vector<uint8_t> data) : data_(std::move(data)) {}
    uint64_t Read(void* buffer, uint64_t size) override {
        uint64_t maxRead = data_.size() - pos_;
        uint64_t toRead = (size > maxRead) ? maxRead : size;
        if (toRead == 0) return 0;
        std::memcpy(buffer, data_.data() + pos_, (size_t)toRead);
        pos_ += toRead;
        return toRead;
    }
    uint64_t GetSize() const override { return data_.size(); }
    uint64_t GetOffset() const override { return pos_; }
    void Seek(uint64_t offset) override { pos_ = (offset > data_.size()) ? data_.size() : offset; }
private:
    std::vector<uint8_t> data_;
    uint64_t pos_ = 0;
};

// --- ByteSource: local file (Phase 13) ---
class FileByteSource : public ByteSource {
public:
    explicit FileByteSource(const std::string& path) {
        f_ = std::fopen(path.c_str(), "rb");
        if (f_) {
#ifdef _WIN32
            _fseeki64(f_, 0, SEEK_END); size_ = (uint64_t)_ftelli64(f_);
#else
            fseeko(f_, 0, SEEK_END); size_ = (uint64_t)ftello(f_);
#endif
        }
    }
    ~FileByteSource() override { if (f_) std::fclose(f_); }
    bool valid() const override { return f_ != nullptr; }
    uint64_t size() const override { return size_; }
    uint64_t read(uint64_t offset, void* buffer, uint64_t len) override {
        if (!f_ || len == 0) return 0;
#ifdef _WIN32
        _fseeki64(f_, (long long)offset, SEEK_SET);
#else
        fseeko(f_, (off_t)offset, SEEK_SET);
#endif
        return (uint64_t)std::fread(buffer, 1, (size_t)len, f_);
    }
private:
    FILE* f_ = nullptr;
    uint64_t size_ = 0;
};

#ifdef _WIN32
// --- ByteSource: HTTP(S) via WinHTTP with a fixed-block LRU cache (Phase 13) ---
// The .vxpc layout (header -> payload -> trailing directory) is range-friendly:
// fetch the header, then the directory, then entries on demand. Reads are
// served from 64 KiB blocks; a miss fetches the covering span in one ranged
// GET (coalescing) and caches each block. Requires the server to honour
// `Range` (206 + Content-Range).
class HttpByteSource : public ByteSource {
public:
    explicit HttpByteSource(const std::string& url) { open(url); }
    ~HttpByteSource() override {
        if (hConnect_) WinHttpCloseHandle(hConnect_);
        if (hSession_) WinHttpCloseHandle(hSession_);
    }
    bool valid() const override { return valid_; }
    uint64_t size() const override { return total_; }

    uint64_t read(uint64_t offset, void* buffer, uint64_t len) override {
        if (!valid_ || len == 0 || offset >= total_) return 0;
        if (offset + len > total_) len = total_ - offset;

        const uint64_t b0 = offset / kBlock;
        const uint64_t b1 = (offset + len - 1) / kBlock;

        // Fast path: every covering block already cached -> assemble from cache.
        bool allCached = true;
        for (uint64_t b = b0; b <= b1; ++b) if (!cache_.count(b)) { allCached = false; break; }
        if (allCached) {
            uint8_t* out = (uint8_t*)buffer; uint64_t done = 0;
            while (done < len) {
                const uint64_t pos = offset + done, b = pos / kBlock;
                auto it = cache_.find(b);
                const uint64_t within = pos - b * kBlock;
                if (it == cache_.end() || within >= it->second.size()) break;
                const uint64_t n = std::min<uint64_t>(len - done, it->second.size() - within);
                std::memcpy(out + done, it->second.data() + within, (size_t)n);
                done += n;
            }
            return done;
        }

        // Slow path: fetch the whole covering span in ONE ranged GET and serve
        // directly from it. Serving from `span` (not the cache) is correct even
        // when the span exceeds the cache ceiling — caching happens after and
        // any eviction can't corrupt this read.
        const uint64_t spanStart = b0 * kBlock;
        const uint64_t spanEnd = std::min(total_, (b1 + 1) * kBlock);   // exclusive
        std::vector<uint8_t> span;
        if (!fetchRange(spanStart, spanEnd - spanStart, span)) return 0;

        const uint64_t within = offset - spanStart;
        uint64_t n = (within < span.size()) ? std::min<uint64_t>(len, span.size() - within) : 0;
        if (n > 0) std::memcpy(buffer, span.data() + within, (size_t)n);

        // Opportunistically cache the covered blocks for future small reads.
        for (uint64_t b = b0; b <= b1; ++b) {
            const uint64_t s = b * kBlock - spanStart;
            if (s >= span.size()) break;
            const uint64_t e = std::min<uint64_t>(s + kBlock, span.size());
            putBlock(b, std::vector<uint8_t>(span.begin() + s, span.begin() + e));
        }
        return n;
    }

private:
    static constexpr uint64_t kBlock = 65536;
    static constexpr size_t   kMaxBlocks = 256;   // ~16 MiB cache ceiling

    static std::wstring widen(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    void open(const std::string& url) {
        std::wstring wurl = widen(url);
        URL_COMPONENTS uc; std::memset(&uc, 0, sizeof(uc)); uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {0}, path[2048] = {0};
        uc.lpszHostName = host; uc.dwHostNameLength = 255;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { logError("HttpByteSource: bad URL"); return; }
        host_ = host; path_ = path; port_ = uc.nPort;
        secure_ = (uc.nScheme == INTERNET_SCHEME_HTTPS);

        hSession_ = WinHttpOpen(L"PointForge/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession_) { logError("HttpByteSource: WinHttpOpen failed"); return; }
        hConnect_ = WinHttpConnect(hSession_, host_.c_str(), port_, 0);
        if (!hConnect_) { logError("HttpByteSource: WinHttpConnect failed"); return; }

        // Discover total size via a 1-byte ranged GET -> Content-Range: .../TOTAL.
        std::vector<uint8_t> probe;
        uint64_t total = 0;
        if (!fetchRange(0, 1, probe, &total) || total == 0) {
            logError("HttpByteSource: server did not report a ranged size (Range unsupported?)");
            return;
        }
        total_ = total;
        valid_ = true;
    }

    void putBlock(uint64_t idx, std::vector<uint8_t> data) {
        if (cache_.count(idx)) return;
        cache_[idx] = std::move(data);
        order_.push_back(idx);
        while (order_.size() > kMaxBlocks) {
            uint64_t old = order_.front(); order_.pop_front();
            cache_.erase(old);
        }
    }

    // One ranged GET of [start, start+len). Fills `out`; if `totalOut`, parses
    // the Content-Range total. Returns false on transport/status failure.
    bool fetchRange(uint64_t start, uint64_t len, std::vector<uint8_t>& out, uint64_t* totalOut = nullptr) {
        out.clear();
        HINTERNET hReq = WinHttpOpenRequest(hConnect_, L"GET", path_.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            secure_ ? WINHTTP_FLAG_SECURE : 0);
        if (!hReq) return false;
        struct ReqGuard { HINTERNET h; ~ReqGuard(){ if (h) WinHttpCloseHandle(h);} } guard{hReq};

        wchar_t range[96];
        swprintf(range, 96, L"Range: bytes=%llu-%llu",
                 (unsigned long long)start, (unsigned long long)(start + len - 1));
        if (!WinHttpAddRequestHeaders(hReq, range, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD)) return false;
        if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return false;
        if (!WinHttpReceiveResponse(hReq, nullptr)) return false;

        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status != 206 && status != 200) { logError("HttpByteSource: HTTP status " + std::to_string(status)); return false; }

        if (totalOut) {
            *totalOut = 0;
            wchar_t cr[128]; DWORD crlen = sizeof(cr);
            if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX,
                                    cr, &crlen, WINHTTP_NO_HEADER_INDEX)) {
                const wchar_t* slash = wcsrchr(cr, L'/');   // "bytes a-b/TOTAL"
                if (slash && slash[1]) *totalOut = _wcstoui64(slash + 1, nullptr, 10);
            }
        }

        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hReq, &avail) || avail == 0) break;
            size_t base = out.size();
            out.resize(base + avail);
            DWORD got = 0;
            if (!WinHttpReadData(hReq, out.data() + base, avail, &got)) return false;
            out.resize(base + got);
            if (got == 0) break;
        }
        return true;
    }

    HINTERNET hSession_ = nullptr, hConnect_ = nullptr;
    std::wstring host_, path_;
    INTERNET_PORT port_ = 0;
    bool secure_ = false, valid_ = false;
    uint64_t total_ = 0;
    std::map<uint64_t, std::vector<uint8_t>> cache_;
    std::deque<uint64_t> order_;
};
#endif // _WIN32

std::unique_ptr<ByteSource> openByteSource(const std::string& path) {
    const bool isUrl = path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0;
    if (isUrl) {
#ifdef _WIN32
        auto s = std::make_unique<HttpByteSource>(path);
        if (s->valid()) return s;
        return nullptr;
#else
        logError("openByteSource: HTTP not supported on this platform: " + path);
        return nullptr;
#endif
    }
    auto s = std::make_unique<FileByteSource>(path);
    if (s->valid()) return s;
    return nullptr;
}

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

    // The filename field is fixed at 64 bytes (63 usable + NUL). Silent
    // truncation could alias two entries to the same stored name and corrupt
    // lookups, so reject an over-long name outright.
    if (filename.empty() || filename.size() >= sizeof(currentEntry_.filename)) {
        logError("PackageWriter: filename too long or empty: " + filename);
        return false;
    }

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
    currentEntry_.crc32 = computeCrc32(currentEntry_.crc32, data, size);
    return true;
}

bool PackageWriter::EndFile() {
    if (!valid_ || !writingFile_) return false;

    directory_.push_back(currentEntry_);
    writingFile_ = false;
    return true;
}

bool PackageWriter::AddFile(const std::string& filename, const std::string& sourcePath, Compression comp) {
    if (!valid_ || writingFile_) return false;

    FILE* in = std::fopen(sourcePath.c_str(), "rb");
    if (!in) {
        logError("PackageWriter: failed to read source file " + sourcePath);
        return false;
    }

    std::fseek(in, 0, SEEK_END);
    size_t size = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);

    std::vector<uint8_t> buffer(size);
    if (size > 0 && std::fread(buffer.data(), 1, size, in) != size) {
        std::fclose(in);
        return false;
    }
    std::fclose(in);

    return AddMemory(filename, buffer.data(), size, comp);
}

bool PackageWriter::SetEncryption(const std::string& password) {
#ifdef PF_HAS_CRYPTO
    if (!valid_) return false;
    salt_.resize(kSaltLen);
    if (!cryptoRandom(salt_.data(), kSaltLen)) { logError("PackageWriter: RNG failed"); return false; }
    iterations_ = kPbkdf2Iterations;
    key_.resize(kKeyLen);
    if (!cryptoDeriveKey(password, salt_.data(), kSaltLen, iterations_, key_.data(), kKeyLen)) {
        key_.clear();
        logError("PackageWriter: key derivation failed");
        return false;
    }
    encrypt_ = true;
    return true;
#else
    (void)password;
    logError("PackageWriter: encryption not supported on this platform");
    return false;
#endif
}

bool PackageWriter::AddMemory(const std::string& filename, const void* data, size_t size, Compression comp) {
    if (!BeginFile(filename)) return false;

    // 1. Compress (optional). `payload` = post-compression bytes.
    const uint8_t* payload = (const uint8_t*)data;
    size_t payloadLen = size;
    uint32_t compression = (uint32_t)Compression::None;
    std::vector<uint8_t> compBuf;
    if (comp == Compression::ZSTD && size > 0) {
#ifdef PF_WITH_ZSTD
        size_t bound = ZSTD_compressBound(size);
        compBuf.resize(bound);
        size_t cSize = ZSTD_compress(compBuf.data(), bound, data, size, 3);
        if (!ZSTD_isError(cSize) && cSize < size) {
            compBuf.resize(cSize);
            payload = compBuf.data(); payloadLen = cSize;
            compression = (uint32_t)Compression::ZSTD;
        }   // else incompressible -> keep None (payload stays = data)
#else
        logError("PackageWriter: ZSTD not compiled in, storing uncompressed");
#endif
    }

    // 2. Encrypt (optional, AFTER compression). Stored = IV|tag|ciphertext.
    std::vector<uint8_t> stored;
    uint32_t flags = 0;
    if (encrypt_) {
#ifdef PF_HAS_CRYPTO
        stored.resize((size_t)kIvLen + kTagLen + payloadLen);
        uint8_t* iv  = stored.data();
        uint8_t* tag = stored.data() + kIvLen;
        uint8_t* ct  = stored.data() + kIvLen + kTagLen;
        if (!cryptoRandom(iv, kIvLen) ||
            !aesGcm(true, key_.data(), iv, tag, payload, payloadLen, ct)) {
            logError("PackageWriter: encryption failed for " + filename);
            EndFile(); return false;
        }
        flags |= VXPC_FLAG_ENCRYPTED;
#else
        logError("PackageWriter: encryption requested but no crypto backend");
        EndFile(); return false;
#endif
    } else {
        stored.assign(payload, payload + payloadLen);
    }

    // 3. Write stored bytes (Write computes crc over them). Then set the
    //    logical sizes: originalSize=plaintext, compressedSize=pre-encryption.
    if (!Write(stored.data(), stored.size())) { EndFile(); return false; }
    currentEntry_.compression    = compression;
    currentEntry_.flags         |= flags;
    currentEntry_.originalSize    = size;
    currentEntry_.compressedSize  = payloadLen;
    return EndFile();
}
bool PackageWriter::AddPluginData(const std::string& relPath, const void* data, size_t size, Compression comp) {
    if (relPath.empty()) {
        logError("PackageWriter: empty plugin path");
        return false;
    }
    // Callers pass a vendor-relative path; the namespace prefix is ours to add
    // so the "core ignores plugins/" contract can't be sidestepped by a caller
    // that forgets (or fakes) the prefix.
    std::string name = (relPath.rfind("plugins/", 0) == 0) ? relPath : "plugins/" + relPath;
    return AddMemory(name, data, size, comp);
}

void PackageWriter::AddCustomMeta(const std::string& key, const std::string& value) {
    customMetadata_.push_back({key, value});
}

void PackageWriter::InheritHeader(const VXPCHeader& src) {
    if (!valid_) return;
    std::memcpy(header_.uuid, src.uuid, sizeof(header_.uuid));
    header_.createdTime = src.createdTime;
    header_.converterVersion = src.converterVersion;
    header_.packageFlags = src.packageFlags;
    WriteHeader();   // persist now; Finalize() rewrites it again with dir info
}

bool PackageWriter::AddRawEntry(const VXPCDirectoryEntry& templ, const void* storedBytes, size_t storedSize) {
    if (!valid_ || writingFile_) return false;
    FILE* f = (FILE*)file_;

    VXPCDirectoryEntry e = templ;
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    e.offset = (uint64_t)_ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    e.offset = (uint64_t)ftello(f);
#endif
    if (storedSize > 0) {
        if (std::fwrite(storedBytes, 1, storedSize, f) != storedSize) {
            logError("PackageWriter: failed to append raw entry");
            return false;
        }
    }
    directory_.push_back(e);
    return true;
}

bool PackageWriter::Finalize() {
    if (!valid_) return false;
    if (writingFile_) EndFile();

    if (!customMetadata_.empty()) {
        std::string json = "{\n";
        for (size_t i = 0; i < customMetadata_.size(); ++i) {
            json += "  \"" + customMetadata_[i].first + "\": \"" + customMetadata_[i].second + "\"";
            if (i < customMetadata_.size() - 1) json += ",\n";
            else json += "\n";
        }
        json += "}\n";
        AddMemory("custom_meta.json", json.c_str(), json.size(), Compression::ZSTD);
    }

#ifdef PF_HAS_CRYPTO
    // Phase 18: keycheck entry (stored UNENCRYPTED) so a reader can verify the
    // password before decrypting real entries. Layout:
    //   "VXCR"(4) | iterations(4) | salt(16) | checkIV(12) | checkTag(16) | checkCT(16)
    if (encrypt_ && !key_.empty()) {
        uint8_t blob[68];
        std::memset(blob, 0, sizeof(blob));
        std::memcpy(blob, "VXCR", 4);
        std::memcpy(blob + 4, &iterations_, 4);
        std::memcpy(blob + 8, salt_.data(), kSaltLen);
        uint8_t* civ  = blob + 24;
        uint8_t* ctag = blob + 36;
        uint8_t* cct  = blob + 52;
        if (cryptoRandom(civ, kIvLen) &&
            aesGcm(true, key_.data(), civ, ctag, kKeyCheckPlain, sizeof(kKeyCheckPlain), cct)) {
            bool save = encrypt_; encrypt_ = false;   // the keycheck itself is plaintext
            AddMemory("vxpc_crypto", blob, sizeof(blob), Compression::None);
            encrypt_ = save;
        } else {
            logError("PackageWriter: failed to build keycheck");
            return false;
        }
    }
#endif

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
    valid_ = false;
    directory_.clear();

    // Phase 13: route through a ByteSource (local file or http(s):// URL).
    src_ = openByteSource(path);
    if (!src_ || !src_->valid()) { src_.reset(); return false; }

    if (src_->read(0, &header_, sizeof(header_)) != sizeof(header_)) { src_.reset(); return false; }
    if (std::memcmp(header_.magic, "VXPC", 4) != 0 || header_.version != 1) { src_.reset(); return false; }

    if (header_.entryCount > 0) {
        directory_.resize(header_.entryCount);
        const uint64_t want = (uint64_t)header_.entryCount * sizeof(VXPCDirectoryEntry);
        if (src_->read(header_.directoryOffset, directory_.data(), want) != want) {
            directory_.clear();
            src_.reset();
            return false;
        }
    }

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

std::vector<uint8_t> PackageReader::ReadRaw(const std::string& name, VXPCDirectoryEntry& outEntry) const {
    std::vector<uint8_t> buffer;
    outEntry = VXPCDirectoryEntry{};
    if (!valid_ || !Contains(name)) return buffer;

    outEntry = GetEntry(name);
    // Stored length = pre-encryption payload (compressedSize) + the IV|tag
    // framing when encrypted. Verbatim bytes, no decrypt/decompress/CRC.
    const uint64_t frame = (outEntry.flags & VXPC_FLAG_ENCRYPTED) ? (uint64_t)kEncFrame : 0;
    const uint64_t storedSize = frame + outEntry.compressedSize;
    if (storedSize == 0) return buffer;   // valid empty entry
    if (!src_) return buffer;

    buffer.resize(storedSize);
    if (src_->read(outEntry.offset, buffer.data(), storedSize) != storedSize) buffer.clear();
    return buffer;
}

std::vector<std::string> PackageReader::ListEntries(const std::string& prefix) const {
    std::vector<std::string> names;
    if (!valid_) return names;
    for (const auto& entry : directory_) {
        // filename is NUL-terminated within the fixed field (BeginFile enforces).
        std::string name(entry.filename);
        if (prefix.empty() || name.rfind(prefix, 0) == 0) names.push_back(std::move(name));
    }
    return names;
}

std::unique_ptr<PackageStream> PackageReader::OpenStream(const std::string& name) const {
    if (!valid_ || !Contains(name)) return nullptr;
    // Materialise the (decompressed, CRC-checked) entry and stream it from
    // memory — source-agnostic (file or HTTP) and correct for compressed
    // entries (the old file stream read originalSize bytes of compressed data).
    return std::make_unique<MemoryStream>(Read(name));
}

std::vector<uint8_t> PackageReader::Read(const std::string& name) const {
    std::vector<uint8_t> buffer;
    if (!valid_) return buffer;

    VXPCDirectoryEntry entry = GetEntry(name);
    if (entry.originalSize == 0 && entry.offset == 0 && std::strcmp(entry.filename, name.c_str()) != 0) {
        return buffer;
    }
    if (!src_) return buffer;

    // Stored bytes = [IV|tag] (if encrypted) + payload (compressedSize).
    const bool encrypted = (entry.flags & VXPC_FLAG_ENCRYPTED) != 0;
    const uint64_t frame = encrypted ? (uint64_t)kEncFrame : 0;
    const uint64_t readSize = frame + entry.compressedSize;
    std::vector<uint8_t> diskBuffer(readSize);

    if (readSize > 0) {
        if (src_->read(entry.offset, diskBuffer.data(), readSize) != readSize) {
            diskBuffer.clear();
        } else {
            // Validate CRC32 of the stored bytes.
            uint32_t checksum = computeCrc32(0, diskBuffer.data(), diskBuffer.size());
            if (checksum != entry.crc32) {
                logError("PackageReader: CRC32 mismatch for " + name);
                diskBuffer.clear();
            }
        }
        if (diskBuffer.empty()) return buffer;
    }

    // Phase 18: decrypt (AES-256-GCM) -> payload (compressedSize bytes).
    std::vector<uint8_t> payload;
    if (encrypted) {
#ifdef PF_HAS_CRYPTO
        if (key_.empty()) {
            logError("PackageReader: '" + name + "' is encrypted — call SetPassword() first");
            return buffer;
        }
        payload.resize(entry.compressedSize);
        uint8_t* iv  = diskBuffer.data();
        uint8_t* tag = diskBuffer.data() + kIvLen;
        uint8_t* ct  = diskBuffer.data() + kEncFrame;
        if (!aesGcm(false, key_.data(), iv, tag, ct, entry.compressedSize, payload.data())) {
            logError("PackageReader: decryption/authentication failed for " + name);
            return buffer;
        }
#else
        logError("PackageReader: encrypted entry '" + name + "' but no crypto backend");
        return buffer;
#endif
    } else {
        payload = std::move(diskBuffer);
    }

    if (payload.empty() && entry.originalSize == 0) return buffer;  // valid empty

    if (entry.compression == 1) { // ZSTD
#ifdef PF_WITH_ZSTD
        buffer.resize(entry.originalSize);
        size_t dSize = ZSTD_decompress(buffer.data(), entry.originalSize, payload.data(), payload.size());
        if (ZSTD_isError(dSize) || dSize != entry.originalSize) {
            logError("PackageReader: ZSTD decompression failed for " + name);
            buffer.clear();
        }
#else
        logError("PackageReader: Cannot decompress ZSTD because PF_WITH_ZSTD is not defined.");
#endif
    } else {
        buffer = std::move(payload);
    }

    return buffer;
}

bool PackageReader::isEncrypted() const { return valid_ && Contains("vxpc_crypto"); }

bool PackageReader::SetPassword(const std::string& password) {
#ifdef PF_HAS_CRYPTO
    if (!valid_ || !Contains("vxpc_crypto")) {
        logError("PackageReader: package is not encrypted");
        return false;
    }
    auto blob = Read("vxpc_crypto");   // stored unencrypted
    if (blob.size() != 68 || std::memcmp(blob.data(), "VXCR", 4) != 0) {
        logError("PackageReader: malformed vxpc_crypto keycheck");
        return false;
    }
    uint32_t iters = 0; std::memcpy(&iters, blob.data() + 4, 4);
    const uint8_t* salt = blob.data() + 8;
    uint8_t* civ  = blob.data() + 24;
    uint8_t* ctag = blob.data() + 36;
    uint8_t* cct  = blob.data() + 52;
    std::vector<uint8_t> k(kKeyLen);
    if (!cryptoDeriveKey(password, salt, kSaltLen, iters, k.data(), kKeyLen)) {
        logError("PackageReader: key derivation failed");
        return false;
    }
    uint8_t check[16];
    if (!aesGcm(false, k.data(), civ, ctag, cct, sizeof(check), check) ||
        std::memcmp(check, kKeyCheckPlain, sizeof(check)) != 0) {
        logError("PackageReader: wrong password");
        return false;
    }
    key_ = std::move(k);
    return true;
#else
    (void)password;
    logError("PackageReader: encryption not supported on this platform");
    return false;
#endif
}

// --- Repack (Phase 7 support) ---

bool RepackPackage(const std::string& path,
                   const std::vector<std::pair<std::string, std::vector<uint8_t>>>& upserts,
                   const std::vector<std::string>& removals) {
    std::unordered_set<std::string> upsertNames, removeNames;
    for (const auto& u : upserts) upsertNames.insert(u.first);
    for (const auto& r : removals) removeNames.insert(r);

    const std::string tmp = path + ".tmp";
    {
        // The reader holds `path` open (its ByteSource), so it MUST be
        // destroyed before the rename below — on Windows a rename over a file
        // with any open handle fails ("Access is denied"). Scope both here.
        PackageReader reader;
        if (!reader.Open(path)) {
            logError("RepackPackage: cannot open " + path);
            return false;
        }
        PackageWriter writer;
        if (!writer.Create(tmp)) {
            logError("RepackPackage: cannot create " + tmp);
            return false;
        }
        writer.InheritHeader(reader.GetHeader());

        // 1. Copy every surviving existing entry VERBATIM (no re-(de)compress).
        //    Skip ones being replaced by an upsert or dropped by a removal.
        for (const std::string& name : reader.ListEntries()) {
            if (removeNames.count(name) || upsertNames.count(name)) continue;
            VXPCDirectoryEntry e;
            std::vector<uint8_t> raw = reader.ReadRaw(name, e);
            const uint64_t storedSize = ((e.flags & VXPC_FLAG_ENCRYPTED) ? (uint64_t)kEncFrame : 0) + e.compressedSize;
            if (raw.size() != storedSize) {
                logError("RepackPackage: raw read short for " + name);
                std::error_code ec; std::filesystem::remove(tmp, ec);
                return false;
            }
            if (!writer.AddRawEntry(e, raw.data(), raw.size())) {
                std::error_code ec; std::filesystem::remove(tmp, ec);
                return false;
            }
        }

        // 2. Apply upserts (add-or-replace), ZSTD-compressed.
        for (const auto& u : upserts) {
            if (!writer.AddMemory(u.first, u.second.data(), u.second.size(),
                                  PackageWriter::Compression::ZSTD)) {
                std::error_code ec; std::filesystem::remove(tmp, ec);
                return false;
            }
        }

        if (!writer.Finalize()) {
            std::error_code ec; std::filesystem::remove(tmp, ec);
            return false;
        }
    } // reader + writer both closed here -> `path` handle released

    // 3. Atomic replace. std::filesystem::rename replaces an existing dest on
    //    both Windows (MoveFileEx semantics) and POSIX.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // Fallback: remove-then-rename (some filesystems reject cross-replace).
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            logError("RepackPackage: rename failed: " + ec.message());
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

// --- Combine (Phase 10: multi-cloud package) ---

static std::string fileStem(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

// Minimal JSON string escape (names are user-facing free text).
static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

bool combineClouds(const std::string& outPath,
                   const std::vector<std::pair<std::string, std::string>>& sources) {
    if (sources.empty()) { logError("combineClouds: no sources"); return false; }

    PackageWriter writer;
    if (!writer.Create(outPath)) { logError("combineClouds: cannot create " + outPath); return false; }

    std::string manifest = "{\n  \"version\": 1,\n  \"clouds\": [\n";

    for (size_t i = 0; i < sources.size(); ++i) {
        const std::string& srcPath = sources[i].first;
        PackageReader r;
        if (!r.Open(srcPath)) { logError("combineClouds: cannot open source " + srcPath); return false; }
        if (r.Contains("scene.json")) {
            logError("combineClouds: source is itself multi-cloud (nesting unsupported): " + srcPath);
            return false;
        }
        const std::string prefix = "clouds/" + std::to_string(i) + "/";
        // Copy every entry verbatim under the cloud's namespace.
        for (const std::string& name : r.ListEntries()) {
            VXPCDirectoryEntry e;
            std::vector<uint8_t> raw = r.ReadRaw(name, e);
            const uint64_t storedSize = ((e.flags & VXPC_FLAG_ENCRYPTED) ? (uint64_t)kEncFrame : 0) + e.compressedSize;
            if (raw.size() != storedSize) {
                logError("combineClouds: raw read short for " + name + " in " + srcPath);
                return false;
            }
            VXPCDirectoryEntry ne = e;
            const std::string nsName = prefix + name;
            if (nsName.size() >= sizeof(ne.filename)) {
                logError("combineClouds: namespaced name too long: " + nsName);
                return false;
            }
            std::memset(ne.filename, 0, sizeof(ne.filename));
            std::strncpy(ne.filename, nsName.c_str(), sizeof(ne.filename) - 1);
            if (!writer.AddRawEntry(ne, raw.data(), raw.size())) {
                logError("combineClouds: failed to append " + nsName);
                return false;
            }
        }
        const std::string name = sources[i].second.empty() ? fileStem(srcPath) : sources[i].second;
        manifest += "    { \"prefix\": \"" + prefix + "\", \"name\": \"" + jsonEscape(name) + "\" }";
        manifest += (i + 1 < sources.size()) ? ",\n" : "\n";
    }
    manifest += "  ]\n}\n";

    if (!writer.AddMemory("scene.json", manifest.data(), manifest.size(),
                          PackageWriter::Compression::None)) {
        logError("combineClouds: failed to write scene.json");
        return false;
    }
    if (!writer.Finalize()) { logError("combineClouds: finalize failed"); return false; }
    logInfo("combineClouds: wrote " + std::to_string(sources.size()) + " clouds -> " + outPath);
    return true;
}

} // namespace pf
