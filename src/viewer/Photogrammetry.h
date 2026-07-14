#pragma once
// Level-2 photogrammetry integration. PointForge does NOT embed a
// reconstruction engine: ODM (Docker) and COLMAP (native binaries) stay
// external installs that this module detects, auto-installs (after explicit
// user consent in the Convert wizard) and drives as child processes from the
// background job worker. The produced point file (.laz / .ply) is then chained
// into the normal buildOctree conversion.
//
// Viewer-only: never include from pfcore. All process spawning is Windows
// (CreateProcess); non-Windows builds get graceful stubs, matching the
// VideoExporter precedent.
#include <atomic>
#include <functional>
#include <string>

namespace pf {

// progress callback: (fraction 0..1, human status line)
using PhotoProgressFn = std::function<void(float, const std::string&)>;

// ---- image folder scan ------------------------------------------------------

struct ImageSetInfo {
    int      imageCount = 0;   // jpg/jpeg/png/tif files found (non-recursive)
    int      jpegCount  = 0;   // subset that are JPEGs (EXIF-capable)
    int      sampled    = 0;   // JPEGs actually sniffed for GPS EXIF
    int      gpsCount   = 0;   // sampled JPEGs carrying a GPS latitude/longitude
    uint64_t totalBytes = 0;
    bool hasGps() const { return gpsCount > 0; }
};

// Counts images in `dir` and sniffs EXIF GPS tags on an evenly-spread sample
// of the JPEGs (bounded reads — never loads whole images). Cheap enough to run
// synchronously on folder pick.
ImageSetInfo scanImageFolder(const std::string& dir);

// ---- engines ----------------------------------------------------------------

enum class PhotogramEngine { ODM = 0, COLMAP = 1 };

const char* engineName(PhotogramEngine e);

struct EngineStatus {
    // COLMAP: managed native install under %LOCALAPPDATA%\ViitorX\PointForge\engines
    bool        colmapInstalled = false;
    std::string colmapExe;           // full path when installed
    // ODM: runs via Docker
    bool dockerCli     = false;      // docker.exe on PATH
    bool dockerRunning = false;      // daemon answered `docker info`
    bool odmImage      = false;      // opendronemap/odm image pulled
    // Hardware
    bool cudaGpu = false;            // nvidia-smi present (COLMAP dense MVS needs CUDA)
    bool virtFirmware = false;       // VT-x / AMD-V enabled in BIOS/UEFI firmware
    bool hypervisor   = false;       // a hypervisor is already running (Hyper-V / WSL2)

    // Docker Desktop needs hardware virtualization: either the firmware flag,
    // or an already-active hypervisor (which hides the firmware flag).
    bool virtualizationOk() const { return virtFirmware || hypervisor; }
    bool odmReady()    const { return dockerCli && dockerRunning && odmImage; }
    bool colmapReady() const { return colmapInstalled; }
    bool ready(PhotogramEngine e) const {
        return e == PhotogramEngine::ODM ? odmReady() : colmapReady();
    }
};

// Probes engine availability. Spawns short-lived processes (docker info,
// nvidia-smi) — call from a background thread, not the render loop.
EngineStatus queryEngines();

// ---- BIOS virtualization help -------------------------------------------

// Motherboard identity read from the registry (HKLM\HARDWARE\...\BIOS) —
// no WMI/COM dependency. Used to give the user board-specific instructions
// for enabling VT-x / AMD-V when Docker (ODM) is blocked.
struct BoardInfo {
    std::string vendor;    // e.g. "Gigabyte Technology Co., Ltd."
    std::string product;   // e.g. "B550M DS3H"
    std::string bios;      // e.g. "F13"
    bool amdCpu = false;   // AuthenticAMD => the setting is called "SVM Mode"
};
BoardInfo queryBoard();

// Vendor-specific step list (newline-separated) for enabling virtualization
// in this board's BIOS/UEFI; generic fallback for unknown vendors.
std::string biosVirtSteps(const BoardInfo& b);

// Opens the default browser on a web search for this exact board's
// virtualization steps ("enable SVM <vendor> <product> BIOS").
void openVirtSearch(const BoardInfo& b);

// GPS-tagged sets -> ODM (georeferenced, metric-scale LAZ straight from drone
// GPS). Untagged ground/object captures -> COLMAP (denser per-pixel MVS) —
// unless there is no CUDA GPU, where COLMAP cannot run dense stereo at all.
PhotogramEngine recommendEngine(const ImageSetInfo& info, const EngineStatus& st);

// One-line human reason for the recommendation (shown in the wizard).
std::string recommendReason(const ImageSetInfo& info, const EngineStatus& st);

// ---- install / run ----------------------------------------------------------

// Installs the requested engines with no manual user steps:
//   COLMAP — official GitHub release zip (CUDA build when an NVIDIA GPU is
//            present) downloaded via the system curl and unpacked with the
//            system tar into the app-data engines dir.
//   ODM    — Docker Desktop via winget when missing (may raise a UAC prompt),
//            then `docker pull opendronemap/odm`.
// Blocking; run on the job worker thread. Returns false and fills `err` if any
// requested engine could not be set up (the other may still have succeeded —
// re-query status afterwards).
bool installEngines(bool wantColmap, bool wantOdm, std::atomic<bool>& cancel,
                    const PhotoProgressFn& progress, std::string& err);

// Runs a full reconstruction of `imagesDir` into `workDir` (created if
// missing; intermediate data is kept for inspection). quality: 0 draft,
// 1 balanced, 2 high. On success `outPointFile` names the produced .laz (ODM)
// or .ply (COLMAP) ready for buildOctree. Blocking; honors `cancel` by
// terminating the child process (and force-removing the ODM container).
bool runReconstruction(PhotogramEngine engine, const std::string& imagesDir,
                       const std::string& workDir, int quality,
                       std::atomic<bool>& cancel, const PhotoProgressFn& progress,
                       std::string& outPointFile, std::string& err);

} // namespace pf
