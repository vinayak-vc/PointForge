#include "Photogrammetry.h"
#include "common/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifndef PF_VIRT_FIRMWARE_ENABLED   // winnt.h processor-feature id (Windows 8+ SDKs)
#define PF_VIRT_FIRMWARE_ENABLED 21
#endif
#endif

namespace fs = std::filesystem;

namespace pf {

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
namespace {

std::string toLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

bool isImageExt(const std::string& lowExt) {
    return lowExt == ".jpg" || lowExt == ".jpeg" || lowExt == ".png" ||
           lowExt == ".tif" || lowExt == ".tiff";
}

bool isJpegExt(const std::string& lowExt) { return lowExt == ".jpg" || lowExt == ".jpeg"; }

std::string getEnvVar(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// Engine installs live in local (non-roaming) app data — they are large
// machine-specific binaries. Kept separate from the SDL_GetPrefPath roaming
// prefs on purpose.
std::string enginesRoot() {
    std::string base = getEnvVar("LOCALAPPDATA");
    if (base.empty()) base = getEnvVar("TEMP");
    return (fs::path(base) / "ViitorX" / "PointForge" / "engines").string();
}

// forward slashes for docker -v mount specs
std::string forwardSlashes(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

} // namespace

const char* engineName(PhotogramEngine e) {
    return e == PhotogramEngine::ODM ? "ODM" : "COLMAP";
}

// ---------------------------------------------------------------------------
// EXIF GPS sniffing — minimal, bounds-checked JPEG APP1/TIFF walk. We only
// need one bit of information (does this image carry a GPS fix?), so we look
// for the GPS IFD pointer (tag 0x8825) in IFD0 and then require an actual
// GPSLatitude/GPSLongitude entry inside the GPS IFD (some cameras write an
// empty GPS IFD when no fix was available).
// ---------------------------------------------------------------------------
namespace {

uint16_t rd16(const uint8_t* p, bool little) {
    return little ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}
uint32_t rd32(const uint8_t* p, bool little) {
    return little ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24))
                  : (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

bool jpegHasGps(const std::vector<uint8_t>& buf) {
    const size_t n = buf.size();
    if (n < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return false; // not a JPEG (SOI)

    // Walk segments looking for APP1/Exif.
    size_t pos = 2;
    const uint8_t* exif = nullptr;
    size_t exifLen = 0;
    while (pos + 4 <= n) {
        if (buf[pos] != 0xFF) return false;
        const uint8_t marker = buf[pos + 1];
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD9)) { pos += 2; continue; }
        if (marker == 0xDA) break;                          // start of scan — no EXIF ahead
        const size_t segLen = ((size_t)buf[pos + 2] << 8) | buf[pos + 3];
        if (segLen < 2 || pos + 2 + segLen > n) break;      // truncated (we only read the head)
        if (marker == 0xE1 && segLen >= 2 + 6 &&
            std::memcmp(&buf[pos + 4], "Exif\0\0", 6) == 0) {
            exif = &buf[pos + 4 + 6];
            exifLen = segLen - 2 - 6;
            break;
        }
        pos += 2 + segLen;
    }
    if (!exif || exifLen < 8) return false;

    // TIFF header
    const bool little = exif[0] == 'I' && exif[1] == 'I';
    const bool big    = exif[0] == 'M' && exif[1] == 'M';
    if (!little && !big) return false;
    if (rd16(exif + 2, little) != 42) return false;
    const uint32_t ifd0 = rd32(exif + 4, little);
    if (ifd0 + 2 > exifLen) return false;

    // IFD0: find the GPS IFD pointer.
    const uint16_t count0 = rd16(exif + ifd0, little);
    uint32_t gpsIfd = 0;
    for (uint16_t i = 0; i < count0; ++i) {
        const size_t e = (size_t)ifd0 + 2 + (size_t)i * 12;
        if (e + 12 > exifLen) return false;
        if (rd16(exif + e, little) == 0x8825) { gpsIfd = rd32(exif + e + 8, little); break; }
    }
    if (gpsIfd == 0 || (size_t)gpsIfd + 2 > exifLen) return false;

    // GPS IFD: require actual latitude/longitude entries.
    const uint16_t countG = rd16(exif + gpsIfd, little);
    for (uint16_t i = 0; i < countG; ++i) {
        const size_t e = (size_t)gpsIfd + 2 + (size_t)i * 12;
        if (e + 12 > exifLen) return false;
        const uint16_t tag = rd16(exif + e, little);
        if (tag == 0x0002 /*GPSLatitude*/ || tag == 0x0004 /*GPSLongitude*/) return true;
    }
    return false;
}

// EXIF lives at the front of the file; 128 KB covers every sane maker.
std::vector<uint8_t> readHead(const fs::path& p, size_t maxBytes = 128 * 1024) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> buf;
    if (!f) return buf;
    buf.resize(maxBytes);
    f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)maxBytes);
    buf.resize((size_t)f.gcount());
    return buf;
}

} // namespace

