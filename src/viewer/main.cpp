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

static GLuint loadTextureBMP(const char* path) {
    SDL_Surface* surf = SDL_LoadBMP(path);
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

int main(int argc, char** argv) {
    std::string initialDir = "";
    if (argc >= 2) initialDir = argv[1];

    // ---- SDL + GL context -------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { logError(std::string("SDL_Init: ") + SDL_GetError()); return 1; }
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

    SDL_Surface* icon = SDL_LoadBMP("C:\\UnrealProject\\PointForge\\images\\vx.bmp");
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

    // ---- assets -----------------------------------------------------------
    OctreeStore store;
    bool octreeLoaded = false;
    if (!initialDir.empty()) {
        if (store.load(initialDir)) { octreeLoaded = true; }
        else { logError("Failed to load octree from " + initialDir); }
    }

    // Locate shaders relative to the executable so the viewer can run from any cwd.
    std::string base;
    if (char* b = SDL_GetBasePath()) { base = b; SDL_free(b); }
    Shader shader;
    if (!shader.loadFromFiles(base + "shaders/point.vert", base + "shaders/point.frag")) {
        logError("Failed to load shaders from " + base + "shaders/");
        return 3;
    }

    PointRenderer renderer;
    GLuint watermarkTex = loadTextureBMP("C:\\UnrealProject\\PointForge\\images\\vx.bmp");

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

    auto resetSettings = [&]() {
        pointSize = 2.0f; sseBudget = 1.5f; gpuBudgetMB = 1024;
        roundPoints = true; attenuate = false; uploadsPerFrame = 32;
        clearColor[0]=0.06f; clearColor[1]=0.07f; clearColor[2]=0.09f;
        colorMode = 0; solidColor[0]=1.0f; solidColor[1]=1.0f; solidColor[2]=1.0f;
        enableClipping = false; clipMin[0]=-1000.0f; clipMin[1]=-1000.0f; clipMin[2]=-1000.0f;
        clipMax[0]=1000.0f; clipMax[1]=1000.0f; clipMax[2]=1000.0f;
        stereoSBS = false; eyeSeparation = 0.05f; focalDistance = 10.0f;
        camSpeedMultiplier = 1.0f;
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
        }
        fclose(f);
    };

    loadSettings();

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
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_DROPFILE) {
                std::string dropFile = e.drop.file;
                SDL_free(e.drop.file);
                std::error_code ec;
                if (std::filesystem::is_directory(dropFile, ec)) {
                    store.clear();
                    renderer.clear();
                    if (store.load(dropFile)) {
                        octreeLoaded = true;
                        setupCamera();
#ifdef _WIN32
                        MessageBeep(MB_ICONINFORMATION);
#endif
                    }
                } else if (std::filesystem::is_regular_file(dropFile, ec)) {
                    convInput = dropFile;
                    convOutput = "";
                }
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                winW = e.window.data1; winH = e.window.data2;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    mouseLook = true; SDL_SetRelativeMouseMode(SDL_TRUE);
                    SDL_GetRelativeMouseState(nullptr, nullptr); // clear accumulated motion
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) {
                mouseLook = false; SDL_SetRelativeMouseMode(SDL_FALSE);
            } else if (e.type == SDL_MOUSEMOTION && mouseLook) {
                cam.addYawPitch((float)e.motion.xrel, -(float)e.motion.yrel);
            } else if (e.type == SDL_MOUSEWHEEL) {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    pointSize = std::max(1.0f, pointSize + (e.wheel.y > 0 ? 1.0f : -1.0f));
                }
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_F5) {
                    showUI = !showUI;
                } else if (e.key.keysym.sym == SDLK_f) {
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
            if (ks[SDL_SCANCODE_E]) moveDir += glm::vec3(0, 1, 0); // E is up
            if (ks[SDL_SCANCODE_Q]) moveDir -= glm::vec3(0, 1, 0); // Q is down
            
            if (glm::length(moveDir) > 0.0f) {
                moveDir = glm::normalize(moveDir);
                cam.position += moveDir * speed;
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
                        store.requestLoad(idx);
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

        if (convertDone) {
            convertDone = false;
            if (convertThread.joinable()) convertThread.join();
#ifdef _WIN32
            MessageBeep(MB_ICONINFORMATION);
#endif
            if (convertSuccess && !convOutput.empty()) {
                store.clear();
                renderer.clear();
                if (store.load(convOutput)) {
                    octreeLoaded = true;
                    setupCamera();
                }
            }
        }

        if (showUI) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350, octreeLoaded ? 500.0f : 350.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("PointForge Dashboard");
        
        if (ImGui::CollapsingHeader("Viewer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Browse & Load Octree Folder...")) {
                std::string folder = pf::openFolderDialog();
                if (!folder.empty()) {
                    store.clear();
                    renderer.clear();
                    if (store.load(folder)) {
                        octreeLoaded = true;
                        setupCamera();
#ifdef _WIN32
                        MessageBeep(MB_ICONINFORMATION);
#endif
                    } else {
                        pf::logError("Could not load octree from " + folder);
                    }
                }
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

                if (ImGui::Button("Reset to Defaults")) {
                    resetSettings();
                    settingsChanged = true;
                    if (octreeLoaded) setupCamera();
                }

                ImGui::Separator();
                if (ImGui::TreeNode("Rendering")) {
                    if (ImGui::SliderFloat("Point size", &pointSize, 1.0f, 16.0f)) settingsChanged = true;
                    if (ImGui::SliderFloat("LOD budget (px)", &sseBudget, 0.3f, 8.0f)) settingsChanged = true;
                    if (ImGui::SliderInt("GPU budget (MB)", &gpuBudgetMB, 128, 8192)) settingsChanged = true;
                    if (ImGui::SliderInt("Uploads/frame", &uploadsPerFrame, 1, 256)) settingsChanged = true;
                    if (ImGui::Checkbox("Round points", &roundPoints)) settingsChanged = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Attenuate", &attenuate)) settingsChanged = true;
                    if (ImGui::ColorEdit3("Background", clearColor)) settingsChanged = true;
                    
                    const char* modes[] = { "True Color", "Elevation", "Solid Color" };
                    if (ImGui::Combo("Color Mode", &colorMode, modes, IM_ARRAYSIZE(modes))) settingsChanged = true;
                    if (colorMode == 2) {
                        if (ImGui::ColorEdit3("Solid Color", solidColor)) settingsChanged = true;
                    }
                    
                    if (ImGui::Checkbox("Stereoscopic (SBS)", &stereoSBS)) settingsChanged = true;
                    if (stereoSBS) {
                        if (ImGui::SliderFloat("Eye Separation (IPD)", &eyeSeparation, 0.01f, 0.2f)) settingsChanged = true;
                        if (ImGui::SliderFloat("Focal Distance", &focalDistance, 1.0f, 100.0f)) settingsChanged = true;
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
                    }
                    ImGui::TreePop();
                }

                if (settingsChanged) {
                    saveSettings();
                }

                ImGui::Separator();
                ImGui::TextWrapped("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");
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
                ImGui::ProgressBar(convertProgress.load(), ImVec2(300, 15));
            } else {
                if (ImGui::Button("Convert!", ImVec2(100, 30))) {
                    if (!convInput.empty() && !convOutput.empty()) {
                        isConverting = true;
                        convertDone = false;
                        convertProgress = 0.0f;
                        convertStatus = "Starting...";
                        
                        pf::IndexOptions opts = customOpts;
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

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
