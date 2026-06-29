// pfview — PointForge streaming viewer (SDL2 + OpenGL 3.3 core + Dear ImGui).
#include <GL/glew.h>   // must precede any system GL header
#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "viewer/Camera.h"
#include "viewer/Shader.h"
#include "viewer/OctreeStore.h"
#include "viewer/PointRenderer.h"
#include "viewer/Controller.h"
#include "viewer/SerialController.h"
#include "viewer/EmbeddedShaders.h"
#include "viewer/EmbeddedImage.h"
#include "common/Log.h"
#include "common/OctreeFormat.h"
#include "common/FileDialog.h"
#include "indexer/OctreeIndexer.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace pf;

// ---- frustum culling -------------------------------------------------------
struct Frustum { std::array<glm::vec4, 6> planes; };

static Frustum extractFrustum(const glm::mat4& m) {
    Frustum f;
    // rows of the matrix (glm is column-major; m[col][row])
    auto row = [&](int r) { return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    f.planes[0] = r3 + r0; // left
    f.planes[1] = r3 - r0; // right
    f.planes[2] = r3 + r1; // bottom
    f.planes[3] = r3 - r1; // top
    f.planes[4] = r3 + r2; // near
    f.planes[5] = r3 - r2; // far
    for (auto& p : f.planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 0) p /= len;
    }
    return f;
}

static bool aabbVisible(const Frustum& f, const glm::vec3& mn, const glm::vec3& mx) {
    for (const auto& p : f.planes) {
        // positive vertex (farthest along plane normal)
        glm::vec3 pv(p.x >= 0 ? mx.x : mn.x,
                     p.y >= 0 ? mx.y : mn.y,
                     p.z >= 0 ? mx.z : mn.z);
        if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f) return false; // fully outside
    }
    return true;
}

static GLuint loadTextureBMP(SDL_RWops* rw, int freeRw) {
    if (!rw) return 0;
    SDL_Surface* surf = SDL_LoadBMP_RW(rw, freeRw);
    if (!surf) return 0;
    
    SDL_Surface* formatted = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surf);
    if (!formatted) return 0;

    Uint32* pixels = (Uint32*)formatted->pixels;
    int count = formatted->w * formatted->h;
    for (int i = 0; i < count; ++i) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(pixels[i], formatted->format, &r, &g, &b, &a);
        if (r == 255 && g == 0 && b == 255) { 
            pixels[i] = SDL_MapRGBA(formatted->format, 255, 255, 255, 0); 
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, formatted->w, formatted->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, formatted->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    SDL_FreeSurface(formatted);
    return tex;
}

