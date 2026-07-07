#pragma once
#include <cstdint>
#include <string>

namespace pf {

struct VideoExportSettings {
    int         width      = 1920;   // must be even (NV12 chroma)
    int         height     = 1080;   // must be even
    int         fps        = 30;
    int         bitrateMbps = 20;    // H.264 average bitrate hint
    std::string outPath;             // .mp4 destination
};

struct VideoExporterImpl;  // Media Foundation kept out of the header

// Self-contained MP4 (H.264) writer around IMFSinkWriter. The Sink Writer
// performs its own encoder MFT selection; MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS
// lets it pick NVENC/QuickSync when available, with automatic software
// fallback. Feed NV12 frames (use rgbToNv12BottomUp from Nv12.h); timestamps
// are generated internally from the fps.
//
// Threading/COM: begin/writeFrameNV12/finish/abort must all be called from the
// same thread (the viewer calls from the main thread). MFStartup/MFShutdown
// are handled internally (process-refcounted, safe next to RemoteServer's
// encoder thread).
//
// Non-Windows builds compile as a stub whose begin() fails with an error.
class VideoExporter {
public:
    VideoExporter();
    ~VideoExporter();
    VideoExporter(const VideoExporter&) = delete;
    VideoExporter& operator=(const VideoExporter&) = delete;

    static bool available();                       // false in the stub build

    bool begin(const VideoExportSettings& s);      // false -> see error()
    bool writeFrameNV12(const uint8_t* nv12, size_t size);
    bool finish();                                 // finalize the MP4
    void abort();                                  // cancel: discard + delete partial file

    bool active() const;
    const std::string& error() const;

private:
    VideoExporterImpl* impl_;
};

} // namespace pf
