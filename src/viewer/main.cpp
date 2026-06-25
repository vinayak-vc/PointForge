// pfview — PointForge streaming viewer (SDL2 + OpenGL 3.3 core + Dear ImGui).
#include <GL/glew.h>   // must precede any system GL header
#include <SDL.h>

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

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("Usage: pfview <octree-dir>\n"); return 1; }
    const std::string dir = argv[1];

    // ---- SDL + GL context -------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { logError(std::string("SDL_Init: ") + SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int winW = 1280, winH = 720;
    SDL_Window* window = SDL_CreateWindow(
        "PointForge Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { logError(std::string("CreateWindow: ") + SDL_GetError()); return 1; }

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
    if (!store.load(dir)) { logError("Failed to load octree from " + dir); return 2; }

    // Locate shaders relative to the executable so the viewer can run from any cwd.
    std::string base;
    if (char* b = SDL_GetBasePath()) { base = b; SDL_free(b); }
    Shader shader;
    if (!shader.loadFromFiles(base + "shaders/point.vert", base + "shaders/point.frag")) {
        logError("Failed to load shaders from " + base + "shaders/");
        return 3;
    }

    PointRenderer renderer;

    // ---- camera initial framing ------------------------------------------
    Camera cam;
    const double cubeSize = store.meta().cubeSize;
    cam.position = glm::vec3(0.0f, -(float)cubeSize, 0.0f);
    cam.yaw = 0.0f; cam.pitch = 0.0f;
    cam.nearZ = (float)std::max(0.01, cubeSize / 5000.0);
    cam.farZ  = (float)(cubeSize * 8.0);
    cam.moveSpeed = (float)(cubeSize / 8.0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // ---- tunables (ImGui) -------------------------------------------------
    float pointSize   = 2.0f;
    float sseBudget   = 1.5f;   // pixels; smaller = more detail loaded
    int   gpuBudgetMB = 1024;
    bool  roundPoints = true;
    bool  attenuate   = false;
    int   uploadsPerFrame = 32;

    bool running = true;
    bool mouseLook = false;
    uint64_t frame = 0;
    Uint64 prevTicks = SDL_GetPerformanceCounter();

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
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
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
            }
        }

        // ---- keyboard movement (ignored while typing in ImGui) ------------
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            float speed = cam.moveSpeed * dt * (ks[SDL_SCANCODE_LSHIFT] ? 5.0f : 1.0f);
            glm::vec3 fwd = cam.front(), rgt = cam.right();
            glm::vec3 moveDir(0.0f);
            
            if (ks[SDL_SCANCODE_W]) moveDir += fwd;
            if (ks[SDL_SCANCODE_S]) moveDir -= fwd;
            if (ks[SDL_SCANCODE_D]) moveDir += rgt;
            if (ks[SDL_SCANCODE_A]) moveDir -= rgt;
            if (ks[SDL_SCANCODE_E]) moveDir += glm::vec3(0, 0, 1);
            if (ks[SDL_SCANCODE_Q]) moveDir -= glm::vec3(0, 0, 1);
            
            if (glm::length(moveDir) > 0.0f) {
                moveDir = glm::normalize(moveDir);
                cam.moveLocal(moveDir * speed);
            }
        }

        // ---- absorb finished async loads ---------------------------------
        for (int i = 0; i < uploadsPerFrame; ++i) {
            LoadResult res;
            if (!store.popResult(res)) break;
            renderer.upload(res.nodeIndex, res.verts);
        }

        // ---- LOD traversal: build visible set, request/draw ---------------
        cam.aspect = (winH > 0) ? (float)winW / (float)winH : 1.0f;
        glm::mat4 vp = cam.viewProj();
        Frustum frustum = extractFrustum(vp);
        const glm::dvec3 center = store.cubeCenter();
        const double ssFactor = (winH * 0.5) / std::tan(glm::radians(cam.fovY) * 0.5);

        size_t visibleNodes = 0, drawnNodes = 0;
        uint64_t drawnPoints = 0;

        shader.use();
        shader.setMat4("uMVP", glm::value_ptr(vp));
        shader.setFloat("uPointSize", pointSize);
        shader.setFloat("uAttenuation", attenuate ? 1.0f : 0.0f);
        shader.setFloat("uViewportH", (float)winH);
        shader.setInt("uRound", roundPoints ? 1 : 0);

        glViewport(0, 0, winW, winH);
        glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        std::function<void(uint32_t)> visit = [&](uint32_t idx) {
            const NodeRecord& rec = store.nodes()[idx];
            const NodeCube& nc = store.cube(idx);
            glm::vec3 mn((float)(nc.min[0] - center.x),
                         (float)(nc.min[1] - center.y),
                         (float)(nc.min[2] - center.z));
            glm::vec3 mx = mn + glm::vec3((float)nc.size);
            if (!aabbVisible(frustum, mn, mx)) return;

            ++visibleNodes;
            if (renderer.isResident(idx)) {
                renderer.draw(idx, frame);
                ++drawnNodes;
                drawnPoints += rec.pointCount;
            } else {
                store.requestLoad(idx);
            }

            // screen-space error: project this node's sample spacing to pixels
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

        renderer.evictToBudget((size_t)gpuBudgetMB * 1024u * 1024u, frame, store);

        // ---- ImGui overlay ------------------------------------------------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("PointForge");
        ImGui::Text("Cloud: %llu pts, %u nodes",
                    (unsigned long long)store.meta().pointCount, store.meta().nodeCount);
        ImGui::Separator();
        ImGui::Text("Visible nodes:  %zu", visibleNodes);
        ImGui::Text("Drawn nodes:    %zu", drawnNodes);
        ImGui::Text("Points on GPU:  %llu", (unsigned long long)renderer.pointsOnGpu());
        ImGui::Text("Drawn points:   %llu", (unsigned long long)drawnPoints);
        ImGui::Text("GPU resident:   %.1f MB", renderer.residentBytes() / (1024.0 * 1024.0));
        ImGui::Text("Load queue:     %zu", store.pendingRequests());
        ImGui::Text("FPS:            %.1f", dt > 0 ? 1.0f / dt : 0.0f);
        ImGui::Separator();
        ImGui::SliderFloat("Point size", &pointSize, 1.0f, 16.0f);
        ImGui::SliderFloat("LOD budget (px)", &sseBudget, 0.3f, 8.0f);
        ImGui::SliderInt("GPU budget (MB)", &gpuBudgetMB, 128, 8192);
        ImGui::SliderInt("Uploads/frame", &uploadsPerFrame, 1, 256);
        ImGui::Checkbox("Round points", &roundPoints);
        ImGui::SameLine();
        ImGui::Checkbox("Attenuate", &attenuate);
        ImGui::TextWrapped("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    // ---- shutdown ---------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