ImageSetInfo scanImageFolder(const std::string& dir) {
    ImageSetInfo info;
    std::error_code ec;
    std::vector<fs::path> jpegs;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = toLower(entry.path().extension().string());
        if (!isImageExt(ext)) continue;
        ++info.imageCount;
        std::error_code sec;
        const auto sz = entry.file_size(sec);
        if (!sec) info.totalBytes += (uint64_t)sz;
        if (isJpegExt(ext)) { ++info.jpegCount; jpegs.push_back(entry.path()); }
    }
    // Sniff an evenly-spread sample; 24 reads of <=128 KB is instant even on
    // network shares and representative for the ODM/COLMAP recommendation.
    const size_t kMaxSample = 24;
    const size_t stride = jpegs.empty() ? 1 : std::max<size_t>(1, jpegs.size() / kMaxSample);
    for (size_t i = 0; i < jpegs.size(); i += stride) {
        ++info.sampled;
        if (jpegHasGps(readHead(jpegs[i]))) ++info.gpsCount;
        if ((size_t)info.sampled >= kMaxSample) break;
    }
    return info;
}

PhotogramEngine recommendEngine(const ImageSetInfo& info, const EngineStatus& st) {
    // ODM runs on Docker, and Docker cannot start at all without hardware
    // virtualization - COLMAP (native) is the only engine that can run then.
    if (!st.virtualizationOk()) return PhotogramEngine::COLMAP;
    if (info.hasGps()) return PhotogramEngine::ODM;
    if (!st.cudaGpu) return PhotogramEngine::ODM; // COLMAP dense MVS needs CUDA
    return PhotogramEngine::COLMAP;
}

std::string recommendReason(const ImageSetInfo& info, const EngineStatus& st) {
    if (!st.virtualizationOk()) {
        std::string r = "CPU virtualization (Intel VT-x / AMD-V) is disabled in this PC's "
                        "BIOS/UEFI, so Docker - and with it ODM - cannot run. Falling back "
                        "to COLMAP, which runs natively without it.";
        if (info.hasGps())
            r += " Note: the photos' GPS tags will not be used, so the result has "
                 "arbitrary scale and origin. Enable virtualization in the BIOS to unlock ODM.";
        return r;
    }
    if (info.hasGps())
        return "GPS tags found in the photos - ODM produces a georeferenced, "
               "metric-scale cloud straight from them.";
    if (!st.cudaGpu)
        return "No GPS tags and no NVIDIA GPU detected - COLMAP's dense "
               "reconstruction needs CUDA, so ODM is the safer choice here.";
    return "No GPS tags - COLMAP gives the densest result for ground-level or "
           "object captures (output has arbitrary scale and origin).";
}

#ifdef _WIN32

// ---------------------------------------------------------------------------
// child process plumbing
// ---------------------------------------------------------------------------
namespace {

// Builds an environment block with PATH prepended (COLMAP's exe needs its
// bin/ + lib/ DLL dirs on PATH, as its COLMAP.bat would set up).
std::vector<char> envWithPathPrepend(const std::string& prepend) {
    std::vector<char> block;
    LPCH cur = GetEnvironmentStringsA();
    if (!cur) return block;
    for (LPCH p = cur; *p;) {
        const size_t len = std::strlen(p);
        std::string entry(p, len);
        if (!prepend.empty() && _strnicmp(entry.c_str(), "PATH=", 5) == 0)
            entry = "PATH=" + prepend + ";" + entry.substr(5);
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back('\0');
        p += len + 1;
    }
    FreeEnvironmentStringsA(cur);
    block.push_back('\0');
    return block;
}

struct RunOpts {
    std::string workDir;                 // empty = inherit
    std::string pathPrepend;             // extra PATH entries for the child
    std::atomic<bool>* cancel = nullptr;
    std::function<void(const std::string&)> onLine;   // each stdout/stderr line
    std::string cancelCmd;               // extra command to run on cancel (docker rm -f ...)
};

constexpr int kExitSpawnFailed = -9001;
constexpr int kExitCanceled    = -9002;

// Runs `cmdline` hidden, merging stderr into stdout and streaming lines to
// opts.onLine. Cancels by TerminateProcess (plus opts.cancelCmd for children
// that outlive their launcher, e.g. docker containers).
int runProcess(const std::string& cmdline, const RunOpts& opts) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return kExitSpawnFailed;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError  = writePipe;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<char> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back('\0');
    std::vector<char> env = envWithPathPrepend(opts.pathPrepend);

