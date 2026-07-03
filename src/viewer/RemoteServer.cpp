#include "RemoteServer.h"
#include "common/Log.h"

#ifdef PF_WITH_REMOTE

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#endif

#include <civetweb.h>
#include <nlohmann/json.hpp>
#ifdef PF_REMOTE_STREAM
#  include <turbojpeg.h>
#endif
#ifdef PF_REMOTE_WEBRTC
#  include <rtc/rtc.hpp>
#  include <rtc/rtp.hpp>
#  include <rtc/h264rtppacketizer.hpp>
#  include <rtc/rtcpnackresponder.hpp>
#  include <rtc/rtcpsrreporter.hpp>
#  include <rtc/rtppacketizationconfig.hpp>
#  include <rtc/nalunit.hpp>
#  ifdef _WIN32
#    include <mfapi.h>
#    include <mfidl.h>
#    include <mfreadwrite.h>
#    include <mferror.h>
#    include <codecapi.h>
#    include <wmcodecdsp.h>
#    pragma comment(lib, "mfplat.lib")
#    pragma comment(lib, "mfuuid.lib")
#    pragma comment(lib, "mfreadwrite.lib")
#    pragma comment(lib, "wmcodecdspuuid.lib")
#  endif
#endif
#ifdef PF_EMBED_WEB
#  include "EmbeddedWeb.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <thread>

using json = nlohmann::json;

namespace pf {

// Move messages older than this read as zero axes (phone sleep / WiFi drop
// must never leave the camera flying).
static constexpr int64_t kMoveStaleMs = 500;
static constexpr int     kMaxPinTries = 3;

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// First private IPv4 of an up, non-loopback adapter (for the URL in the UI).
static std::string lanIPv4() {
#ifdef _WIN32
    ULONG sz = 16 * 1024;
    std::vector<char> buf(sz);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                             GAA_FLAG_SKIP_DNS_SERVER, nullptr, addrs, &sz) != NO_ERROR)
        return "127.0.0.1";
    std::string fallback;
    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            unsigned b0 = sa->sin_addr.S_un.S_un_b.s_b1;
            unsigned b1 = sa->sin_addr.S_un.S_un_b.s_b2;
            bool priv = b0 == 192 || b0 == 10 || (b0 == 172 && b1 >= 16 && b1 <= 31);
            if (priv) return ip;
            if (fallback.empty()) fallback = ip;
        }
    }
    return fallback.empty() ? "127.0.0.1" : fallback;
#else
    return "127.0.0.1";
#endif
}

struct RemoteServerImpl {
    mg_context* ctx = nullptr;
    int         listenPort = 0;
    std::string pinCode;
    std::string ip;
    bool        forceDiskWeb = false;

    // ---- input state (server threads write, main loop reads) --------------
    std::atomic<float> f{0}, s{0}, u{0}, yaw{0}, pit{0};
    std::atomic<bool>  boost{false};
    std::atomic<int64_t> lastMoveMs{0};

    std::mutex             cmdMx;
    std::vector<RemoteCmd> cmds;

    // ---- clients -----------------------------------------------------------
    struct Client { 
        bool authed = false; 
        int badTries = 0; 
        bool stream = false; 
        bool webrtc = false;
#ifdef PF_REMOTE_WEBRTC
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> videoTrack;
#endif
    };
    std::mutex                            clMx;
    std::map<mg_connection*, Client>      clients;   // keyed by WS connection
    std::atomic<int>                      authedCount{0};

    int64_t lastStateMs = 0;   // publishState throttle (main thread only)

    // ---- viewport stream (encode thread + latest-frame slot) --------------
    std::atomic<int>  streamSubs{0};    // authed clients with stream=true
    std::atomic<int>  webrtcSubs{0};    // authed clients with webrtc=true
    std::atomic<bool> encBusy{false};   // readback gate: frame in flight
    std::atomic<int>  streamW{1280}, streamFps{15}, streamQ{70};
    int64_t           lastFrameMs = 0;  // wantFrame() throttle (main thread only)

    std::thread             encThread;
    std::mutex              encMx;
    std::condition_variable encCv;
    std::vector<uint8_t>    encBuf;     // pending RGB frame (bottom-up)
    int  encW = 0, encH = 0;
    bool encPending = false, encQuit = false;

