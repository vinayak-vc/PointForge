#include "VideoExporter.h"
#include "common/Log.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

namespace pf {

struct VideoExporterImpl {
    IMFSinkWriter* writer = nullptr;
    DWORD          streamIdx = 0;
    VideoExportSettings cfg;
    int64_t        frameIdx = 0;
    bool           active = false;
    bool           mfStarted = false;
    bool           coInit = false;
    std::string    err;

    void release() {
        if (writer) { writer->Release(); writer = nullptr; }
        if (mfStarted) { MFShutdown(); mfStarted = false; }
        if (coInit) { CoUninitialize(); coInit = false; }
        active = false;
    }
};

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

VideoExporter::VideoExporter() : impl_(new VideoExporterImpl) {}
VideoExporter::~VideoExporter() {
    if (impl_->active) abort();
    delete impl_;
}

bool VideoExporter::available() { return true; }
bool VideoExporter::active() const { return impl_->active; }
const std::string& VideoExporter::error() const { return impl_->err; }

bool VideoExporter::begin(const VideoExportSettings& s) {
    VideoExporterImpl& m = *impl_;
    if (m.active) { m.err = "export already running"; return false; }
    m.err.clear();
    m.cfg = s;
    m.frameIdx = 0;

    if (s.width < 2 || s.height < 2 || (s.width & 1) || (s.height & 1) || s.fps < 1) {
        m.err = "invalid export dimensions/fps";
        return false;
    }

    // COM: tolerate an already-initialized apartment (RPC_E_CHANGED_MODE) —
    // MF's Sink Writer works from either model; only balance what we own.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    m.coInit = SUCCEEDED(hr);

    if (FAILED(MFStartup(MF_VERSION))) {
        m.err = "MFStartup failed";
        if (m.coInit) { CoUninitialize(); m.coInit = false; }
        return false;
    }
    m.mfStarted = true;

    auto fail = [&](const std::string& what, HRESULT code) {
        char buf[64]; snprintf(buf, sizeof(buf), " (hr=0x%08lX)", (unsigned long)code);
        m.err = what + buf;
        m.release();
        return false;
    };

    IMFAttributes* attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    if (attrs) attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    hr = MFCreateSinkWriterFromURL(widen(s.outPath).c_str(), nullptr, attrs, &m.writer);
    if (attrs) attrs->Release();
    if (FAILED(hr)) return fail("MFCreateSinkWriterFromURL failed", hr);

    // Output: H.264 in MP4.
    IMFMediaType* out = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)((int64_t)s.bitrateMbps * 1000000));
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, (UINT32)s.width, (UINT32)s.height);
    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, (UINT32)s.fps, 1);
    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    hr = m.writer->AddStream(out, &m.streamIdx);
    out->Release();
    if (FAILED(hr)) return fail("SinkWriter AddStream failed", hr);

    // Input: NV12 frames we produce from the GL readback.
    IMFMediaType* in = nullptr;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, (UINT32)s.width, (UINT32)s.height);
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, (UINT32)s.fps, 1);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = m.writer->SetInputMediaType(m.streamIdx, in, nullptr);
    in->Release();
    if (FAILED(hr)) return fail("SinkWriter SetInputMediaType(NV12) failed", hr);

    hr = m.writer->BeginWriting();
    if (FAILED(hr)) return fail("SinkWriter BeginWriting failed", hr);

    m.active = true;
    logInfo("Video export started: " + s.outPath + " " +
            std::to_string(s.width) + "x" + std::to_string(s.height) + "@" +
            std::to_string(s.fps) + " " + std::to_string(s.bitrateMbps) + " Mbps");
    return true;
}

bool VideoExporter::writeFrameNV12(const uint8_t* nv12, size_t size) {
    VideoExporterImpl& m = *impl_;
    if (!m.active) return false;
    const size_t need = (size_t)m.cfg.width * m.cfg.height * 3 / 2;
    if (size < need) { m.err = "NV12 buffer too small"; return false; }

    IMFMediaBuffer* buf = nullptr;
    if (FAILED(MFCreateMemoryBuffer((DWORD)need, &buf)) || !buf) {
        m.err = "MFCreateMemoryBuffer failed";
        return false;
    }
    BYTE* dst = nullptr;
    bool ok = false;
    if (SUCCEEDED(buf->Lock(&dst, nullptr, nullptr))) {
        memcpy(dst, nv12, need);
        buf->Unlock();
        buf->SetCurrentLength((DWORD)need);

        IMFSample* smp = nullptr;
        if (SUCCEEDED(MFCreateSample(&smp)) && smp) {
            smp->AddBuffer(buf);
            // 100 ns units; computed per frame index so rounding never drifts.
            const LONGLONG t0 = (LONGLONG)(m.frameIdx * 10000000LL / m.cfg.fps);
            const LONGLONG t1 = (LONGLONG)((m.frameIdx + 1) * 10000000LL / m.cfg.fps);
            smp->SetSampleTime(t0);
            smp->SetSampleDuration(t1 - t0);
            HRESULT hr = m.writer->WriteSample(m.streamIdx, smp);
            if (SUCCEEDED(hr)) { ++m.frameIdx; ok = true; }
            else {
                char b[64]; snprintf(b, sizeof(b), "WriteSample failed (hr=0x%08lX)", (unsigned long)hr);
                m.err = b;
            }
            smp->Release();
        } else {
            m.err = "MFCreateSample failed";
        }
    } else {
        m.err = "media buffer Lock failed";
    }
    buf->Release();
    return ok;
}

bool VideoExporter::finish() {
    VideoExporterImpl& m = *impl_;
    if (!m.active) return false;
    HRESULT hr = m.writer->Finalize();
    const bool ok = SUCCEEDED(hr);
    if (!ok) {
        char b[64]; snprintf(b, sizeof(b), "SinkWriter Finalize failed (hr=0x%08lX)", (unsigned long)hr);
        m.err = b;
    }
    m.release();
    if (ok) logInfo("Video export finished: " + m.cfg.outPath +
                    " (" + std::to_string(m.frameIdx) + " frames)");
    else    logError("Video export finalize failed: " + m.err);
    return ok;
}

void VideoExporter::abort() {
    VideoExporterImpl& m = *impl_;
    if (!m.active) return;
    m.release();                      // no Finalize -> moov never written
    DeleteFileA(m.cfg.outPath.c_str());  // partial MP4 is useless; remove it
    logInfo("Video export canceled: " + m.cfg.outPath);
}

} // namespace pf

#else // !_WIN32 — stub (Media Foundation is Windows-only)

namespace pf {
struct VideoExporterImpl { std::string err = "video export requires Windows (Media Foundation)"; };
VideoExporter::VideoExporter() : impl_(new VideoExporterImpl) {}
VideoExporter::~VideoExporter() { delete impl_; }
bool VideoExporter::available() { return false; }
bool VideoExporter::active() const { return false; }
const std::string& VideoExporter::error() const { return impl_->err; }
bool VideoExporter::begin(const VideoExportSettings&) { return false; }
bool VideoExporter::writeFrameNV12(const uint8_t*, size_t) { return false; }
bool VideoExporter::finish() { return false; }
void VideoExporter::abort() {}
} // namespace pf

#endif