    const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW,
                                   env.empty() ? nullptr : env.data(),
                                   opts.workDir.empty() ? nullptr : opts.workDir.c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        logWarn("[photogrammetry] failed to spawn: " + cmdline);
        return kExitSpawnFailed;
    }
    CloseHandle(pi.hThread);

    std::string pending;
    bool canceled = false;
    char chunk[4096];
    for (;;) {
        if (opts.cancel && opts.cancel->load() && !canceled) {
            canceled = true;
            TerminateProcess(pi.hProcess, 1);
            if (!opts.cancelCmd.empty()) {
                RunOpts kill;                       // fire-and-forget cleanup
                runProcess(opts.cancelCmd, kill);
            }
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 150) == WAIT_OBJECT_0) {
                // drain whatever the child wrote between the peek and its exit
                while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    DWORD got = 0;
                    if (!ReadFile(readPipe, chunk, std::min<DWORD>(avail, sizeof(chunk)), &got, nullptr) || got == 0) break;
                    pending.append(chunk, got);
                }
                break;
            }
            continue;
        }
        DWORD got = 0;
        if (!ReadFile(readPipe, chunk, std::min<DWORD>(avail, sizeof(chunk)), &got, nullptr) || got == 0) break;
        pending.append(chunk, got);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && opts.onLine) opts.onLine(line);
        }
    }
    if (!pending.empty() && opts.onLine) opts.onLine(pending);

    WaitForSingleObject(pi.hProcess, canceled ? 10000 : INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return canceled ? kExitCanceled : (int)code;
}

// quick yes/no probe (no output wanted)
bool probeOk(const std::string& cmdline) {
    RunOpts o;
    return runProcess(cmdline, o) == 0;
}

std::string findColmapExe() {
    const fs::path root = fs::path(enginesRoot()) / "colmap";
    std::error_code ec;
    if (!fs::exists(root, ec)) return {};
    for (auto it = fs::recursive_directory_iterator(root, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (toLower(it->path().filename().string()) == "colmap.exe")
            return it->path().string();
    }
    return {};
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

// Hardware virtualization state. When a hypervisor is already active
// (Hyper-V / WSL2) the firmware flag is consumed and reads false, so both
// signals are needed: firmware-enabled OR hypervisor-running means Docker can
// work; both false means VT-x/AMD-V is off in the BIOS and Docker is a
// hard no-go (no software workaround exists).
bool virtFirmwareEnabled() {
    return IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != 0;
}
bool hypervisorPresent() {
#ifdef _MSC_VER
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 1);
    return (cpuInfo[2] & (1 << 31)) != 0;   // CPUID.1:ECX[31] hypervisor bit
#else
    return false;
#endif
}
bool virtualizationAvailable() { return virtFirmwareEnabled() || hypervisorPresent(); }

} // namespace

// ---------------------------------------------------------------------------
// engine status
// ---------------------------------------------------------------------------
EngineStatus queryEngines() {
    EngineStatus st;
    st.colmapExe = findColmapExe();
    st.colmapInstalled = !st.colmapExe.empty();
    st.cudaGpu = probeOk("nvidia-smi -L");
    st.virtFirmware = virtFirmwareEnabled();
    st.hypervisor = hypervisorPresent();
    st.dockerCli = probeOk("docker --version");
    if (st.dockerCli && st.virtualizationOk()) {
        st.dockerRunning = probeOk("docker info");
        if (st.dockerRunning)
            st.odmImage = probeOk("docker image inspect opendronemap/odm");
    }
    return st;
}