    void encodeLoop() {
#ifdef PF_REMOTE_STREAM
        tjhandle tj = tjInitCompress();
#endif

#if defined(PF_REMOTE_WEBRTC) && defined(_WIN32)
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IMFTransform* pMFEncoder = nullptr;
        bool mfInit = false;
        int mfW = 0, mfH = 0;
        uint64_t mfFrameTime = 0;
        std::vector<uint8_t> nv12Buf;

        auto initMF = [&](int w, int h) {
            if (mfInit && w == mfW && h == mfH) return true;
            if (pMFEncoder) {
                pMFEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
                pMFEncoder->Release();
                pMFEncoder = nullptr;
            }
            mfW = w; mfH = h;
            
            if (FAILED(MFStartup(MF_VERSION))) {
                pf::logError("MFStartup failed");
                return false;
            }
            if (FAILED(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pMFEncoder)))) {
                pf::logError("CoCreateInstance H264 Encoder failed");
                return false;
            }
            
            ICodecAPI* pCodecApi = nullptr;
            if (SUCCEEDED(pMFEncoder->QueryInterface(IID_PPV_ARGS(&pCodecApi)))) {
                VARIANT var;
                VariantInit(&var);
                var.vt = VT_BOOL;
                var.boolVal = VARIANT_TRUE;
                pCodecApi->SetValue(&CODECAPI_AVEncCommonLowLatency, &var);
                pCodecApi->SetValue(&CODECAPI_AVEncCommonRealTime, &var);

                var.vt = VT_UI4;
                var.ulVal = 30; // 30 frames GOP (1 keyframe per second at 30fps)
                pCodecApi->SetValue(&CODECAPI_AVEncMPVGOPSize, &var);
                pCodecApi->Release();
            }
            
