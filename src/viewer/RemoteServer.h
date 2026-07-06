#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pf {

// One-shot command or setting change from the web remote (drained by the main
// loop per frame). Commands: frame, reset_view, preset1/3/7, hideui, shot,
// fullscreen, stereo, measure, measure_undo, measure_clear, clip_tool,
// clip_reset, loadrecent (value = index), pointsize+/-, speed, ortho.
// Settings arrive as name "set.<key>" with value (numbers/bools as float) or
// vec (colors, clip planes) — keys mirror the Properties panel, see the
// dispatch table in main.cpp.
struct RemoteCmd {
    std::string name;
    float       value  = 0.0f;
    float       vec[3] = {0, 0, 0};
    bool        hasVec = false;
};

// Full viewer state pushed to clients as {"t":"cfg",...} — everything the
// phone UI can display/edit. Built by the main loop (~1 Hz + after remote
// changes), serialized inside RemoteServer.
struct RemoteConfig {
    // display
    int   qualityIdx = 1;
    float pointSize = 2.0f, sseBudget = 1.5f;
    int   colorMode = 0;
    float solidColor[3] = {1, 1, 1};
    bool  enableEDL = false; float edlStrength = 1, edlRadius = 1.5f;
    int   gpuBudgetMB = 1024, uploadsPerFrame = 32;
    bool  roundPoints = true, attenuate = false;
    float clearColor[3] = {0, 0, 0};
    // camera
    bool  isOrtho = false; float orthoSize = 100;
    float camSpeed = 1;
    bool  stereo = false; float eyeSep = 0.05f, focalDist = 10;
    // tools
    int   toolMode = 0;              // 0 nav, 1 measure, 2 clip
    bool  clipEnabled = false;
    float clipMin[3] = {0,0,0}, clipMax[3] = {0,0,0};
    float clipExt = 0;               // slider range for clip planes
    std::vector<std::array<double,3>> measurePts;
    double measureTotal = 0;
    // ui
    bool uiVisible = true, statsOverlay = false, fullscreen = false, darkTheme = true;
    // files
    std::vector<std::string> recentDirs;
    std::string loadedFile;
    uint64_t pointCount = 0; uint64_t nodeCount = 0; float cubeSize = 0;
    // stream
    bool streamAvailable = false;
    bool webrtcAvailable = false;
    int preferredStream = 0; // 0: JPEG, 1: WebRTC
    // camera bookmarks for the loaded cloud (names only; recall by index via
    // {"t":"cmd","n":"bookmark_goto","v":<idx>}, save via "bookmark_add",
    // delete via "bookmark_del")
    std::vector<std::string> bookmarks;
};

struct RemoteServerImpl;   // defined in RemoteServer.cpp (civetweb kept out of headers)

// Embedded HTTP + WebSocket server (civetweb) that serves the React control
// page from `webRoot` and accepts phone input on /ws. Mirrors the
// SerialController pattern: server threads write state, the main loop polls
// axes + drains commands once per frame. No GL/SDL/ImGui dependency.
//
// Protocol (JSON text frames), see docs/ai_handoff.md:
//   -> {"t":"hello","pin":"1234"}       first message; 3 bad tries -> close
//   -> {"t":"move","f":..,"s":..,"u":..,"yaw":..,"pit":..,"boost":0|1}  30 Hz
//   -> {"t":"cmd","n":"frame"} / {"t":"cmd","n":"speed","v":2.5}
//   <- {"t":"hello_ok"} | {"t":"hello_bad"}
//   <- {"t":"state","fps":..,"pts":..,"pos":[..],"ui":..,"file":".."}   5 Hz
//
// Safety: axes read as zero when no authed client has sent a move message in
// the last 500 ms (phone sleep / WiFi drop must never leave the camera flying).
//
// Compiled as a no-op stub when PF_WITH_REMOTE is not defined (civetweb or
// nlohmann-json missing), same convention as PF_WITH_LAS readers.
class RemoteServer {
public:
    RemoteServer();
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    static bool available();                 // false in the stub build

    // (Re)start on `port`, serving static files from `webRoot` (the `web/`
    // dir beside the exe). Generates a fresh 4-digit PIN each start.
    bool start(int port, const std::string& webRoot);
    void stop();
    void setForceDiskWeb(bool enable);
    bool running() const;

    int         port() const;
    std::string pin() const;                 // "0000".."9999"
    std::string url() const;                 // http://<lan-ipv4>:<port>/
    int         clientCount() const;         // currently authed sockets

    // ---- input (polled by the main loop each frame) -----------------------
    // Axes in [-1,1]; forced to 0 when stale (see safety note above).
    float fwd()    const;
    float strafe() const;
    float up()     const;
    float yawRate()   const;
    float pitchRate() const;
    bool  boost()  const;
    bool  inputActive() const;               // fresh move data this instant

    std::vector<RemoteCmd> consumeCommands();

    // ---- output (called by the main loop, throttled to ~5 Hz inside) ------
    void publishState(float fps, uint64_t points, const float pos[3],
                      bool uiVisible, const std::string& file);

    // Push the full viewer config to authed clients ({"t":"cfg",...}).
    // Call after applying remote changes (instant echo) and ~1 Hz otherwise.
    void publishConfig(const RemoteConfig& cfg);

    // ---- viewport streaming (JPEG binary frames on the same socket) -------
    // Client opts in with {"t":"stream","on":1[,"w":1280,"fps":15,"q":70]}.
    // The main loop asks wantFrame() each frame; true only when a subscriber
    // exists, the encoder is idle and the frame interval elapsed — so the GPU
    // readback cost is only paid when a frame will actually be sent.
    // publishFrame copies the pixels; TurboJPEG encode + broadcast happen on a
    // background thread. rgb is tightly packed, bottom-up (GL origin) — the
    // encoder flips. No-ops when built without PF_REMOTE_STREAM.
    static bool streamAvailable();
    bool wantFrame();
    void publishFrame(const uint8_t* rgb, int w, int h);
    int  streamMaxWidth() const;      // client-requested output width cap

    static bool webrtcAvailable();

    // ---- screenshot download (phone grabs the last capture) ---------------
    // Store the most recent screenshot (PNG bytes). Served over HTTP at
    // /shot.png?pin=<PIN> — PIN required so the viewport image is not exposed
    // to unauthenticated LAN peers. Also notifies authed WS clients with
    // {"t":"shot_ready"} so the web UI can offer the download.
    void publishShot(const std::vector<uint8_t>& png);

private:
    RemoteServerImpl* impl_;
};

} // namespace pf