// ---------------------------------------------------------------------------
// install
// ---------------------------------------------------------------------------
namespace {

// Official COLMAP 4.1.0 release assets (github.com/colmap/colmap). Version is
// pinned so the download is reproducible; sizes drive the progress bar.
const char* kColmapCudaUrl   = "https://github.com/colmap/colmap/releases/download/4.1.0/colmap-x64-windows-cuda.zip";
const char* kColmapNoCudaUrl = "https://github.com/colmap/colmap/releases/download/4.1.0/colmap-x64-windows-nocuda.zip";
constexpr uint64_t kColmapCudaBytes   = 321463068;
constexpr uint64_t kColmapNoCudaBytes = 118876963;

// Download with the system curl (ships with Windows 10 1803+), reporting
// progress by polling the output file size against the known asset size.
bool downloadFile(const std::string& url, const std::string& dest, uint64_t expectedBytes,
                  std::atomic<bool>& cancel, const PhotoProgressFn& progress,
                  float progFrom, float progTo, std::string& err) {
    std::error_code ec;
    fs::remove(dest, ec);
    // --fail: HTTP errors become a nonzero exit instead of an HTML error page
    // saved as the "zip"; --location: follow the GitHub CDN redirect.
    const std::string cmd = "curl.exe --location --fail --retry 3 --silent --show-error -o " +
                            quoted(dest) + " " + quoted(url);

    std::atomic<bool> done{false};
    std::string curlErr;
    std::thread poll([&] {
        while (!done.load()) {
            std::error_code sec;
            const auto sz = fs::exists(dest, sec) ? fs::file_size(dest, sec) : 0;
            if (!sec && expectedBytes > 0 && progress) {
                const float f = std::min(1.0f, (float)((double)sz / (double)expectedBytes));
                progress(progFrom + (progTo - progFrom) * f,
                         "Downloading COLMAP  (" + std::to_string(sz / (1024 * 1024)) + " / " +
                             std::to_string(expectedBytes / (1024 * 1024)) + " MB)");
            }
            for (int i = 0; i < 5 && !done.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    RunOpts o;
    o.cancel = &cancel;
    o.onLine = [&](const std::string& l) { curlErr = l; };
    const int rc = runProcess(cmd, o);
    done = true;
    poll.join();
    if (rc == kExitCanceled) { err = "Canceled"; return false; }
    if (rc != 0) {
        err = "Download failed (" + std::string(rc == kExitSpawnFailed ? "curl.exe not found" : curlErr) + ")";
        return false;
    }
    return true;
}

bool installColmap(std::atomic<bool>& cancel, const PhotoProgressFn& progress,
                   float progFrom, float progTo, std::string& err) {
    if (!findColmapExe().empty()) return true;   // already installed
    const bool cuda = probeOk("nvidia-smi -L");
    const std::string url = cuda ? kColmapCudaUrl : kColmapNoCudaUrl;
    const uint64_t bytes  = cuda ? kColmapCudaBytes : kColmapNoCudaBytes;

    const fs::path dir = fs::path(enginesRoot()) / "colmap";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { err = "Cannot create " + dir.string(); return false; }
    const std::string zip = (dir / "colmap.zip").string();

    const float mid = progFrom + (progTo - progFrom) * 0.85f;
    if (!downloadFile(url, zip, bytes, cancel, progress, progFrom, mid, err)) return false;

    if (progress) progress(mid, "Unpacking COLMAP...");
    RunOpts o;
    o.cancel = &cancel;
    const int rc = runProcess("tar.exe -xf " + quoted(zip) + " -C " + quoted(dir.string()), o);
    fs::remove(zip, ec);
    if (rc == kExitCanceled) { err = "Canceled"; return false; }
    if (rc != 0) { err = "Failed to unpack the COLMAP archive"; return false; }
    if (findColmapExe().empty()) { err = "COLMAP unpacked but colmap.exe was not found"; return false; }
    if (progress) progress(progTo, "COLMAP installed");
    return true;
}

bool installOdm(std::atomic<bool>& cancel, const PhotoProgressFn& progress,
                float progFrom, float progTo, std::string& err) {
    auto report = [&](float f, const std::string& m) { if (progress) progress(f, m); };

    // 0) Hardware gate - fail fast instead of installing Docker Desktop and
    // then polling a daemon that can never start.
    if (!virtualizationAvailable()) {
        err = "ODM skipped: CPU virtualization (Intel VT-x / AMD-V) is disabled in this "
              "PC's BIOS/UEFI, which Docker requires. To unlock ODM later: enable "
              "virtualization in the BIOS setup, then run this setup again. "
              "COLMAP works without it and is used instead.";
        return false;
    }

    // 1) Docker CLI
    if (!probeOk("docker --version")) {
        report(progFrom, "Installing Docker Desktop (a Windows admin prompt may appear)...");
        RunOpts o;
        o.cancel = &cancel;
        o.onLine = [&](const std::string& l) { report(progFrom, "Docker Desktop: " + l); };
        const int rc = runProcess(
            "winget install -e --id Docker.DockerDesktop "
            "--accept-source-agreements --accept-package-agreements", o);
        if (rc == kExitCanceled) { err = "Canceled"; return false; }
        if (rc != 0 || !probeOk("docker --version")) {
            err = "Docker Desktop install did not complete (winget exit " + std::to_string(rc) +
                  "). A reboot or sign-out may be required, then run setup again.";
            return false;
        }
    }

    // 2) Docker daemon — start Docker Desktop and wait for it to answer.
    const float waitTo = progFrom + (progTo - progFrom) * 0.25f;
    if (!probeOk("docker info")) {
        report(waitTo, "Starting Docker Desktop...");
        const char* candidates[] = {
            "C:\\Program Files\\Docker\\Docker\\Docker Desktop.exe",
            "C:\\Program Files (x86)\\Docker\\Docker\\Docker Desktop.exe",
        };
        for (const char* c : candidates) {
            std::error_code ec;
            if (fs::exists(c, ec)) {
                RunOpts bg;   // launcher returns immediately; the app keeps running
                runProcess("cmd /c start \"\" " + quoted(c), bg);
                break;
            }
        }
        for (int i = 0; i < 100; ++i) {           // up to ~5 min for first-time WSL init
            if (cancel.load()) { err = "Canceled"; return false; }
            if (probeOk("docker info")) break;
            report(waitTo, "Waiting for the Docker engine to start... (" + std::to_string(i * 3) + "s)");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        if (!probeOk("docker info")) {
            err = "The Docker engine did not come up. Docker Desktop may still be finishing its "
                  "first-time setup - once it shows 'running', run setup again.";
            return false;
        }
    }

    // 3) ODM image
    if (!probeOk("docker image inspect opendronemap/odm")) {
        const float pullFrom = progFrom + (progTo - progFrom) * 0.3f;
        report(pullFrom, "Downloading the ODM engine image (several GB, one-time)...");
        RunOpts o;
        o.cancel = &cancel;
        o.onLine = [&](const std::string& l) { report(pullFrom + (progTo - pullFrom) * 0.5f, "ODM image: " + l); };
        const int rc = runProcess("docker pull opendronemap/odm", o);
        if (rc == kExitCanceled) { err = "Canceled"; return false; }
        if (rc != 0) { err = "docker pull opendronemap/odm failed - see Console"; return false; }
    }
    report(progTo, "ODM ready");
    return true;
}

} // namespace

bool installEngines(bool wantColmap, bool wantOdm, std::atomic<bool>& cancel,
                    const PhotoProgressFn& progress, std::string& err) {
    std::string errors;
    // COLMAP first: small download, no admin rights, quick win before the
    // potentially interactive Docker install.
    if (wantColmap) {
        std::string e;
        if (!installColmap(cancel, progress, 0.0f, wantOdm ? 0.35f : 1.0f, e)) {
            logWarn("[photogrammetry] COLMAP setup failed: " + e);
            errors += "COLMAP: " + e;
        }
        if (cancel.load()) { err = "Canceled"; return false; }
    }
    if (wantOdm && !virtualizationAvailable()) {
        // Hardware limitation, not a failure: COLMAP is the supported
        // alternative, so the setup still counts as a success. The wizard's
        // status line explains how to unlock ODM (BIOS virtualization).
        logWarn("[photogrammetry] ODM skipped: CPU virtualization disabled in BIOS/UEFI");
        wantOdm = false;
        if (errors.empty() && progress)
            progress(1.0f, "COLMAP ready - ODM skipped (virtualization disabled in BIOS)");
    } else if (wantOdm) {
        std::string e;
        if (!installOdm(cancel, progress, wantColmap ? 0.35f : 0.0f, 1.0f, e)) {
            logWarn("[photogrammetry] ODM setup failed: " + e);
            if (!errors.empty()) errors += "  |  ";
            errors += "ODM: " + e;
        }
    }
    if (!errors.empty()) { err = errors; return false; }
    if (progress) progress(1.0f, "Photogrammetry engines ready");
    return true;
}

// ---------------------------------------------------------------------------
// reconstruction
// ---------------------------------------------------------------------------
namespace {

const char* qualityWord(int q) { return q <= 0 ? "low" : q >= 2 ? "high" : "medium"; }

// stage keyword -> progress fraction; message shows the raw line
struct Stage { const char* needle; float progress; };

float stageProgress(const std::string& line, const Stage* stages, size_t n, float current) {
    const std::string low = toLower(line);
    for (size_t i = 0; i < n; ++i)
        if (low.find(stages[i].needle) != std::string::npos)
            return std::max(current, stages[i].progress);
    return current;
}

bool runOdm(const std::string& imagesDir, const std::string& workDir, int quality,
            std::atomic<bool>& cancel, const PhotoProgressFn& progress,
            std::string& outPointFile, std::string& err) {
    std::error_code ec;
    fs::create_directories(workDir, ec);
    const std::string projName = "cloud";
    fs::create_directories(fs::path(workDir) / projName, ec);

    // The images dir is mounted read-only INTO the dataset mount, so nothing
    // is copied and ODM cannot touch the originals.
    const std::string container = "viitorx_odm_job";
    const std::string cmd =
        "docker run --rm --name " + container +
        " -v " + quoted(forwardSlashes(workDir) + ":/datasets") +
        " -v " + quoted(forwardSlashes(imagesDir) + ":/datasets/" + projName + "/images:ro") +
        " opendronemap/odm --project-path /datasets " + projName +
        " --pc-quality " + qualityWord(quality) +
        " --skip-3dmodel --end-with odm_georeferencing";

    static const Stage stages[] = {
        {"running dataset stage",            0.03f},
        {"running opensfm stage",            0.08f},
        {"reconstruct_ba",                   0.20f},
        {"running openmvs stage",            0.35f},
        {"densifying",                       0.45f},
        {"running odm_filterpoints stage",   0.80f},
        {"running odm_georeferencing stage", 0.92f},
    };
    float cur = 0.0f;
    RunOpts o;
    o.cancel = &cancel;
    o.cancelCmd = "docker rm -f " + container;
    o.onLine = [&](const std::string& l) {
        logDebug("[odm] " + l);
        cur = stageProgress(l, stages, sizeof(stages) / sizeof(stages[0]), cur);
        if (progress) progress(cur, l.substr(0, 160));
    };
    // Stale container from a crashed previous run would collide on --name.
    RunOpts pre;
    runProcess("docker rm -f " + container, pre);

    const int rc = runProcess(cmd, o);
    if (rc == kExitCanceled) { err = "Canceled"; return false; }
    if (rc != 0) { err = "ODM exited with code " + std::to_string(rc) + " - see Console"; return false; }

    const fs::path geo = fs::path(workDir) / projName / "odm_georeferencing";
    for (const char* name : {"odm_georeferenced_model.laz", "odm_georeferenced_model.las"}) {
        if (fs::exists(geo / name, ec)) { outPointFile = (geo / name).string(); return true; }
    }
    const fs::path filtered = fs::path(workDir) / projName / "odm_filterpoints" / "point_cloud.ply";
    if (fs::exists(filtered, ec)) { outPointFile = filtered.string(); return true; }
    err = "ODM finished but no point cloud was found in " + geo.string();
    return false;
}

bool runColmap(const std::string& imagesDir, const std::string& workDir, int quality,
               std::atomic<bool>& cancel, const PhotoProgressFn& progress,
               std::string& outPointFile, std::string& err) {
    const std::string exe = findColmapExe();
    if (exe.empty()) { err = "COLMAP is not installed"; return false; }
    std::error_code ec;
    fs::create_directories(workDir, ec);

    // COLMAP's exe needs its sibling bin/ + lib/ DLL dirs on PATH (what the
    // shipped COLMAP.bat does).
    const fs::path exeDir = fs::path(exe).parent_path();
    std::string pathPrepend = exeDir.string();
    const fs::path libDir = exeDir.parent_path() / "lib";
    if (fs::exists(libDir, ec)) pathPrepend += ";" + libDir.string();

    const bool cuda = probeOk("nvidia-smi -L");
    const std::string cmd = quoted(exe) + " automatic_reconstructor" +
                            " --workspace_path " + quoted(workDir) +
                            " --image_path " + quoted(imagesDir) +
                            " --quality " + qualityWord(quality) +
                            (cuda ? " --dense 1" : " --dense 0");

    static const Stage stages[] = {
        {"feature extraction", 0.05f},
        {"feature matching",   0.15f},
        {"registering image",  0.30f},
        {"bundle adjustment",  0.40f},
        {"undistort",          0.55f},
        {"stereo",             0.65f},
        {"fusing",             0.88f},
    };
    float cur = 0.0f;
    RunOpts o;
    o.cancel = &cancel;
    o.workDir = workDir;
    o.pathPrepend = pathPrepend;
    o.onLine = [&](const std::string& l) {
        logDebug("[colmap] " + l);
        cur = stageProgress(l, stages, sizeof(stages) / sizeof(stages[0]), cur);
        if (progress) progress(cur, l.substr(0, 160));
    };
    const int rc = runProcess(cmd, o);
    if (rc == kExitCanceled) { err = "Canceled"; return false; }
    if (rc != 0) { err = "COLMAP exited with code " + std::to_string(rc) + " - see Console"; return false; }

    if (cuda) {
        // dense fusion output: <ws>/dense/<i>/fused.ply
        const fs::path dense = fs::path(workDir) / "dense";
        if (fs::exists(dense, ec))
            for (const auto& sub : fs::directory_iterator(dense, ec))
                if (fs::exists(sub.path() / "fused.ply", ec)) {
                    outPointFile = (sub.path() / "fused.ply").string();
                    return true;
                }
    }
    // sparse fallback (also the --dense 0 path): export the model as PLY
    const fs::path sparse = fs::path(workDir) / "sparse";
    if (fs::exists(sparse, ec)) {
        for (const auto& sub : fs::directory_iterator(sparse, ec)) {
            if (!fs::exists(sub.path() / "cameras.bin", ec)) continue;
            const std::string ply = (fs::path(workDir) / "sparse.ply").string();
            RunOpts co;
            co.cancel = &cancel;
            co.pathPrepend = pathPrepend;
            const int crc = runProcess(quoted(exe) + " model_converter --input_path " +
                                           quoted(sub.path().string()) + " --output_path " +
                                           quoted(ply) + " --output_type PLY", co);
            if (crc == 0 && fs::exists(ply, ec)) {
                if (progress) progress(0.95f, "Sparse model exported (no CUDA GPU - dense stereo skipped)");
                outPointFile = ply;
                return true;
            }
        }
    }
    err = "COLMAP finished but produced no point cloud in " + workDir;
    return false;
}

} // namespace

bool runReconstruction(PhotogramEngine engine, const std::string& imagesDir,
                       const std::string& workDir, int quality,
                       std::atomic<bool>& cancel, const PhotoProgressFn& progress,
                       std::string& outPointFile, std::string& err) {
    logInfo("[photogrammetry] " + std::string(engineName(engine)) + " reconstruction: " +
            imagesDir + " -> " + workDir);
    return engine == PhotogramEngine::ODM
               ? runOdm(imagesDir, workDir, quality, cancel, progress, outPointFile, err)
               : runColmap(imagesDir, workDir, quality, cancel, progress, outPointFile, err);
}

#else // !_WIN32 — graceful stubs (same pattern as VideoExporter)

EngineStatus queryEngines() { return {}; }

bool installEngines(bool, bool, std::atomic<bool>&, const PhotoProgressFn&, std::string& err) {
    err = "Photogrammetry engine setup is only supported on Windows.";
    return false;
}

bool runReconstruction(PhotogramEngine, const std::string&, const std::string&, int,
                       std::atomic<bool>&, const PhotoProgressFn&, std::string& outPointFile,
                       std::string& err) {
    (void)outPointFile;
    err = "Photogrammetry reconstruction is only supported on Windows.";
    return false;
}

#endif

} // namespace pf