            IMFMediaType* pOutType = nullptr;
            MFCreateMediaType(&pOutType);
            pOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
            pOutType->SetUINT32(MF_MT_AVG_BITRATE, 2000000);
            MFSetAttributeSize(pOutType, MF_MT_FRAME_SIZE, w, h);
            MFSetAttributeRatio(pOutType, MF_MT_FRAME_RATE, 30, 1);
            pOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeRatio(pOutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (FAILED(pMFEncoder->SetOutputType(0, pOutType, 0))) { pOutType->Release(); return false; }
            pOutType->Release();

            IMFMediaType* pInType = nullptr;
            MFCreateMediaType(&pInType);
            pInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pInType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            MFSetAttributeSize(pInType, MF_MT_FRAME_SIZE, w, h);
            MFSetAttributeRatio(pInType, MF_MT_FRAME_RATE, 30, 1);
            pInType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeRatio(pInType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (FAILED(pMFEncoder->SetInputType(0, pInType, 0))) { pInType->Release(); return false; }
            pInType->Release();

            pMFEncoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
            pMFEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
            pMFEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
            mfInit = true;
            return true;
        };
#endif

        std::vector<uint8_t> rgb;
        while (true) {
            int w, h;
            {
                std::unique_lock<std::mutex> lk(encMx);
                encCv.wait(lk, [&] { return encPending || encQuit; });
                if (encQuit) break;
                rgb.swap(encBuf);
                w = encW; h = encH;
                encPending = false;
            }

            int nStream = streamSubs.load();
            int nWebrtc = webrtcSubs.load();

#ifdef PF_REMOTE_STREAM
            if (nStream > 0) {
                unsigned char* jpg = nullptr;
                unsigned long  jpgSz = 0;
                if (tj && tjCompress2(tj, rgb.data(), w, w * 3, h, TJPF_RGB,
                                      &jpg, &jpgSz, TJSAMP_420, streamQ.load(),
                                      TJFLAG_BOTTOMUP | TJFLAG_FASTDCT) == 0) {
                    std::lock_guard<std::mutex> g(clMx);
                    for (auto& kv : clients) {
                        if (kv.second.authed && kv.second.stream) {
                            mg_lock_connection(kv.first);
                            mg_websocket_write(kv.first, MG_WEBSOCKET_OPCODE_BINARY, (const char*)jpg, jpgSz);
                            mg_unlock_connection(kv.first);
                        }
                    }
                }
                if (jpg) tjFree(jpg);
            }
#endif

#if defined(PF_REMOTE_WEBRTC) && defined(_WIN32)
            if (nWebrtc > 0 && initMF(w, h)) {
                // Convert RGB to NV12
                int nv12Size = w * h + (w * h) / 2;
                if (nv12Buf.size() < nv12Size) nv12Buf.resize(nv12Size);
                uint8_t* yPlane = nv12Buf.data();
                uint8_t* uvPlane = nv12Buf.data() + w * h;
                
                for (int y = 0; y < h; ++y) {
                    const uint8_t* srcRow = rgb.data() + (h - 1 - y) * w * 3;
                    uint8_t* dstY = yPlane + y * w;
                    uint8_t* dstUV = uvPlane + (y / 2) * w;
                    
                    for (int x = 0; x < w; ++x) {
                        int r = srcRow[x * 3 + 0];
                        int g = srcRow[x * 3 + 1];
                        int b = srcRow[x * 3 + 2];
                        
                        int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                        dstY[x] = (uint8_t)std::clamp(Y, 0, 255);
                        
                        if (y % 2 == 0 && x % 2 == 0) {
                            int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                            int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                            dstUV[x] = (uint8_t)std::clamp(U, 0, 255);
                            dstUV[x + 1] = (uint8_t)std::clamp(V, 0, 255);
                        }
                    }
                }

                IMFSample* pSample = nullptr;
                MFCreateSample(&pSample);
                IMFMediaBuffer* pBuffer = nullptr;
                MFCreateMemoryBuffer(nv12Size, &pBuffer);
                BYTE* pData = nullptr;
                pBuffer->Lock(&pData, nullptr, nullptr);
                memcpy(pData, nv12Buf.data(), nv12Size);
                pBuffer->Unlock();
                pBuffer->SetCurrentLength(nv12Size);
                pSample->AddBuffer(pBuffer);
                pSample->SetSampleTime(mfFrameTime);
                pSample->SetSampleDuration(10000000 / 30);
                mfFrameTime += 10000000 / 30;

                HRESULT hr = pMFEncoder->ProcessInput(0, pSample, 0);
                pBuffer->Release();
                pSample->Release();

                if (SUCCEEDED(hr)) {
                    while (true) {
                        MFT_OUTPUT_STREAM_INFO info = {};
                        pMFEncoder->GetOutputStreamInfo(0, &info);
                        
                        DWORD cbSize = info.cbSize;
                        if (cbSize == 0) cbSize = w * h * 3;
                        
                        IMFSample* outSample = nullptr;
                        IMFMediaBuffer* outBuffer = nullptr;
                        MFT_OUTPUT_DATA_BUFFER outputData = {};
                        
                        if ((info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0) {
                            if (SUCCEEDED(MFCreateSample(&outSample)) && SUCCEEDED(MFCreateMemoryBuffer(cbSize, &outBuffer))) {
                                outSample->AddBuffer(outBuffer);
                                outputData.pSample = outSample;
                            }
                            if (outBuffer) outBuffer->Release();
                        }
                        
                        DWORD status = 0;
                        hr = pMFEncoder->ProcessOutput(0, 1, &outputData, &status);
                        
                        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                            if (outputData.pSample) outputData.pSample->Release();
                            break;
                        }
                        
                        if (FAILED(hr)) {
                            pf::logError("MF ProcessOutput failed");
                            if (outputData.pSample) outputData.pSample->Release();
                            break;
                        }
                        
                        if (SUCCEEDED(hr)) {
                            if (outputData.pSample) {
                                IMFMediaBuffer* outBuf = nullptr;
                                outputData.pSample->ConvertToContiguousBuffer(&outBuf);
                                if (outBuf) {
                                    BYTE* outData = nullptr;
                                    DWORD outLen = 0;
                                    if (SUCCEEDED(outBuf->Lock(&outData, nullptr, &outLen))) {
                                        static bool loggedFirst = false;
                                        if (!loggedFirst && outLen >= 4) {
                                            loggedFirst = true;
                                            pf::logInfo("H.264 First frame bytes: " + 
                                                std::to_string(outData[0]) + " " + 
                                                std::to_string(outData[1]) + " " + 
                                                std::to_string(outData[2]) + " " + 
                                                std::to_string(outData[3]));
                                        }
                                        
                                        // Send frame to WebRTC clients
                                        std::lock_guard<std::mutex> g(clMx);
                                        for (auto& kv : clients) {
                                            if (kv.second.authed && kv.second.webrtc && kv.second.videoTrack) {
                                                try {
                                                    if (kv.second.videoTrack->isOpen()) {
                                                        kv.second.videoTrack->sendFrame(reinterpret_cast<const std::byte*>(outData), outLen, rtc::FrameInfo(static_cast<uint32_t>(nowMs() * 90)));
                                                    }
                                                } catch (const std::exception& e) {
                                                    pf::logError("WebRTC sendFrame exception: " + std::string(e.what()));
                                                } catch (...) {
                                                    pf::logError("WebRTC sendFrame unknown exception");
                                                }
                                            }
                                        }
                                        outBuf->Unlock();
                                    }
                                    outBuf->Release();
                                }
                                outputData.pSample->Release();
                            }
                            if (outputData.pEvents) outputData.pEvents->Release();
                        } else {
                            break;
                        }
                    }
                }
            }
#endif
            encBusy = false;
        }

#ifdef PF_REMOTE_STREAM
        if (tj) tjDestroy(tj);
#endif
#if defined(PF_REMOTE_WEBRTC) && defined(_WIN32)
        if (pMFEncoder) {
            pMFEncoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            pMFEncoder->Release();
        }
        MFShutdown();
        CoUninitialize();
#endif
    }