// Save the current GL framebuffer to a BMP (no image-library dependency).
static bool saveScreenshotBMP(const char* path, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // GL is bottom-up; SDL surface is top-down -> flip rows.
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; ++y)
        std::memcpy((unsigned char*)s->pixels + (size_t)y * s->pitch,
                    px.data() + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    bool ok = SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

int main(int argc, char** argv) {
    std::string initialDir = "";
    if (argc >= 2) initialDir = argv[1];

    // ---- SDL + GL context -------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        logError(std::string("SDL_Init: ") + SDL_GetError()); return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int winW = 1280, winH = 720;
    SDL_Window* window = SDL_CreateWindow(
        "ViitorX PointCloud Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { logError(std::string("CreateWindow: ") + SDL_GetError()); return 1; }

    SDL_Surface* icon = SDL_LoadBMP_RW(SDL_RWFromConstMem(kVxBmp, (int)kVxBmpLen), 1);
    if (icon) {
        SDL_SetColorKey(icon, SDL_TRUE, SDL_MapRGB(icon->format, 255, 0, 255));
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { logError(std::string("GL context: ") + SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { logError("glewInit failed"); return 1; }

    // ---- ImGui ------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Snapshot the unscaled style so DPI scaling can be re-applied from a clean
    // base each time (ScaleAllSizes is cumulative and must not stack).
    const ImGuiStyle baseStyle = ImGui::GetStyle();
    // Auto-detect the display DPI (96 dpi = 1.0x). Used as the default UI scale
    // unless the config file overrides it.
    float autoUiScale = 1.0f;
    {
        float ddpi = 96.0f;
        if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &ddpi, nullptr, nullptr) == 0 && ddpi > 0.0f)
            autoUiScale = std::clamp(ddpi / 96.0f, 0.5f, 4.0f);
    }
    bool darkTheme = true;
    // Re-apply style from the clean base: theme colours, then DPI size scaling.
    auto applyUiScale = [&](float s) {
        ImGuiStyle& st = ImGui::GetStyle();
        st = baseStyle;                      // restores default (dark) sizes + colours
        if (!darkTheme) ImGui::StyleColorsLight(&st); // override colours only
        st.ScaleAllSizes(s);
        ImGui::GetIO().FontGlobalScale = s;
    };

    // ---- assets -----------------------------------------------------------
    OctreeStore store;
    bool octreeLoaded = false;
    // (initial / auto-load happens after settings + helpers are set up below)

    // Shaders are embedded in the binary (EmbeddedShaders.h) so the viewer is a
    // single self-contained exe with no external shaders/ folder.
    Shader shader;
    if (!shader.loadFromSource(kPointVertSrc, kPointFragSrc)) {
        logError("Failed to compile embedded shaders");
        return 3;
    }

    PointRenderer renderer;
    GLuint watermarkTex = loadTextureBMP(SDL_RWFromConstMem(kVxBmp, (int)kVxBmpLen), 1);

    // ---- post-process (offscreen FBO + EDL fullscreen pass) ---------------
    Shader edlShader;
    if (!edlShader.loadFromSource(kEdlVertSrc, kEdlFragSrc)) {
        logError("Failed to compile embedded EDL shader");
        return 3;
    }
    GLuint quadVao = 0; glGenVertexArrays(1, &quadVao); // empty VAO for fullscreen triangle
    GLuint edlFbo = 0, edlColorTex = 0, edlDepthTex = 0;
    int fboW = 0, fboH = 0;
    auto ensureFbo = [&](int w, int h) {
        if (w == fboW && h == fboH && edlFbo) return;
        if (!edlFbo) glGenFramebuffers(1, &edlFbo);
        if (!edlColorTex) glGenTextures(1, &edlColorTex);
        if (!edlDepthTex) glGenTextures(1, &edlDepthTex);
        glBindTexture(GL_TEXTURE_2D, edlColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, edlDepthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, edlFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, edlColorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, edlDepthTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        fboW = w; fboH = h;
    };

    // ---- camera initial framing ------------------------------------------
    Camera cam;
    auto setupCamera = [&]() {
        const double cubeSize = store.meta().cubeSize;
        cam.position = glm::vec3(0.0f, -(float)cubeSize, 0.0f);
        cam.yaw = 0.0f; cam.pitch = 0.0f;
        cam.nearZ = (float)std::max(0.01, cubeSize / 5000.0);
        cam.farZ  = (float)(cubeSize * 8.0);
        cam.moveSpeed = (float)(cubeSize / 8.0);
    };
    if (octreeLoaded) setupCamera();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // ---- tunables (ImGui) -------------------------------------------------
    float pointSize   = 2.0f;
    float sseBudget   = 1.5f;   // pixels; smaller = more detail loaded
    int   gpuBudgetMB = 1024;
    bool  roundPoints = true;
    bool  attenuate   = false;
    int   uploadsPerFrame = 32;
    float clearColor[3] = {0.06f, 0.07f, 0.09f};
    int colorMode = 0;
    float solidColor[3] = {1.0f, 1.0f, 1.0f};
    bool enableClipping = false;
    float clipMin[3] = {-1000.0f, -1000.0f, -1000.0f};
    float clipMax[3] = {1000.0f, 1000.0f, 1000.0f};
    bool stereoSBS = false;
    float eyeSeparation = 0.05f;
    float focalDistance = 10.0f;
    float camSpeedMultiplier = 1.0f;
    float uiScale = autoUiScale;
    bool  enableEDL = false;       // eye-dome lighting post-process
    float edlStrength = 1.0f;
    float edlRadius = 1.5f;

    // ---- controller (gamepad / custom joystick) ---------------------------
    GameInput pad;
    pad.openFirst();
    bool  padEnabled  = true;
    float padLookSens = 120.0f;    // deg/sec at full stick deflection
    float padMoveSens = 1.0f;      // multiplier on cam.moveSpeed
    bool  padInvertY  = false;
    bool  uiNavMode   = false;     // true = controller drives the UI, not the camera

    // ---- custom serial controller (ESP32 joystick over Bluetooth SPP) -----
    SerialController serial;
    bool        serialEnabled = true;
    bool        serialAuto    = true;
    std::string serialMac     = "B4BFE90B6036"; // from the user's JoystickReceiver
    std::string serialPort    = "COM4";
    bool        serialNavRelease = false;       // pending ImGui activate-release

    // ---- measurement (CPU point picking, multi-segment polyline) ----------
    bool measureMode = false;
    std::vector<glm::dvec3> measurePts;   // picked vertices, world coords
    bool pendingPick = false;             // a click is waiting to be resolved this frame
    int  pickX = 0, pickY = 0;
    auto measureTotal = [&]() -> double {
        double t = 0.0;
        for (size_t i = 1; i < measurePts.size(); ++i)
            t += glm::length(measurePts[i] - measurePts[i - 1]);
        return t;
    };

    // ---- navigation (orbit / focus / zoom-to-cursor) ----------------------
    glm::vec3 pivot(0.0f);            // orbit pivot in centred space (cube centre = origin)
    bool orbitDrag = false;          // LMB held -> turntable orbit
    bool pendingFocus = false; int focusX = 0, focusY = 0;   // double-click to focus
    bool pendingZoom  = false; float zoomDelta = 0.0f; int zoomX = 0, zoomY = 0;
    bool frameAllReq  = false;       // 'F' / button -> fit whole cloud

    // ---- HUD / help -------------------------------------------------------
    bool showHelp = false;           // F1 toggles the controls overlay
    bool showStatusBar = true;       // always-on bottom status strip
    bool pendingShot = false;        // F12 / button -> save a screenshot
    int  shotCounter = 0;
    glm::dvec3 hoverWorld(0.0);      // world point under the cursor this frame
    bool hoverValid = false;

    // ---- loading (recent files / auto-load) -------------------------------
    std::vector<std::string> recentDirs;   // most-recent first, capped
    bool autoLoadLast = false;
    auto addRecent = [&](const std::string& dir) {
        recentDirs.erase(std::remove(recentDirs.begin(), recentDirs.end(), dir), recentDirs.end());
        recentDirs.insert(recentDirs.begin(), dir);
        if (recentDirs.size() > 8) recentDirs.resize(8);
    };

    auto resetSettings = [&]() {
        pointSize = 2.0f; sseBudget = 1.5f; gpuBudgetMB = 1024;
        roundPoints = true; attenuate = false; uploadsPerFrame = 32;
        clearColor[0]=0.06f; clearColor[1]=0.07f; clearColor[2]=0.09f;
        colorMode = 0; solidColor[0]=1.0f; solidColor[1]=1.0f; solidColor[2]=1.0f;
        enableClipping = false; clipMin[0]=-1000.0f; clipMin[1]=-1000.0f; clipMin[2]=-1000.0f;
        clipMax[0]=1000.0f; clipMax[1]=1000.0f; clipMax[2]=1000.0f;
        stereoSBS = false; eyeSeparation = 0.05f; focalDistance = 10.0f;
        camSpeedMultiplier = 1.0f;
        uiScale = autoUiScale;
        darkTheme = true;
        enableEDL = false; edlStrength = 1.0f; edlRadius = 1.5f;
        padEnabled = true; pad.deadzone = 0.18f; padLookSens = 120.0f;
        padMoveSens = 1.0f; padInvertY = false;
    };

    auto saveSettings = [&]() {
        FILE* f = fopen("pfview_config.txt", "w");
        if (!f) return;
        fprintf(f, "pointSize=%f\n", pointSize);
        fprintf(f, "sseBudget=%f\n", sseBudget);
        fprintf(f, "gpuBudgetMB=%d\n", gpuBudgetMB);
        fprintf(f, "uploadsPerFrame=%d\n", uploadsPerFrame);
        fprintf(f, "roundPoints=%d\n", (int)roundPoints);
        fprintf(f, "attenuate=%d\n", (int)attenuate);
        fprintf(f, "clearColor=%f,%f,%f\n", clearColor[0], clearColor[1], clearColor[2]);
        fprintf(f, "colorMode=%d\n", colorMode);
        fprintf(f, "solidColor=%f,%f,%f\n", solidColor[0], solidColor[1], solidColor[2]);
        fprintf(f, "stereoSBS=%d\n", (int)stereoSBS);
        fprintf(f, "eyeSeparation=%f\n", eyeSeparation);
        fprintf(f, "focalDistance=%f\n", focalDistance);
        fprintf(f, "enableClipping=%d\n", (int)enableClipping);
        fprintf(f, "clipMin=%f,%f,%f\n", clipMin[0], clipMin[1], clipMin[2]);
        fprintf(f, "clipMax=%f,%f,%f\n", clipMax[0], clipMax[1], clipMax[2]);
        fprintf(f, "camSpeedMultiplier=%f\n", camSpeedMultiplier);
        fprintf(f, "uiScale=%f\n", uiScale);
        fprintf(f, "darkTheme=%d\n", (int)darkTheme);
        fprintf(f, "enableEDL=%d\n", (int)enableEDL);
        fprintf(f, "edlStrength=%f\n", edlStrength);
        fprintf(f, "edlRadius=%f\n", edlRadius);
        fprintf(f, "autoLoadLast=%d\n", (int)autoLoadLast);
        fprintf(f, "padEnabled=%d\n", (int)padEnabled);
        fprintf(f, "padDeadzone=%f\n", pad.deadzone);
        fprintf(f, "padLookSens=%f\n", padLookSens);
        fprintf(f, "padMoveSens=%f\n", padMoveSens);
        fprintf(f, "padInvertY=%d\n", (int)padInvertY);
        fprintf(f, "jAxes=%d,%d,%d,%d\n", pad.jAxisX, pad.jAxisY, pad.jAxisRX, pad.jAxisRY);
        fprintf(f, "jBtns=%d,%d,%d,%d,%d,%d\n", pad.jBtnA, pad.jBtnB, pad.jBtnLB, pad.jBtnRB, pad.jBtnBack, pad.jBtnStart);
        fprintf(f, "serialEnabled=%d\n", (int)serialEnabled);
        fprintf(f, "serialAuto=%d\n", (int)serialAuto);
        fprintf(f, "serialMac=%s\n", serialMac.c_str());
        fprintf(f, "serialPort=%s\n", serialPort.c_str());
        for (const auto& r : recentDirs) fprintf(f, "recent=%s\n", r.c_str());
        fclose(f);
    };

    auto loadSettings = [&]() {
        FILE* f = fopen("pfview_config.txt", "r");
        if (!f) return;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            float f1, f2, f3; int i;
            if (sscanf(line, "pointSize=%f", &f1) == 1) pointSize = f1;
            else if (sscanf(line, "sseBudget=%f", &f1) == 1) sseBudget = f1;
            else if (sscanf(line, "gpuBudgetMB=%d", &i) == 1) gpuBudgetMB = i;
            else if (sscanf(line, "uploadsPerFrame=%d", &i) == 1) uploadsPerFrame = i;
            else if (sscanf(line, "roundPoints=%d", &i) == 1) roundPoints = (i != 0);
            else if (sscanf(line, "attenuate=%d", &i) == 1) attenuate = (i != 0);
            else if (sscanf(line, "clearColor=%f,%f,%f", &f1, &f2, &f3) == 3) { clearColor[0]=f1; clearColor[1]=f2; clearColor[2]=f3; }
            else if (sscanf(line, "colorMode=%d", &i) == 1) colorMode = i;
            else if (sscanf(line, "solidColor=%f,%f,%f", &f1, &f2, &f3) == 3) { solidColor[0]=f1; solidColor[1]=f2; solidColor[2]=f3; }
            else if (sscanf(line, "stereoSBS=%d", &i) == 1) stereoSBS = (i != 0);
            else if (sscanf(line, "eyeSeparation=%f", &f1) == 1) eyeSeparation = f1;
            else if (sscanf(line, "focalDistance=%f", &f1) == 1) focalDistance = f1;
            else if (sscanf(line, "enableClipping=%d", &i) == 1) enableClipping = (i != 0);
            else if (sscanf(line, "clipMin=%f,%f,%f", &f1, &f2, &f3) == 3) { clipMin[0]=f1; clipMin[1]=f2; clipMin[2]=f3; }
            else if (sscanf(line, "clipMax=%f,%f,%f", &f1, &f2, &f3) == 3) { clipMax[0]=f1; clipMax[1]=f2; clipMax[2]=f3; }
            else if (sscanf(line, "camSpeedMultiplier=%f", &f1) == 1) camSpeedMultiplier = f1;
            else if (sscanf(line, "uiScale=%f", &f1) == 1) uiScale = f1;
            else if (sscanf(line, "darkTheme=%d", &i) == 1) darkTheme = (i != 0);
            else if (sscanf(line, "enableEDL=%d", &i) == 1) enableEDL = (i != 0);
            else if (sscanf(line, "edlStrength=%f", &f1) == 1) edlStrength = f1;
            else if (sscanf(line, "edlRadius=%f", &f1) == 1) edlRadius = f1;
            else if (sscanf(line, "autoLoadLast=%d", &i) == 1) autoLoadLast = (i != 0);
            else if (sscanf(line, "padEnabled=%d", &i) == 1) padEnabled = (i != 0);
            else if (sscanf(line, "padDeadzone=%f", &f1) == 1) pad.deadzone = f1;
            else if (sscanf(line, "padLookSens=%f", &f1) == 1) padLookSens = f1;
            else if (sscanf(line, "padMoveSens=%f", &f1) == 1) padMoveSens = f1;
            else if (sscanf(line, "padInvertY=%d", &i) == 1) padInvertY = (i != 0);
            else if (sscanf(line, "jAxes=%d,%d,%d,%d", &pad.jAxisX, &pad.jAxisY, &pad.jAxisRX, &pad.jAxisRY) == 4) {}
            else if (sscanf(line, "jBtns=%d,%d,%d,%d,%d,%d", &pad.jBtnA, &pad.jBtnB, &pad.jBtnLB, &pad.jBtnRB, &pad.jBtnBack, &pad.jBtnStart) == 6) {}
            else if (sscanf(line, "serialEnabled=%d", &i) == 1) serialEnabled = (i != 0);
            else if (sscanf(line, "serialAuto=%d", &i) == 1) serialAuto = (i != 0);
            else if (strncmp(line, "serialMac=", 10) == 0) {
                std::string v = line + 10;
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
                serialMac = v;
            }
            else if (strncmp(line, "serialPort=", 11) == 0) {
                std::string v = line + 11;
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
                serialPort = v;
            }
            else if (strncmp(line, "recent=", 7) == 0) {
                std::string v = line + 7;
                while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
                if (!v.empty() && recentDirs.size() < 8) recentDirs.push_back(v);
            }
        }
        fclose(f);
    };

    loadSettings();
    uiScale = std::clamp(uiScale, 0.5f, 4.0f);
    applyUiScale(uiScale);
    if (serialEnabled) serial.start(serialMac, serialPort, serialAuto);

    // Load an octree directory, reset view, record it as recent.
    auto loadOctree = [&](const std::string& dir) -> bool {
        store.clear();
        renderer.clear();
        if (store.load(dir)) {
            octreeLoaded = true;
            setupCamera();
            addRecent(dir);
            saveSettings();
#ifdef _WIN32
            MessageBeep(MB_ICONINFORMATION);
#endif
            return true;
        }
        pf::logError("Could not load octree from " + dir);
        return false;
    };

    // Initial cloud: CLI arg wins, else auto-load the most recent if enabled.
    if (!initialDir.empty()) loadOctree(initialDir);
    else if (autoLoadLast && !recentDirs.empty()) loadOctree(recentDirs.front());

    // Fit the whole cloud in view (keeps current orientation, centres on pivot).
    auto frameAll = [&]() {
        if (!octreeLoaded) return;
        double cs = store.cube(store.rootIndex()).size;
        float dist = (float)(cs * 0.5 / std::tan(glm::radians(cam.fovY * 0.5f)) * 1.4);
        pivot = glm::vec3(0.0f);                 // cube centre in centred space
        cam.position = pivot - cam.front() * dist;
        cam.lookAt(pivot);
        cam.orthoSize = (float)(cs * 0.6);
    };

    bool running = true;
    bool mouseLook = false;
    uint64_t frame = 0;
    Uint64 prevTicks = SDL_GetPerformanceCounter();
    
    std::string convInput = "";
    std::string convOutput = "";
    int presetIdx = 1;
    std::atomic<bool> isConverting(false);
    std::atomic<bool> convertDone(false);
    std::atomic<bool> convertSuccess(false);
    std::atomic<bool> cancelConvert(false);
    std::thread convertThread;
    
    std::atomic<float> convertProgress(0.0f);
    std::mutex convertStatusMutex;
    std::string convertStatus = "";
    
    pf::IndexOptions customOpts;
    
    bool showUI = true;
    Uint32 lastEscapeTime = 0;

    while (running) {
        // ---- timing -------------------------------------------------------
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - prevTicks) / (double)SDL_GetPerformanceFrequency());
        if (dt > 0.1f) dt = 0.1f;
        prevTicks = now;
        ++frame;

        // ---- events -------------------------------------------------------
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            pad.onDeviceEvent(e);
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_DROPFILE) {
                std::string dropFile = e.drop.file;
                SDL_free(e.drop.file);
                std::error_code ec;
                if (std::filesystem::is_directory(dropFile, ec)) {
                    loadOctree(dropFile);
                } else if (std::filesystem::is_regular_file(dropFile, ec)) {
                    convInput = dropFile;
                    convOutput = "";
                }
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                winW = e.window.data1; winH = e.window.data2;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (octreeLoaded && !ImGui::GetIO().WantCaptureMouse) {
                    if (measureMode) {
                        pendingPick = true; pickX = e.button.x; pickY = e.button.y;
                    } else {
                        orbitDrag = true;                       // LMB drag = orbit
                        if (e.button.clicks == 2) {             // double-click = focus
                            pendingFocus = true; focusX = e.button.x; focusY = e.button.y;
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                orbitDrag = false;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    mouseLook = true; SDL_SetRelativeMouseMode(SDL_TRUE);
                    SDL_GetRelativeMouseState(nullptr, nullptr); // clear accumulated motion
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                mouseLook = false; SDL_SetRelativeMouseMode(SDL_FALSE);
            } else if (e.type == SDL_MOUSEMOTION) {
                if (mouseLook) {
                    cam.addYawPitch((float)e.motion.xrel, -(float)e.motion.yrel);
                } else if (orbitDrag && octreeLoaded) {
                    cam.orbit((float)e.motion.xrel, -(float)e.motion.yrel, pivot);
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    if (SDL_GetModState() & KMOD_CTRL) {
                        // Ctrl+wheel adjusts point size (was plain wheel).
                        pointSize = std::clamp(pointSize + (e.wheel.y > 0 ? 1.0f : -1.0f), 1.0f, 16.0f);
                    } else if (octreeLoaded) {
                        // Plain wheel = zoom toward cursor (resolved after render).
                        pendingZoom = true; zoomDelta = (float)e.wheel.y;
                        SDL_GetMouseState(&zoomX, &zoomY);
                    }
                }
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_F5) {
                    showUI = !showUI;
                } else if (e.key.keysym.sym == SDLK_F12) {
                    pendingShot = true;                         // F12 = screenshot
                } else if (e.key.keysym.sym == SDLK_F1) {
                    showHelp = !showHelp;                       // F1 = controls help
                } else if (e.key.keysym.sym == SDLK_f) {
                    frameAllReq = true;                         // 'F' = frame all
                } else if (e.key.keysym.sym == SDLK_F11) {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                        SDL_SetWindowFullscreen(window, 0);
                    } else {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                    Uint32 nowTicks = SDL_GetTicks();
                    if (nowTicks - lastEscapeTime < 500) {
                        running = false;
                    }
                    lastEscapeTime = nowTicks;
                }
            }
        }

        // ---- keyboard movement (ignored while typing in ImGui) ------------
        if (!ImGui::GetIO().WantCaptureKeyboard && octreeLoaded) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            float speed = cam.moveSpeed * dt * (ks[SDL_SCANCODE_LSHIFT] ? 5.0f : 1.0f) * camSpeedMultiplier;
            glm::vec3 fwd = cam.front(), rgt = cam.right();
            glm::vec3 moveDir(0.0f);
            
            if (ks[SDL_SCANCODE_W]) moveDir += fwd;
            if (ks[SDL_SCANCODE_S]) moveDir -= fwd;
            if (ks[SDL_SCANCODE_D]) moveDir += rgt;
            if (ks[SDL_SCANCODE_A]) moveDir -= rgt;
            if (ks[SDL_SCANCODE_E]) moveDir += glm::vec3(0, 0, 1); // E is up (world Z-up)
            if (ks[SDL_SCANCODE_Q]) moveDir -= glm::vec3(0, 0, 1); // Q is down
            
            if (glm::length(moveDir) > 0.0f) {
                moveDir = glm::normalize(moveDir);
                cam.position += moveDir * speed;
            }
        }

        // ---- controller input --------------------------------------------
        if (padEnabled && pad.connected()) {
            pad.poll();
            // Drive ImGui nav with the gamepad only in UI mode (GameController only;
            // imgui's SDL2 backend reads the controller itself).
            if (uiNavMode) ImGui::GetIO().ConfigFlags |=  ImGuiConfigFlags_NavEnableGamepad;
            else           ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;

            // Mode toggle: Start (Xbox) or B (custom) flips camera <-> UI control.
            if (pad.pressed(PAD_START) || pad.pressed(PAD_B)) uiNavMode = !uiNavMode;

            if (!uiNavMode && octreeLoaded) {
                const PadState& s = pad.cur();
                float boost = (pad.held(PAD_RB) ? 5.0f : 1.0f);
                float mv = cam.moveSpeed * dt * camSpeedMultiplier * padMoveSens * boost;

                // Custom 1-stick controllers have no right stick: hold LB to make
                // the left stick steer (look) instead of move.
                bool lookWithLeft = !pad.isGameController() && pad.held(PAD_LB);
                float invY = padInvertY ? -1.0f : 1.0f;

                if (lookWithLeft) {
                    cam.addYawPitch(s.lx * padLookSens * dt, -s.ly * invY * padLookSens * dt);
                } else {
                    glm::vec3 fwd = cam.front(), rgt = cam.right();
                    cam.position += (rgt * s.lx - fwd * s.ly) * mv;   // left stick = move
                    // Right stick = look (Xbox); absent on custom pads -> zero.
                    cam.addYawPitch(s.rx * padLookSens * dt, -s.ry * invY * padLookSens * dt);
                }
                // Triggers / D-pad vertical = down / up.
                float up = s.rt - s.lt + (pad.held(PAD_DU) ? 1.0f : 0.0f) - (pad.held(PAD_DD) ? 1.0f : 0.0f);
                cam.position += glm::vec3(0, 0, 1) * up * mv;

                // Action buttons.
                if (pad.pressed(PAD_A)) frameAllReq = true;          // A = frame all
                if (pad.pressed(PAD_Y)) measureMode = !measureMode;  // Y = toggle measure
                if (pad.pressed(PAD_X)) pendingShot = true;          // X = screenshot
                if (pad.pressed(PAD_BACK)) showUI = !showUI;         // Back = toggle UI
            }
        }

        // ---- custom serial controller (ESP32 joystick: look + trigger-fly) --
        if (serialEnabled && serial.connected()) {
            ImGuiIO& io = ImGui::GetIO();
            float sx = serial.normX() * 2.0f - 1.0f;   // 0..1 -> -1..1
            float sy = serial.normY() * 2.0f - 1.0f;
            auto dz = [&](float v) {
                float a = std::fabs(v);
                if (a < pad.deadzone) return 0.0f;
                float s = (a - pad.deadzone) / (1.0f - pad.deadzone);
                return v < 0.0f ? -s : s;
            };
            sx = dz(sx); sy = dz(sy);
            float invY = padInvertY ? -1.0f : 1.0f;

            bool pauseClick = serial.consumePause();
            bool playClick  = serial.consumePlay();
            if (pauseClick) uiNavMode = !uiNavMode;     // PAUSE = UI mode toggle

            if (uiNavMode) {
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
                float ly = sy * invY;
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  sx < -0.1f, std::fmax(0.0f, -sx));
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, sx >  0.1f, std::fmax(0.0f,  sx));
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    ly < -0.1f, std::fmax(0.0f, -ly));
                io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  ly >  0.1f, std::fmax(0.0f,  ly));
                if (playClick) { io.AddKeyEvent(ImGuiKey_GamepadFaceDown, true); serialNavRelease = true; }
                else if (serialNavRelease) { io.AddKeyEvent(ImGuiKey_GamepadFaceDown, false); serialNavRelease = false; }
            } else if (octreeLoaded) {
                // Joystick = look; hold trigger to fly forward along the look dir.
                cam.addYawPitch(sx * padLookSens * dt, -sy * invY * padLookSens * dt);
                if (serial.triggerHeld()) {
                    float mv = cam.moveSpeed * dt * camSpeedMultiplier * padMoveSens;
                    cam.position += cam.front() * mv;
                }
                if (playClick) frameAllReq = true;       // PLAY = activate (frame all)
            }
        }

                // ---- absorb finished async loads ---------------------------------
        if (octreeLoaded) {
            for (int i = 0; i < uploadsPerFrame; ++i) {
                LoadResult res;
                if (!store.popResult(res)) break;
                renderer.upload(res.nodeIndex, res.verts);
            }
        }

        size_t visibleNodes = 0, drawnNodes = 0;
        uint64_t drawnPoints = 0;
        if (octreeLoaded) {
            // Render the scene into an offscreen FBO so depth is sampleable both
            // for the EDL post-pass and for cursor depth queries.
            ensureFbo(winW, winH);
            glBindFramebuffer(GL_FRAMEBUFFER, edlFbo);
            glViewport(0, 0, winW, winH);
            glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            size_t totalVisibleNodes = 0, totalDrawnNodes = 0;
            uint64_t totalDrawnPoints = 0;

            auto renderPass = [&](const glm::vec3& eyeOffset, int vpX, int vpY, int vpW, int vpH) {
                glViewport(vpX, vpY, vpW, vpH);
                
                cam.aspect = (vpH > 0) ? (float)vpW / (float)vpH : 1.0f;
                glm::vec3 originalPos = cam.position;
                cam.position += eyeOffset;
                
                glm::mat4 vp = cam.viewProj();
                Frustum frustum = extractFrustum(vp);
                
                if (stereoSBS) {
                    // Shift the view projection horizontally to create convergence at focalDistance
                    // This is a simple off-axis projection approximation by skewing the frustum
                    // Shift is related to eye offset and focal distance
                    float shift = -eyeOffset.x / focalDistance;
                    glm::mat4 shear(1.0f);
                    shear[2][0] = shift;
                    vp = vp * shear;
                    frustum = extractFrustum(vp);
                }
                
                const glm::dvec3 center = store.cubeCenter();
                const double ssFactor = (vpH * 0.5) / std::tan(glm::radians(cam.fovY) * 0.5);

                shader.use();
                shader.setMat4("uMVP", glm::value_ptr(vp));
                shader.setFloat("uPointSize", pointSize);
                shader.setFloat("uAttenuation", attenuate ? 1.0f : 0.0f);
                shader.setFloat("uViewportH", (float)vpH);
                shader.setInt("uRound", roundPoints ? 1 : 0);
                
                shader.setInt("uColorMode", colorMode);
                shader.setVec3("uSolidColor", solidColor[0], solidColor[1], solidColor[2]);
                
                float minZ = (float)store.cube(store.rootIndex()).min[2] - (float)center.z;
                float maxZ = minZ + (float)store.cube(store.rootIndex()).size;
                shader.setVec2("uZBounds", minZ, maxZ);
                
                if (enableClipping) {
                    shader.setVec3("uClipMin", clipMin[0], clipMin[1], clipMin[2]);
                    shader.setVec3("uClipMax", clipMax[0], clipMax[1], clipMax[2]);
                } else {
                    shader.setVec3("uClipMin", -1e9f, -1e9f, -1e9f);
                    shader.setVec3("uClipMax", 1e9f, 1e9f, 1e9f);
                }

                std::function<void(uint32_t)> visit = [&](uint32_t idx) {
                    const NodeRecord& rec = store.nodes()[idx];
                    const NodeCube& nc = store.cube(idx);
                    glm::vec3 mn((float)(nc.min[0] - center.x),
                                 (float)(nc.min[1] - center.y),
                                 (float)(nc.min[2] - center.z));
                    glm::vec3 mx = mn + glm::vec3((float)nc.size);
                    if (!aabbVisible(frustum, mn, mx)) return;

                    ++totalVisibleNodes;
                    if (renderer.isResident(idx)) {
                        renderer.draw(idx, frame);
                        ++totalDrawnNodes;
                        totalDrawnPoints += rec.pointCount;
                    } else {
                        store.requestLoad(idx, frame);
                    }

                    glm::vec3 nodeCenter = (mn + mx) * 0.5f;
                    float dist = glm::length(nodeCenter - cam.position);
                    if (dist < 1e-3f) dist = 1e-3f;
                    double spacing = store.nodeSpacing(rec.level);
                    double pixels = spacing * ssFactor / dist;

                    if (pixels > sseBudget && rec.childMask != 0) {
                        for (int o = 0; o < 8; ++o)
                            if (rec.children[o] != kNoChild) visit(rec.children[o]);
                    }
                };
                visit(store.rootIndex());
                cam.position = originalPos; // restore
            };

            if (stereoSBS) {
                glm::vec3 rightVec = cam.right();
                glm::vec3 leftOffset = -rightVec * (eyeSeparation * 0.5f);
                glm::vec3 rightOffset = rightVec * (eyeSeparation * 0.5f);
                
                int halfW = winW / 2;
                renderPass(leftOffset, 0, 0, halfW, winH);
                renderPass(rightOffset, halfW, 0, winW - halfW, winH);
                
                visibleNodes = totalVisibleNodes / 2;
                drawnNodes = totalDrawnNodes / 2;
                drawnPoints = totalDrawnPoints / 2;
            } else {
                renderPass(glm::vec3(0.0f), 0, 0, winW, winH);
                visibleNodes = totalVisibleNodes;
                drawnNodes = totalDrawnNodes;
                drawnPoints = totalDrawnPoints;
            }

            renderer.evictToBudget((size_t)gpuBudgetMB * 1024u * 1024u, frame, store);

            // Drop load requests the camera moved past (>120 frames ≈ 2s @60fps)
            // and cap buffered uploads so the streaming queues don't grow unbounded.
            store.purgeStale(frame, 120, (size_t)uploadsPerFrame * 8);

            // ---- resolve a pending measurement pick ---------------------------
            if (pendingPick) {
                pendingPick = false;
                cam.aspect = (winH > 0) ? (float)winW / (float)winH : 1.0f;
                glm::vec3 ro, rd;
                cam.screenRay((float)pickX, (float)pickY, winW, winH, ro, rd);
                double ssF = (winH * 0.5) / std::tan(glm::radians(cam.fovY) * 0.5);
                double tolPerDist = (ssF > 0.0) ? (6.0 / ssF) : 0.01; // ~6px pick disc
                glm::dvec3 hit;
                if (store.pickPoint(ro, rd, tolPerDist, hit)) {
                    measurePts.push_back(hit);   // append vertex to the polyline
                }
            }

            // ---- depth-buffer cursor query (cheap; reuses this frame's depth) --
            // Returns the world-space surface point under pixel (mx,my), or false
            // if the cursor is over empty background. Used for focus + zoom.
            auto worldUnderCursor = [&](int mx, int my, glm::dvec3& outWorld) -> bool {
                if (mx < 0 || my < 0 || mx >= winW || my >= winH) return false;
                float depth = 1.0f;
                glReadPixels(mx, winH - 1 - my, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                if (depth >= 1.0f) return false; // background
                cam.aspect = (winH > 0) ? (float)winW / (float)winH : 1.0f;
                float ndcX = 2.0f * mx / (float)winW - 1.0f;
                float ndcY = 1.0f - 2.0f * my / (float)winH;
                float ndcZ = 2.0f * depth - 1.0f;
                glm::vec4 p = glm::inverse(cam.viewProj()) * glm::vec4(ndcX, ndcY, ndcZ, 1.0f);
                if (std::fabs(p.w) < 1e-9f) return false;
                outWorld = glm::dvec3(glm::vec3(p) / p.w) + store.cubeCenter();
                return true;
            };

            if (pendingFocus) {
                pendingFocus = false;
                glm::dvec3 w;
                if (worldUnderCursor(focusX, focusY, w)) {
                    pivot = glm::vec3(w - store.cubeCenter());
                    float d = glm::length(cam.position - pivot);
                    cam.position = pivot - cam.front() * d; // centre the pivot, keep distance
                }
            }
            if (pendingZoom) {
                pendingZoom = false;
                glm::dvec3 w;
                glm::vec3 tgt = worldUnderCursor(zoomX, zoomY, w)
                                  ? glm::vec3(w - store.cubeCenter())
                                  : cam.position + cam.front();
                pivot = tgt;                                  // future orbit centres here
                float factor = (zoomDelta > 0.0f) ? 0.8f : 1.25f; // wheel up = closer
                cam.position = tgt + (cam.position - tgt) * factor;
                if (cam.isOrtho) cam.orthoSize = std::max(0.01f, cam.orthoSize * factor);
            }
            if (frameAllReq) { frameAllReq = false; frameAll(); }

            // Live world coordinate under the cursor (for the status bar).
            {
                int mxp = 0, myp = 0; SDL_GetMouseState(&mxp, &myp);
                hoverValid = !ImGui::GetIO().WantCaptureMouse &&
                             worldUnderCursor(mxp, myp, hoverWorld);
            }

            // ---- post-process pass: FBO -> screen (copy, or EDL shading) -----
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, winW, winH);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            edlShader.use();
            edlShader.setInt("uColor", 0);
            edlShader.setInt("uDepth", 1);
            edlShader.setVec2("uTexel", 1.0f / (float)winW, 1.0f / (float)winH);
            edlShader.setFloat("uStrength", edlStrength);
            edlShader.setFloat("uRadius", edlRadius);
            edlShader.setInt("uEdlOn", enableEDL ? 1 : 0);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, edlColorTex);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, edlDepthTex);
            glBindVertexArray(quadVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glActiveTexture(GL_TEXTURE0);
            glEnable(GL_DEPTH_TEST);
        } else {
            glViewport(0, 0, winW, winH);
            glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }



                // ---- ImGui overlay ------------------------------------------------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (watermarkTex) {
            float wmSize = 80.0f; // size of the watermark
            ImVec2 p_min = ImVec2(winW - wmSize - 20, 20); // 20px padding from top right
            ImVec2 p_max = ImVec2(p_min.x + wmSize, p_min.y + wmSize);
            ImU32 col = IM_COL32(255, 255, 255, 25); // ~10% opacity
            ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)watermarkTex, p_min, p_max, ImVec2(0,0), ImVec2(1,1), col);
        }

        // ---- measurement overlay (project polyline to screen) ---------------
        if (octreeLoaded && (!measurePts.empty() || measureMode)) {
            cam.aspect = (winH > 0) ? (float)winW / (float)winH : 1.0f;
            glm::mat4 mvp = cam.viewProj();
            glm::dvec3 ctr = store.cubeCenter();
            auto project = [&](const glm::dvec3& world, ImVec2& out) -> bool {
                glm::vec4 clip = mvp * glm::vec4(glm::vec3(world - ctr), 1.0f);
                if (clip.w <= 0.0f) return false; // behind the camera
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                out.x = (ndc.x * 0.5f + 0.5f) * (float)winW;
                out.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)winH;
                return true;
            };
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const ImU32 cMark = IM_COL32(255, 220, 40, 255);
            const ImU32 cLine = IM_COL32(255, 220, 40, 200);
            const ImU32 cText = IM_COL32(255, 255, 255, 255);
            const ImU32 cSnap = IM_COL32(80, 200, 255, 255);

            // Segments + per-segment length labels.
            ImVec2 prev;
            bool prevOk = false;
            for (size_t i = 0; i < measurePts.size(); ++i) {
                ImVec2 s;
                bool ok = project(measurePts[i], s);
                if (ok) { dl->AddCircleFilled(s, 5.0f, cMark); dl->AddCircle(s, 8.0f, cMark, 0, 2.0f); }
                if (ok && prevOk) {
                    dl->AddLine(prev, s, cLine, 2.0f);
                    double d = glm::length(measurePts[i] - measurePts[i - 1]);
                    char db[48]; snprintf(db, sizeof(db), "%.3f m", d);
                    dl->AddText(ImVec2((prev.x + s.x) * 0.5f + 6, (prev.y + s.y) * 0.5f - 6), cText, db);
                }
                prev = s; prevOk = ok;
            }
            // Snap preview: marker at the surface point under the cursor.
            if (measureMode && hoverValid) {
                ImVec2 s;
                if (project(hoverWorld, s)) dl->AddCircle(s, 7.0f, cSnap, 0, 2.0f);
            }
        }

        // ---- colour legend (elevation / intensity) --------------------------
        if (octreeLoaded && (colorMode == 1 || colorMode == 3)) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            float bx = (float)winW - 38.0f, by = 120.0f, bh = 180.0f, bw = 16.0f;
            auto turboCol = [](float v) {
                v = std::clamp(v, 0.0f, 1.0f);
                float r = std::clamp(3.24f * v - 2.15f, 0.0f, 1.0f);
                float g = std::clamp(-5.5f * v * v + 6.32f * v - 0.72f, 0.0f, 1.0f);
                float b = std::clamp(-4.5f * v * v + 1.25f * v + 0.88f, 0.0f, 1.0f);
                return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
            };
            const int N = 32;
            for (int i = 0; i < N; ++i) {
                float t0 = (float)i / N, t1 = (float)(i + 1) / N;
                dl->AddRectFilled(ImVec2(bx, by + bh * (1.0f - t1)),
                                  ImVec2(bx + bw, by + bh * (1.0f - t0)),
                                  turboCol((t0 + t1) * 0.5f));
            }
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 180));
            ImU32 tc = IM_COL32(255, 255, 255, 220);
            char hi[32], lo[32]; const char* title;
            if (colorMode == 1) {
                double zmin = store.cube(store.rootIndex()).min[2];
                double zmax = zmin + store.cube(store.rootIndex()).size;
                snprintf(hi, sizeof(hi), "%.1f", zmax);
                snprintf(lo, sizeof(lo), "%.1f", zmin);
                title = "Z (m)";
            } else {
                snprintf(hi, sizeof(hi), "max"); snprintf(lo, sizeof(lo), "0");
                title = "Intensity";
            }
            dl->AddText(ImVec2(bx - 2, by - 18), tc, title);
            dl->AddText(ImVec2(bx + bw + 4, by - 6), tc, hi);
            dl->AddText(ImVec2(bx + bw + 4, by + bh - 6), tc, lo);
        }

        if (convertDone) {
            convertDone = false;
            if (convertThread.joinable()) convertThread.join();
#ifdef _WIN32
            MessageBeep(MB_ICONINFORMATION);
#endif
            if (convertSuccess && !convOutput.empty()) {
                loadOctree(convOutput);
            }
        }

        if (showUI) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350, octreeLoaded ? 500.0f : 350.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("PointForge Dashboard");

        // ---- top toolbar (common actions) --------------------------------
        if (ImGui::Button("Open")) {
            std::string folder = pf::openFolderDialog();
            if (!folder.empty()) loadOctree(folder);
        }
        ImGui::SameLine(); if (ImGui::Button("Frame")) frameAll();
        ImGui::SameLine(); ImGui::Checkbox("Measure##toolbar", &measureMode);
        ImGui::SameLine(); if (ImGui::Button("Shot")) pendingShot = true;
        ImGui::SameLine(); if (ImGui::Button("Help")) showHelp = !showHelp;
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Viewer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Browse & Load Octree Folder...")) {
                std::string folder = pf::openFolderDialog();
                if (!folder.empty()) loadOctree(folder);
            }
            // Recent clouds + auto-load.
            if (!recentDirs.empty()) {
                ImGui::SameLine();
                if (ImGui::BeginCombo("##recent", "Recent")) {
                    for (size_t i = 0; i < recentDirs.size(); ++i) {
                        if (ImGui::Selectable(recentDirs[i].c_str())) loadOctree(recentDirs[i]);
                    }
                    ImGui::EndCombo();
                }
            }
            if (ImGui::Checkbox("Auto-load last on startup", &autoLoadLast)) saveSettings();
            ImGui::Separator();
            if (ImGui::SliderFloat("UI Scale", &uiScale, 0.5f, 3.0f, "%.2fx")) {
                uiScale = std::clamp(uiScale, 0.5f, 4.0f);
                applyUiScale(uiScale);
                saveSettings();
            }
            if (octreeLoaded) {
                ImGui::Separator();
                ImGui::Text("Cloud: %llu pts, %u nodes", (unsigned long long)store.meta().pointCount, store.meta().nodeCount);
                ImGui::Text("Visible nodes:  %zu", visibleNodes);
                ImGui::Text("Drawn nodes:    %zu", drawnNodes);
                ImGui::Text("Points on GPU:  %llu", (unsigned long long)renderer.pointsOnGpu());
                ImGui::Text("Drawn points:   %llu", (unsigned long long)drawnPoints);
                ImGui::Text("GPU resident:   %.1f MB", renderer.residentBytes() / (1024.0 * 1024.0));
                ImGui::Text("Load queue:     %zu", store.pendingRequests());
                ImGui::Text("FPS:            %.1f", dt > 0 ? 1.0f / dt : 0.0f);
                ImGui::Separator();
                bool settingsChanged = false;

                if (ImGui::Button("Reset to Defaults")) ImGui::OpenPopup("Reset?");
                if (ImGui::BeginPopupModal("Reset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Reset all viewer settings to defaults?");
                    if (ImGui::Button("Yes", ImVec2(80, 0))) {
                        resetSettings();
                        applyUiScale(uiScale);
                        if (octreeLoaded) setupCamera();
                        saveSettings();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(80, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                ImGui::Separator();
                if (ImGui::TreeNode("Rendering")) {
                    static int qualityIdx = 1;
                    const char* quals[] = { "Low", "Medium", "High", "Ultra" };
                    if (ImGui::Combo("Quality", &qualityIdx, quals, IM_ARRAYSIZE(quals))) {
                        switch (qualityIdx) {
                            case 0: sseBudget = 4.0f; gpuBudgetMB = 512;  break;
                            case 1: sseBudget = 2.0f; gpuBudgetMB = 1024; break;
                            case 2: sseBudget = 1.0f; gpuBudgetMB = 2048; break;
                            case 3: sseBudget = 0.5f; gpuBudgetMB = 4096; break;
                        }
                        settingsChanged = true;
                    }
                    ImGui::SetItemTooltip("One-knob preset for LOD detail + GPU budget.");
                    if (ImGui::SliderFloat("Point size", &pointSize, 1.0f, 16.0f)) settingsChanged = true;
                    ImGui::SetItemTooltip("Splat size in pixels. Also: Ctrl+mouse wheel.");
                    if (ImGui::SliderFloat("LOD budget (px)", &sseBudget, 0.3f, 8.0f)) settingsChanged = true;
                    ImGui::SetItemTooltip("Screen-space error target. Lower = more detail loaded (slower).");
                    if (ImGui::SliderInt("GPU budget (MB)", &gpuBudgetMB, 128, 8192)) settingsChanged = true;
                    ImGui::SetItemTooltip("Max GPU memory for resident points before LRU eviction.");
                    if (ImGui::SliderInt("Uploads/frame", &uploadsPerFrame, 1, 256)) settingsChanged = true;
                    ImGui::SetItemTooltip("Node VBO uploads per frame. Higher = faster streaming, more hitches.");
                    if (ImGui::Checkbox("Round points", &roundPoints)) settingsChanged = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Attenuate", &attenuate)) settingsChanged = true;
                    if (ImGui::ColorEdit3("Background", clearColor)) settingsChanged = true;
                    
                    const char* modes[] = { "True Color", "Elevation", "Solid Color", "Intensity", "Classification" };
                    if (ImGui::Combo("Color Mode", &colorMode, modes, IM_ARRAYSIZE(modes))) settingsChanged = true;
                    ImGui::SetItemTooltip("How points are coloured. Intensity/Classification use the LAS attributes.");
                    if (colorMode == 2) {
                        if (ImGui::ColorEdit3("Solid Color", solidColor)) settingsChanged = true;
                    }
                    bool lightTheme = !darkTheme;
                    if (ImGui::Checkbox("Light theme", &lightTheme)) {
                        darkTheme = !lightTheme;
                        applyUiScale(uiScale);   // re-apply colours + scale
                        settingsChanged = true;
                    }

                    if (ImGui::Checkbox("Stereoscopic (SBS)", &stereoSBS)) settingsChanged = true;
                    if (stereoSBS) {
                        if (ImGui::SliderFloat("Eye Separation (IPD)", &eyeSeparation, 0.01f, 0.2f)) settingsChanged = true;
                        if (ImGui::SliderFloat("Focal Distance", &focalDistance, 1.0f, 100.0f)) settingsChanged = true;
                    }

                    if (ImGui::Checkbox("Eye-Dome Lighting", &enableEDL)) settingsChanged = true;
                    ImGui::SetItemTooltip("Shades depth edges for much better depth perception on uncoloured clouds.");
                    if (enableEDL) {
                        if (ImGui::SliderFloat("EDL strength", &edlStrength, 0.1f, 5.0f)) settingsChanged = true;
                        if (ImGui::SliderFloat("EDL radius", &edlRadius, 0.5f, 4.0f)) settingsChanged = true;
                    }
                    ImGui::TreePop();
                }
                
                if (ImGui::TreeNode("Camera")) {
                    if (ImGui::Checkbox("Orthographic", &cam.isOrtho)) settingsChanged = true;
                    if (cam.isOrtho) {
                        if (ImGui::SliderFloat("Ortho Size", &cam.orthoSize, 1.0f, 5000.0f, "%.1f", ImGuiSliderFlags_Logarithmic)) settingsChanged = true;
                    }
                    if (ImGui::SliderFloat("Fly Speed", &camSpeedMultiplier, 0.1f, 10.0f, "%.1fx")) settingsChanged = true;
                    if (ImGui::Button("Reset View")) {
                        setupCamera();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Frame All (F)")) {
                        frameAll();
                    }
                    ImGui::Text("Presets:");
                    if (ImGui::Button("Top")) { cam.yaw = -90; cam.pitch = 89; cam.position = store.cubeCenter() + glm::dvec3(0,0,store.cube(store.rootIndex()).size * 2); } ImGui::SameLine();
                    if (ImGui::Button("Front")) { cam.yaw = -90; cam.pitch = 0; cam.position = store.cubeCenter() + glm::dvec3(0,-store.cube(store.rootIndex()).size * 2,0); } ImGui::SameLine();
                    if (ImGui::Button("Side")) { cam.yaw = 0; cam.pitch = 0; cam.position = store.cubeCenter() + glm::dvec3(store.cube(store.rootIndex()).size * 2,0,0); }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Clipping Planes")) {
                    if (ImGui::Checkbox("Enable Clipping", &enableClipping)) settingsChanged = true;
                    if (enableClipping) {
                        float ext = (float)store.cube(store.rootIndex()).size * 2.0f;
                        if (ImGui::SliderFloat3("Min", clipMin, -ext, ext)) settingsChanged = true;
                        if (ImGui::SliderFloat3("Max", clipMax, -ext, ext)) settingsChanged = true;
                        if (ImGui::Button("Reset Planes")) {
                            clipMin[0] = clipMin[1] = clipMin[2] = -ext;
                            clipMax[0] = clipMax[1] = clipMax[2] =  ext;
                            settingsChanged = true;
                        }
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Measure")) {
                    ImGui::Checkbox("Measure mode (LMB adds points)", &measureMode);
                    ImGui::SetItemTooltip("Click successive points to build a polyline; total length sums all segments.");
                    ImGui::TextWrapped("Points: %d", (int)measurePts.size());
                    for (size_t i = 0; i < measurePts.size(); ++i)
                        ImGui::Text("%2d: %.2f, %.2f, %.2f", (int)i + 1,
                                    measurePts[i].x, measurePts[i].y, measurePts[i].z);
                    if (measurePts.size() >= 2)
                        ImGui::TextColored(ImVec4(1, 0.86f, 0.16f, 1),
                                           "Total: %.3f m", measureTotal());
                    if (ImGui::Button("Undo") && !measurePts.empty()) measurePts.pop_back();
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) measurePts.clear();
                    ImGui::SameLine();
                    if (ImGui::Button("Copy") && !measurePts.empty()) {
                        std::string s;
                        char ln[96];
                        for (size_t i = 0; i < measurePts.size(); ++i) {
                            snprintf(ln, sizeof(ln), "%.4f, %.4f, %.4f\n",
                                     measurePts[i].x, measurePts[i].y, measurePts[i].z);
                            s += ln;
                        }
                        if (measurePts.size() >= 2) {
                            snprintf(ln, sizeof(ln), "total: %.4f m", measureTotal());
                            s += ln;
                        }
                        SDL_SetClipboardText(s.c_str());
                    }
                    ImGui::SetItemTooltip("Copy all points + total length to the clipboard.");
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Controller")) {
                    if (ImGui::Checkbox("Enable controller", &padEnabled)) settingsChanged = true;
                    if (pad.connected()) {
                        ImGui::Text("Device: %s", pad.name());
                        ImGui::Text("Type: %s", pad.isGameController() ? "Gamepad (auto-mapped)" : "Raw joystick");
                        ImGui::TextColored(uiNavMode ? ImVec4(0.4f, 0.8f, 1, 1) : ImVec4(0.6f, 1, 0.6f, 1),
                                           "Active: %s", uiNavMode ? "UI navigation" : "Camera");
                    } else {
                        ImGui::TextDisabled("No controller detected");
                        if (ImGui::Button("Rescan")) pad.openFirst();
                    }
                    ImGui::Checkbox("UI navigation mode", &uiNavMode);
                    ImGui::SetItemTooltip("Sticks drive the UI instead of the camera. Start/B toggles.");
                    if (ImGui::SliderFloat("Deadzone", &pad.deadzone, 0.0f, 0.5f)) settingsChanged = true;
                    if (ImGui::SliderFloat("Look sens", &padLookSens, 20.0f, 360.0f, "%.0f deg/s")) settingsChanged = true;
                    if (ImGui::SliderFloat("Move sens", &padMoveSens, 0.1f, 5.0f, "%.1fx")) settingsChanged = true;
                    if (ImGui::Checkbox("Invert look Y", &padInvertY)) settingsChanged = true;

                    if (pad.connected() && !pad.isGameController()) {
                        ImGui::SeparatorText("Custom mapping (raw joystick)");
                        ImGui::TextDisabled("Live values (move stick / press buttons to find indices):");
                        for (int i = 0; i < pad.rawAxisCount(); ++i)
                            ImGui::Text("  axis %d: % .2f", i, pad.rawAxis(i));
                        std::string down;
                        for (int i = 0; i < pad.rawButtonCount(); ++i)
                            if (pad.rawButton(i)) down += std::to_string(i) + " ";
                        ImGui::Text("  buttons down: %s", down.empty() ? "-" : down.c_str());
                        bool ch = false;
                        ch |= ImGui::InputInt("Move X axis", &pad.jAxisX);
                        ch |= ImGui::InputInt("Move Y axis", &pad.jAxisY);
                        ch |= ImGui::InputInt("LB button",  &pad.jBtnLB);
                        ch |= ImGui::InputInt("Button A",   &pad.jBtnA);
                        ch |= ImGui::InputInt("Button B",   &pad.jBtnB);
                        if (ch) settingsChanged = true;
                        ImGui::TextWrapped("Hold LB + stick = look. A = frame all. B = toggle UI mode.");
                    } else {
                        ImGui::TextWrapped("Left stick: move | Right stick: look | Triggers: down/up | "
                                           "RB: boost | A: frame | Y: measure | X: shot | Back: UI | Start: UI mode");
                    }

                    ImGui::SeparatorText("Custom serial controller (Bluetooth)");
                    if (ImGui::Checkbox("Enable serial controller", &serialEnabled)) {
                        settingsChanged = true;
                        if (serialEnabled) serial.start(serialMac, serialPort, serialAuto);
                        else serial.stop();
                    }
                    if (serial.connected())
                        ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "Connected: %s", serial.portName().c_str());
                    else
                        ImGui::TextDisabled("Not connected (waiting / not paired)");
                    if (ImGui::Checkbox("Auto-detect by MAC", &serialAuto)) settingsChanged = true;
                    char macBuf[32]; snprintf(macBuf, sizeof(macBuf), "%s", serialMac.c_str());
                    if (ImGui::InputText("MAC", macBuf, sizeof(macBuf))) { serialMac = macBuf; settingsChanged = true; }
                    char comBuf[16]; snprintf(comBuf, sizeof(comBuf), "%s", serialPort.c_str());
                    if (ImGui::InputText("COM port", comBuf, sizeof(comBuf))) { serialPort = comBuf; settingsChanged = true; }
                    if (ImGui::Button("Reconnect")) serial.start(serialMac, serialPort, serialAuto);
                    if (serial.connected())
                        ImGui::Text("X %.2f  Y %.2f  trigger %d",
                                    serial.normX(), serial.normY(), serial.triggerHeld() ? 1 : 0);
                    ImGui::TextWrapped("Joystick = look. Hold trigger = fly forward. "
                                       "PAUSE = UI mode. PLAY = activate / frame all.");
                    ImGui::TreePop();
                }

                if (settingsChanged) {
                    saveSettings();
                }

                ImGui::Separator();
                ImGui::TextWrapped("LMB drag: orbit   2xLMB: focus   RMB drag: look   "
                                   "wheel: zoom   Ctrl+wheel: point size   WASD/QE: fly   "
                                   "Shift: fast   F: frame all   F11: fullscreen");
            } else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "No octree loaded. Load or convert one.");
            }
        }
        
        if (ImGui::CollapsingHeader("Converter", octreeLoaded ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.02f, 1.0f));
            ImGui::BeginChild("ConverterPanel", ImVec2(0, 0), true);
            
            // Top buttons
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 100);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button(" HQ ")) presetIdx = 0; ImGui::SameLine();
            if (ImGui::Button(" LQ ")) presetIdx = 2; ImGui::SameLine();
            if (ImGui::Button(" M ")) presetIdx = 1; ImGui::SameLine();
            if (ImGui::Button(" Fast ")) presetIdx = 2; ImGui::SameLine();
            if (ImGui::Button(" Slow ")) presetIdx = 0;
            ImGui::PopStyleColor(2);
            
            if (presetIdx == 0) { customOpts.targetLeafSize = 20000; customOpts.rootSpacing = 0.0; customOpts.compress = true; customOpts.gridDepth = 4; customOpts.maxDepth = 28; customOpts.flushBudget = 67108864; presetIdx = 3; }
            if (presetIdx == 1) { customOpts.targetLeafSize = 50000; customOpts.rootSpacing = 0.0; customOpts.compress = true; customOpts.gridDepth = 3; customOpts.maxDepth = 24; customOpts.flushBudget = 33554432; presetIdx = 3; }
            if (presetIdx == 2) { customOpts.targetLeafSize = 100000; customOpts.rootSpacing = 0.0; customOpts.compress = false; customOpts.gridDepth = 2; customOpts.maxDepth = 20; customOpts.flushBudget = 16777216; presetIdx = 3; }
            
            ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
            
            float labelWidth = 140.0f;
            float inputWidth = ImGui::GetWindowWidth() - labelWidth - 30.0f;
            if (inputWidth < 100.0f) inputWidth = 100.0f;
            ImVec4 descColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            
            // Source File
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default bold-ish label
            ImGui::Text("Source File"); ImGui::PopFont(); 
            ImGui::SameLine(labelWidth);
            if (ImGui::Button("Browse")) {
                std::string f = pf::openFileDialog("Point Clouds\0*.las;*.laz;*.e57;*.ply;*.pts;*.xyz\0All Files\0*.*\0");
                if (!f.empty()) {
                    convInput = f;
                    std::filesystem::path p(f);
                    convOutput = (p.parent_path() / ("PointForgeCache_" + p.stem().string())).string();
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(inputWidth - 80.0f);
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", convInput.empty() ? "Enter Path to *.laz;*.las;*.ply;*.e57;*.pts;*.xyz models" : convInput.c_str());
            ImGui::InputText("##sourcefile", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
            ImGui::Spacing(); ImGui::Spacing();
            
            // Spacing
            ImGui::Text("Spacing"); ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(60.0f);
            float spacingFloat = (float)customOpts.rootSpacing;
            if (ImGui::DragFloat("##spacing", &spacingFloat, 0.01f, 0.0f, 10.0f)) customOpts.rootSpacing = spacingFloat;
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Minimum distance between points after decimation.");
            ImGui::TextWrapped("Spacing = 0 Keep all points. Spacing = 0.01 Remove points closer than 1 cm. 1,000,000 points -> 700,000 points");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // Leaf Size
            ImGui::Text("Leaf Size"); ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(inputWidth);
            int leafSize = customOpts.targetLeafSize;
            if (ImGui::DragInt("##leafsize", &leafSize, 1000, 1000, 1000000)) customOpts.targetLeafSize = leafSize;
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Maximum points allowed inside a leaf node.");
            ImGui::TextWrapped("Smaller Leaf Size = More nodes, Better culling, Better LOD, Larger octree");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // MaxDepth
            ImGui::Text("MaxDepth"); ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(inputWidth);
            ImGui::DragInt("##maxdepth", &customOpts.maxDepth, 1, 4, 32);
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Maximum octree depth.");
            ImGui::TextWrapped("Higher depth = More precise, Better close-up detail, More nodes; Lower depth: Less detail, Faster conversion");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // Chunk
            ImGui::Text("Chunk"); ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(inputWidth);
            ImGui::DragInt("##chunk", &customOpts.gridDepth, 1, 1, 8);
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Controls how many chunks of points are processed simultaneously");
            ImGui::TextWrapped("Higher: Faster conversion More RAM; Lower: Less RAM Slower conversion");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // Flush
            ImGui::Text("Flush"); ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(inputWidth);
            int flushBudget = customOpts.flushBudget;
            if (ImGui::DragInt("##flush", &flushBudget, 1024, 1024, 1024*1024*1024)) customOpts.flushBudget = flushBudget;
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Controls how many chunks of points are processed simultaneously.");
            ImGui::TextWrapped("Higher = Faster conversion, More RAM; Lower = Less RAM, Slower conversion");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // Keep Chunk
            ImGui::Text("Keep Chunk"); ImGui::SameLine(labelWidth);
            ImGui::Checkbox("##keepchunk", &customOpts.keepChunks);
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Controls how many chunks of points are processed simultaneously.");
            ImGui::TextWrapped("Higher: Faster conversion. More RAM; Lower: Less RAM, Slower conversion");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // Verbose
            static bool verboseLogs = false;
            ImGui::Text("Verbose"); ImGui::SameLine(labelWidth);
            ImGui::Checkbox("##verbose", &verboseLogs);
            ImGui::PushStyleColor(ImGuiCol_Text, descColor);
            ImGui::TextWrapped("Shows detailed logs.");
            ImGui::PopStyleColor();
            ImGui::Spacing(); ImGui::Spacing();
            
            // Status & Cache
            ImGui::Text("Status"); ImGui::SameLine(labelWidth);
            if (isConverting) {
                std::string statusMsg;
                {
                    std::lock_guard<std::mutex> lk(convertStatusMutex);
                    statusMsg = convertStatus;
                }
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", statusMsg.c_str());
                ImGui::SameLine();
                ImGui::ProgressBar(convertProgress.load(), ImVec2(220, 15));
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) cancelConvert = true;
            } else {
                if (ImGui::Button("Convert!", ImVec2(100, 30))) {
                    if (!convInput.empty() && !convOutput.empty()) {
                        isConverting = true;
                        convertDone = false;
                        cancelConvert = false;
                        convertProgress = 0.0f;
                        convertStatus = "Starting...";

                        pf::IndexOptions opts = customOpts;
                        opts.cancel = &cancelConvert;
                        opts.progressCb = [&convertProgress, &convertStatusMutex, &convertStatus](float pct, const std::string& msg) {
                            convertProgress = pct;
                            std::lock_guard<std::mutex> lk(convertStatusMutex);
                            convertStatus = msg;
                        };

                        convertThread = std::thread([input = convInput, outDir = convOutput, opts, &convertSuccess, &isConverting, &convertDone]() {
                            bool success = pf::buildOctree(input, outDir, opts);
                            convertSuccess = success;
                            isConverting = false;
                            convertDone = true;
                        });
                    }
                }
            }
            
                ImGui::Spacing(); ImGui::Spacing();
                ImGui::Text("Cache"); ImGui::SameLine(labelWidth);
                float mb = 0.0f; // Could compute actual directory size if needed
                ImGui::Text("Cache: %.1f MB", mb);
                
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            ImGui::End();
        }

        // ---- always-on status bar ----------------------------------------
        if (showStatusBar) {
            float barH = ImGui::GetTextLineHeightWithSpacing() + 8.0f;
            ImGui::SetNextWindowPos(ImVec2(0.0f, (float)winH - barH));
            ImGui::SetNextWindowSize(ImVec2((float)winW, barH));
            ImGui::SetNextWindowBgAlpha(0.70f);
            ImGuiWindowFlags sf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;
            if (ImGui::Begin("##statusbar", nullptr, sf)) {
                ImGui::Text("FPS %.0f", dt > 0 ? 1.0f / dt : 0.0f); ImGui::SameLine(0, 18);
                if (octreeLoaded) {
                    const char* mode = mouseLook ? "Look" : (measureMode ? "Measure" : (orbitDrag ? "Orbit" : "Idle"));
                    ImGui::Text("Pts %llu / %llu", (unsigned long long)drawnPoints,
                                (unsigned long long)store.meta().pointCount); ImGui::SameLine(0, 18);
                    ImGui::Text("GPU %.0f MB", renderer.residentBytes() / 1048576.0); ImGui::SameLine(0, 18);
                    ImGui::Text("Mode %s", mode); ImGui::SameLine(0, 18);
                    if (hoverValid)
                        ImGui::Text("XYZ %.2f, %.2f, %.2f", hoverWorld.x, hoverWorld.y, hoverWorld.z);
                    else
                        ImGui::TextDisabled("XYZ --");
                    size_t pend = store.pendingRequests();
                    if (pend > 0) {
                        ImGui::SameLine(0, 18);
                        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Loading %zu...", pend);
                    }
                    if (padEnabled && pad.connected()) {
                        ImGui::SameLine(0, 18);
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Pad:%s", uiNavMode ? "UI" : "Cam");
                    }
                    if (serialEnabled && serial.connected()) {
                        ImGui::SameLine(0, 18);
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "BT:%s", uiNavMode ? "UI" : "Cam");
                    }
                } else {
                    ImGui::TextDisabled("No cloud loaded  -  press F1 for help");
                }
            }
            ImGui::End();
        }

        // ---- controls help overlay (F1) ----------------------------------
        if (showHelp) {
            ImGui::SetNextWindowPos(ImVec2(winW * 0.5f, winH * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("Controls  (F1 to close)", &showHelp, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::SeparatorText("Navigation");
                ImGui::BulletText("LMB drag        Orbit around pivot");
                ImGui::BulletText("Double-click    Focus pivot on point");
                ImGui::BulletText("RMB drag        Free look");
                ImGui::BulletText("Wheel           Zoom to cursor");
                ImGui::BulletText("Ctrl + Wheel    Point size");
                ImGui::BulletText("WASD / Q E      Fly  (Shift = fast)");
                ImGui::BulletText("F               Frame all");
                ImGui::SeparatorText("Controller");
                ImGui::BulletText("Left stick      Move");
                ImGui::BulletText("Right stick     Look  (custom: hold LB + stick)");
                ImGui::BulletText("Triggers        Down / up");
                ImGui::BulletText("A / Y / X       Frame / measure / screenshot");
                ImGui::BulletText("Start (or B)    Toggle UI navigation mode");
                ImGui::SeparatorText("Tools");
                ImGui::BulletText("Measure mode    LMB picks points");
                ImGui::SeparatorText("View");
                ImGui::BulletText("F5              Toggle UI panel");
                ImGui::BulletText("F11             Fullscreen");
                ImGui::BulletText("F1              This help");
                ImGui::BulletText("Esc Esc         Quit (double press)");
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (pendingShot) {
            pendingShot = false;
            char fn[64];
            snprintf(fn, sizeof(fn), "screenshot_%04d.bmp", ++shotCounter);
            if (saveScreenshotBMP(fn, winW, winH)) {
                logInfo(std::string("Saved ") + fn);
#ifdef _WIN32
                MessageBeep(MB_OK);
#endif
            }
        }

        SDL_GL_SwapWindow(window);
    }

    // ---- shutdown ---------------------------------------------------------
    if (convertThread.joinable()) {
        convertThread.join();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