    void recountLocked() {
        int a = 0, s = 0, wr = 0;
        for (auto& kv : clients) {
            if (kv.second.authed) ++a;
            if (kv.second.authed && kv.second.stream) ++s;
            if (kv.second.authed && kv.second.webrtc) ++wr;
        }
        authedCount = a;
        streamSubs  = s;
        webrtcSubs  = wr;
    }

    void zeroAxes() {
        f = 0; s = 0; u = 0; yaw = 0; pit = 0; boost = false;
    }

    void sendTo(mg_connection* c, const std::string& msg) {
        // WS writes can race the server's own worker threads; civetweb
        // requires the per-connection lock for cross-thread writes.
        mg_lock_connection(c);
        mg_websocket_write(c, MG_WEBSOCKET_OPCODE_TEXT, msg.c_str(), msg.size());
        mg_unlock_connection(c);
    }

    // Returns 1 to keep the socket open, 0 to close it.
    int onMessage(mg_connection* conn, const char* data, size_t len) {
        json m = json::parse(std::string_view(data, len), nullptr, false);
        if (m.is_discarded() || !m.is_object()) return 1;   // ignore garbage
        const std::string t = m.value("t", "");

        std::unique_lock<std::mutex> lk(clMx);
        auto it = clients.find(conn);
        if (it == clients.end()) return 0;
        Client& cl = it->second;

        if (t == "hello") {
            if (m.value("pin", "") == pinCode) {
                cl.authed = true;
                recountLocked();
                lk.unlock();
                sendTo(conn, R"({"t":"hello_ok"})");
                pf::logInfo("Remote: client authenticated");
            } else {
                bool drop = ++cl.badTries >= kMaxPinTries;
                lk.unlock();
                sendTo(conn, R"({"t":"hello_bad"})");
                if (drop) { pf::logWarn("Remote: client dropped (bad PIN x3)"); return 0; }
            }
            return 1;
        }

        if (!cl.authed) return 0;    // anything else before hello -> drop

        if (t == "stream") {
            cl.stream = m.value("on", 0) != 0;
            if (m.contains("w"))   streamW   = std::clamp(m.value("w", 1280), 320, 1920);
            if (m.contains("fps")) streamFps = std::clamp(m.value("fps", 15), 5, 30);
            if (m.contains("q"))   streamQ   = std::clamp(m.value("q", 70), 30, 90);
            if (!cl.stream && cl.webrtc) cl.webrtc = false; // client disabled stream
            recountLocked();
            return 1;
        }

#ifdef PF_REMOTE_WEBRTC
        if (t == "webrtc_offer") {
            try {
                cl.webrtc = true;
                cl.stream = false; // disable jpeg
                
                rtc::Configuration config;
                config.iceServers.emplace_back("stun:stun.l.google.com:19302");
                cl.pc = std::make_shared<rtc::PeerConnection>(config);
                
                rtc::Description::Video video("video");
                video.addH264Codec(96);
                video.addSSRC(1111, "video-stream");
                cl.videoTrack = cl.pc->addTrack(video);

                auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
                    1111, "video-stream", 96, rtc::H264RtpPacketizer::ClockRate);
                
                auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);
                packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtpConfig));
                packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
                cl.videoTrack->setMediaHandler(packetizer);

                cl.pc->onLocalDescription([conn, this](rtc::Description description) {
                    json answer = {
                        {"t", "webrtc_answer"},
                        {"type", description.typeString()},
                        {"sdp", description.generateSdp()}
                    };
                    sendTo(conn, answer.dump());
                });

                cl.pc->onLocalCandidate([conn, this](rtc::Candidate candidate) {
                    json cand = {
                        {"t", "webrtc_candidate"},
                        {"candidate", candidate.candidate()},
                        {"sdpMid", candidate.mid()}
                    };
                    sendTo(conn, cand.dump());
                });

                cl.pc->setRemoteDescription(rtc::Description(m.value("sdp", ""), m.value("type", "")));
            } catch (const std::exception& e) {
                pf::logError("WebRTC offer exception: " + std::string(e.what()));
            } catch (...) {
                pf::logError("WebRTC offer unknown exception");
            }
            recountLocked();
            return 1;
        }

        if (t == "webrtc_ice") {
            if (cl.pc) {
                std::string candidate = m.value("candidate", "");
                std::string mid = m.value("sdpMid", "");
                cl.pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
            }
            return 1;
        }
#endif
        lk.unlock();

        if (t == "move") {
            auto ax = [&](const char* k) {
                float v = m.value(k, 0.0f);
                return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            };
            f = ax("f"); s = ax("s"); u = ax("u");
            yaw = ax("yaw"); pit = ax("pit");
            boost = m.value("boost", 0) != 0;
            lastMoveMs = nowMs();
        } else if (t == "cmd" || t == "set") {
            RemoteCmd c;
            if (t == "set") {
                const std::string k = m.value("k", "");
                if (k.empty()) return 1;
                c.name = "set." + k;
            } else {
                c.name = m.value("n", "");
                if (c.name.empty()) return 1;
            }
            // v: number, bool (as 0/1) or [r,g,b] array.
            if (m.contains("v")) {
                const auto& v = m["v"];
                if (v.is_boolean())     c.value = v.get<bool>() ? 1.0f : 0.0f;
                else if (v.is_number()) c.value = v.get<float>();
                else if (v.is_array() && v.size() >= 3) {
                    c.hasVec = true;
                    for (int i = 0; i < 3; ++i)
                        if (v[i].is_number()) c.vec[i] = v[i].get<float>();
                }
            }
            std::lock_guard<std::mutex> g(cmdMx);
            if (cmds.size() < 64) cmds.push_back(std::move(c));
        }
        return 1;
    }

};

// ---- civetweb C callbacks --------------------------------------------------
static int embedded_handler(struct mg_connection* conn, void* fn_data) {
    const struct mg_request_info* ri = mg_get_request_info(conn);
    std::string uri = ri->request_uri;
    // Strip query parameters
    size_t q = uri.find('?');
    if (q != std::string::npos) uri = uri.substr(0, q);
    if (uri == "/") uri = "/index.html";
    auto files = getEmbeddedWebFiles();
    auto it = files.find(uri);
    if (it != files.end()) {
        const unsigned char* data = it->second.first;
        size_t len = it->second.second;
        // Simple MIME type mapping
        std::string mime = "application/octet-stream";
        auto ends_with = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (ends_with(uri, ".html")) mime = "text/html";
        else if (ends_with(uri, ".js")) mime = "application/javascript";
        else if (ends_with(uri, ".css")) mime = "text/css";
        else if (ends_with(uri, ".png")) mime = "image/png";
        else if (ends_with(uri, ".svg")) mime = "image/svg+xml";
        else if (ends_with(uri, ".json")) mime = "application/json";
        mg_send_http_ok(conn, mime.c_str(), len);
        mg_write(conn, reinterpret_cast<const char*>(data), len);
        return 1; // Handled
    }
    return 0; // Not handled
}
static int wsConnect(const mg_connection*, void*) { return 0; }   // accept all

static void wsReady(mg_connection* conn, void* ud) {
    auto* impl = static_cast<RemoteServerImpl*>(ud);
    std::lock_guard<std::mutex> g(impl->clMx);
    impl->clients[conn] = {};
}

static int wsData(mg_connection* conn, int bits, char* data, size_t len, void* ud) {
    if ((bits & 0x0F) != MG_WEBSOCKET_OPCODE_TEXT) return 1;      // text only
    auto* impl = static_cast<RemoteServerImpl*>(ud);
    return impl->onMessage(conn, data, len);
}

static void wsClose(const mg_connection* conn, void* ud) {
    // Embedded handler does not need special close handling
    auto* impl = static_cast<RemoteServerImpl*>(ud);
    std::lock_guard<std::mutex> g(impl->clMx);
    impl->clients.erase(const_cast<mg_connection*>(conn));
    impl->recountLocked();
    if (impl->authedCount == 0) impl->zeroAxes();                 // no runaway fly
    pf::logInfo("Remote: client disconnected (" + std::to_string(impl->authedCount.load()) + " left)");
}

// ---- public API -------------------------------------------------------------
RemoteServer::RemoteServer() : impl_(new RemoteServerImpl) {}

void RemoteServer::setForceDiskWeb(bool enable) {
    if (impl_) impl_->forceDiskWeb = enable;
}
RemoteServer::~RemoteServer() { stop(); delete impl_; }

bool RemoteServer::available() { return true; }

bool RemoteServer::start(int port, const std::string& webRoot) {
    stop();
    static bool libInit = false;
    if (!libInit) { mg_init_library(0); libInit = true; }

    // Fresh pairing PIN each start.
    std::random_device rd;
    char pinBuf[8];
    snprintf(pinBuf, sizeof(pinBuf), "%04u", rd() % 10000u);
    impl_->pinCode = pinBuf;

    char portBuf[16];
    snprintf(portBuf, sizeof(portBuf), "%d", port);
    const char* options[] = {
        "listening_ports",          portBuf,
#ifdef PF_EMBED_WEB
        // No document_root for embedded mode
#else
        "document_root",            webRoot.c_str(),
#endif
        "num_threads",              "4",
        "enable_directory_listing", "no",
        nullptr
    };
    impl_->ctx = mg_start(nullptr, nullptr, options);
    // If forcing disk web, skip embedding handler
    if (PF_EMBED_WEB && !impl_->forceDiskWeb) {
        mg_set_request_handler(impl_->ctx, "/*", embedded_handler, impl_);
    }
    if (!impl_->ctx) {
        pf::logError("Remote: failed to start server on port " + std::to_string(port) + " (in use?)");
        return false;
    }
    mg_set_websocket_handler(impl_->ctx, "/ws",
                             wsConnect, wsReady, wsData, wsClose, impl_);
#ifdef PF_EMBED_WEB
    // Register handler to serve embedded assets
    mg_set_request_handler(impl_->ctx, "/*", embedded_handler, impl_);
#endif
    impl_->listenPort = port;
    impl_->ip = lanIPv4();
    // Store flag for runtime disk serving (already set via setter)
    // No additional action needed here
#if defined(PF_REMOTE_STREAM) || defined(PF_REMOTE_WEBRTC)
    impl_->encQuit = false;
    impl_->encThread = std::thread([this] { impl_->encodeLoop(); });
#endif
    pf::logInfo("Remote: serving " + (PF_EMBED_WEB ? "embedded resources" : webRoot) + " at " + url() +
                " (PIN " + impl_->pinCode + ")");
    return true;
}

void RemoteServer::stop() {
    if (!impl_->ctx) return;
#if defined(PF_REMOTE_STREAM) || defined(PF_REMOTE_WEBRTC)
    // Encoder writes to connections; it must be gone before mg_stop.
    {
        std::lock_guard<std::mutex> g(impl_->encMx);
        impl_->encQuit = true;
    }
    impl_->encCv.notify_all();
    if (impl_->encThread.joinable()) impl_->encThread.join();
#endif
    mg_stop(impl_->ctx);
    impl_->ctx = nullptr;
    {
        std::lock_guard<std::mutex> g(impl_->clMx);
        impl_->clients.clear();
    }
    impl_->authedCount = 0;
    impl_->streamSubs  = 0;
    impl_->encBusy     = false;
    impl_->zeroAxes();
}

bool RemoteServer::running() const { return impl_->ctx != nullptr; }
int  RemoteServer::port()    const { return impl_->listenPort; }
std::string RemoteServer::pin() const { return impl_->pinCode; }

std::string RemoteServer::url() const {
    return "http://" + impl_->ip + ":" + std::to_string(impl_->listenPort) + "/";
}

int RemoteServer::clientCount() const { return impl_->authedCount.load(); }

bool RemoteServer::inputActive() const {
    return impl_->authedCount > 0 && (nowMs() - impl_->lastMoveMs) < kMoveStaleMs;
}

float RemoteServer::fwd()       const { return inputActive() ? impl_->f.load()   : 0.0f; }
float RemoteServer::strafe()    const { return inputActive() ? impl_->s.load()   : 0.0f; }
float RemoteServer::up()        const { return inputActive() ? impl_->u.load()   : 0.0f; }
float RemoteServer::yawRate()   const { return inputActive() ? impl_->yaw.load() : 0.0f; }
float RemoteServer::pitchRate() const { return inputActive() ? impl_->pit.load() : 0.0f; }
bool  RemoteServer::boost()     const { return inputActive() && impl_->boost.load(); }

std::vector<RemoteCmd> RemoteServer::consumeCommands() {
    std::vector<RemoteCmd> out;
    std::lock_guard<std::mutex> g(impl_->cmdMx);
    out.swap(impl_->cmds);
    return out;
}

void RemoteServer::publishState(float fps, uint64_t points, const float pos[3],
                                bool uiVisible, const std::string& file) {
    if (!impl_->ctx || impl_->authedCount == 0) return;
    int64_t t = nowMs();
    if (t - impl_->lastStateMs < 200) return;     // ~5 Hz
    impl_->lastStateMs = t;

    json st = {
        {"t",   "state"},
        {"fps", (int)fps},
        {"pts", points},
        {"pos", {pos[0], pos[1], pos[2]}},
        {"ui",  uiVisible},
        {"file", file}
    };
    const std::string msg = st.dump();

    std::lock_guard<std::mutex> g(impl_->clMx);
    for (auto& kv : impl_->clients)
        if (kv.second.authed) impl_->sendTo(kv.first, msg);
}

void RemoteServer::publishConfig(const RemoteConfig& c) {
    if (!impl_->ctx || impl_->authedCount == 0) return;
    json j = {
        {"t", "cfg"},
        {"quality", c.qualityIdx}, {"pointSize", c.pointSize},
        {"sse", c.sseBudget}, {"colorMode", c.colorMode},
        {"solidColor", {c.solidColor[0], c.solidColor[1], c.solidColor[2]}},
        {"edl", c.enableEDL}, {"edlStrength", c.edlStrength}, {"edlRadius", c.edlRadius},
        {"gpuBudget", c.gpuBudgetMB}, {"uploads", c.uploadsPerFrame},
        {"round", c.roundPoints}, {"attenuate", c.attenuate},
        {"background", {c.clearColor[0], c.clearColor[1], c.clearColor[2]}},
        {"ortho", c.isOrtho}, {"orthoSize", c.orthoSize}, {"speed", c.camSpeed},
        {"stereo", c.stereo}, {"eyeSep", c.eyeSep}, {"focalDist", c.focalDist},
        {"tool", c.toolMode}, {"clip", c.clipEnabled},
        {"clipMin", {c.clipMin[0], c.clipMin[1], c.clipMin[2]}},
        {"clipMax", {c.clipMax[0], c.clipMax[1], c.clipMax[2]}},
        {"clipExt", c.clipExt},
        {"measureTotal", c.measureTotal},
        {"ui", c.uiVisible}, {"stats", c.statsOverlay},
        {"fullscreen", c.fullscreen}, {"darkTheme", c.darkTheme},
        {"recent", c.recentDirs}, {"file", c.loadedFile},
        {"pts", c.pointCount}, {"nodes", c.nodeCount}, {"cubeSize", c.cubeSize},
        {"streamAvailable", c.streamAvailable},
        {"webrtcAvailable", c.webrtcAvailable},
        {"preferredStream", c.preferredStream}
    };
    json mp = json::array();
    for (const auto& p : c.measurePts) mp.push_back({p[0], p[1], p[2]});
    j["measurePts"] = std::move(mp);

    const std::string msg = j.dump();
    std::lock_guard<std::mutex> g(impl_->clMx);
    for (auto& kv : impl_->clients)
        if (kv.second.authed) impl_->sendTo(kv.first, msg);
}

#if defined(PF_REMOTE_STREAM) || defined(PF_REMOTE_WEBRTC)
bool RemoteServer::streamAvailable() {
#ifdef PF_REMOTE_STREAM
    return true;
#else
    return false;
#endif
}

bool RemoteServer::webrtcAvailable() {
#ifdef PF_REMOTE_WEBRTC
    return true;
#else
    return false;
#endif
}

bool RemoteServer::wantFrame() {
    if (!impl_->ctx || impl_->encBusy) return false;
    if (impl_->streamSubs == 0 && impl_->webrtcSubs == 0) return false;
    int64_t t = nowMs();
    if (t - impl_->lastFrameMs < 1000 / impl_->streamFps.load()) return false;
    impl_->lastFrameMs = t;
    return true;
}

void RemoteServer::publishFrame(const uint8_t* rgb, int w, int h) {
    if (!impl_->ctx || w <= 0 || h <= 0) return;
    {
        std::lock_guard<std::mutex> g(impl_->encMx);
        impl_->encBuf.assign(rgb, rgb + (size_t)w * h * 3);
        impl_->encW = w;
        impl_->encH = h;
        impl_->encPending = true;
        impl_->encBusy = true;
    }
    impl_->encCv.notify_one();
}
#else
bool RemoteServer::streamAvailable() { return false; }
bool RemoteServer::webrtcAvailable() { return false; }
bool RemoteServer::wantFrame() { return false; }
void RemoteServer::publishFrame(const uint8_t*, int, int) {}
#endif

int RemoteServer::streamMaxWidth() const { return impl_->streamW.load(); }

} // namespace pf

#else // ---- stub build (civetweb / nlohmann-json missing) -------------------

namespace pf {
struct RemoteServerImpl {};
RemoteServer::RemoteServer() : impl_(nullptr) {}
RemoteServer::~RemoteServer() {}
bool RemoteServer::available() { return false; }
bool RemoteServer::start(int, const std::string&) {
    pf::logError("Remote: viewer built without PF_WITH_REMOTE");
    return false;
}
void RemoteServer::stop() {}
bool RemoteServer::running() const { return false; }
int  RemoteServer::port() const { return 0; }
std::string RemoteServer::pin() const { return ""; }
std::string RemoteServer::url() const { return ""; }
int   RemoteServer::clientCount() const { return 0; }
float RemoteServer::fwd() const { return 0; }
float RemoteServer::strafe() const { return 0; }
float RemoteServer::up() const { return 0; }
float RemoteServer::yawRate() const { return 0; }
void RemoteServer::setForceDiskWeb(bool) {}
float RemoteServer::pitchRate() const { return 0; }
bool  RemoteServer::boost() const { return false; }
bool  RemoteServer::inputActive() const { return false; }
std::vector<RemoteCmd> RemoteServer::consumeCommands() { return {}; }
void RemoteServer::publishState(float, uint64_t, const float*, bool, const std::string&) {}
void RemoteServer::publishConfig(const RemoteConfig&) {}
bool RemoteServer::streamAvailable() { return false; }
bool RemoteServer::webrtcAvailable() { return false; }
bool RemoteServer::wantFrame() { return false; }
void RemoteServer::publishFrame(const uint8_t*, int, int) {}
int  RemoteServer::streamMaxWidth() const { return 0; }
} // namespace pf

#endif
