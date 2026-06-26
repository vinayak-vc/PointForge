import re

with open("src/viewer/main.cpp", "r", encoding="utf-8") as f:
    code = f.read()

# 1. Includes
code = code.replace('#include "common/OctreeFormat.h"',
"""#include "common/OctreeFormat.h"
#include "common/FileDialog.h"
#include "indexer/OctreeIndexer.h"
#include <thread>
#include <atomic>""")

# 2. Main args
code = code.replace('if (argc < 2) { std::printf("Usage: pfview <octree-dir>\\n"); return 1; }\n    const std::string dir = argv[1];',
'std::string initialDir = "";\n    if (argc >= 2) initialDir = argv[1];')

# 3. Store load
code = code.replace('if (!store.load(dir)) { logError("Failed to load octree from " + dir); return 2; }',
"""bool octreeLoaded = false;
    if (!initialDir.empty()) {
        if (store.load(initialDir)) { octreeLoaded = true; }
        else { logError("Failed to load octree from " + initialDir); }
    }""")

# 4. Camera setup
code = code.replace("""    Camera cam;
    const double cubeSize = store.meta().cubeSize;
    cam.position = glm::vec3(0.0f, -(float)cubeSize, 0.0f);
    cam.yaw = 0.0f; cam.pitch = 0.0f;
    cam.nearZ = (float)std::max(0.01, cubeSize / 5000.0);
    cam.farZ  = (float)(cubeSize * 8.0);
    cam.moveSpeed = (float)(cubeSize / 8.0);""",
"""    Camera cam;
    auto setupCamera = [&]() {
        const double cubeSize = store.meta().cubeSize;
        cam.position = glm::vec3(0.0f, -(float)cubeSize, 0.0f);
        cam.yaw = 0.0f; cam.pitch = 0.0f;
        cam.nearZ = (float)std::max(0.01, cubeSize / 5000.0);
        cam.farZ  = (float)(cubeSize * 8.0);
        cam.moveSpeed = (float)(cubeSize / 8.0);
    };
    if (octreeLoaded) setupCamera();""")

# 5. UI variables
code = code.replace("""    bool running = true;
    bool mouseLook = false;
    uint64_t frame = 0;
    Uint64 prevTicks = SDL_GetPerformanceCounter();""",
"""    bool running = true;
    bool mouseLook = false;
    uint64_t frame = 0;
    Uint64 prevTicks = SDL_GetPerformanceCounter();
    
    std::string convInput = "";
    std::string convOutput = "";
    int presetIdx = 1;
    std::atomic<bool> isConverting(false);
    std::atomic<bool> convertDone(false);
    std::atomic<bool> convertSuccess(false);
    std::thread convertThread;""")

# 6. Keyboard capture logic
code = code.replace('if (!ImGui::GetIO().WantCaptureKeyboard) {', 'if (!ImGui::GetIO().WantCaptureKeyboard && octreeLoaded) {')

# 7. Render block (the whole thing)
render_code = """        // ---- absorb finished async loads ---------------------------------
        if (octreeLoaded) {
            for (int i = 0; i < uploadsPerFrame; ++i) {
                LoadResult res;
                if (!store.popResult(res)) break;
                renderer.upload(res.nodeIndex, res.verts);
            }
        }

        size_t visibleNodes = 0, drawnNodes = 0;
        uint64_t drawnPoints = 0;

        glViewport(0, 0, winW, winH);
        glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (octreeLoaded) {
            cam.aspect = (winH > 0) ? (float)winW / (float)winH : 1.0f;
            glm::mat4 vp = cam.viewProj();
            Frustum frustum = extractFrustum(vp);
            const glm::dvec3 center = store.cubeCenter();
            const double ssFactor = (winH * 0.5) / std::tan(glm::radians(cam.fovY) * 0.5);

            shader.use();
            shader.setMat4("uMVP", glm::value_ptr(vp));
            shader.setFloat("uPointSize", pointSize);
            shader.setFloat("uAttenuation", attenuate ? 1.0f : 0.0f);
            shader.setFloat("uViewportH", (float)winH);
            shader.setInt("uRound", roundPoints ? 1 : 0);

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
        }"""
        
# Regex to replace the rendering block
code = re.sub(
    r"// ---- absorb finished async loads ---------------------------------.*?renderer\.evictToBudget\(\(size_t\)gpuBudgetMB \* 1024u \* 1024u, frame, store\);",
    render_code,
    code,
    flags=re.DOTALL
)

# 8. ImGui UI
ui_code = """        // ---- ImGui overlay ------------------------------------------------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (convertDone) {
            convertDone = false;
            if (convertThread.joinable()) convertThread.join();
            if (convertSuccess && !convOutput.empty()) {
                if (store.load(convOutput)) {
                    octreeLoaded = true;
                    setupCamera();
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, octreeLoaded ? 500.0f : 350.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("PointForge Dashboard");
        
        if (ImGui::CollapsingHeader("Viewer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Browse & Load Octree Folder...")) {
                std::string folder = pf::openFolderDialog();
                if (!folder.empty()) {
                    if (store.load(folder)) {
                        octreeLoaded = true;
                        setupCamera();
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
                ImGui::SliderFloat("Point size", &pointSize, 1.0f, 16.0f);
                ImGui::SliderFloat("LOD budget (px)", &sseBudget, 0.3f, 8.0f);
                ImGui::SliderInt("GPU budget (MB)", &gpuBudgetMB, 128, 8192);
                ImGui::SliderInt("Uploads/frame", &uploadsPerFrame, 1, 256);
                ImGui::Checkbox("Round points", &roundPoints);
                ImGui::SameLine();
                ImGui::Checkbox("Attenuate", &attenuate);
                ImGui::TextWrapped("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");
            } else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "No octree loaded. Load or convert one.");
            }
        }
        
        if (ImGui::CollapsingHeader("Converter", octreeLoaded ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            if (isConverting) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Converting in background...");
                ImGui::TextWrapped("Please wait, converting large files can take some time.");
            } else {
                if (ImGui::Button("Select Input...")) {
                    std::string f = pf::openFileDialog("Point Clouds\\0*.las;*.laz;*.e57;*.ply;*.pts;*.xyz\\0All Files\\0*.*\\0");
                    if (!f.empty()) convInput = f;
                }
                ImGui::TextWrapped("Input: %s", convInput.empty() ? "(none)" : convInput.c_str());
                
                if (ImGui::Button("Select Output Folder...")) {
                    std::string f = pf::openFolderDialog();
                    if (!f.empty()) convOutput = f;
                }
                ImGui::TextWrapped("Output: %s", convOutput.empty() ? "(none)" : convOutput.c_str());
                
                const char* presets[] = { "High Quality", "Medium (Balanced)", "Low (Fast)" };
                ImGui::Combo("Preset", &presetIdx, presets, 3);
                
                if (ImGui::Button("Convert!", ImVec2(-1, 30))) {
                    if (!convInput.empty() && !convOutput.empty()) {
                        isConverting = true;
                        convertDone = false;
                        pf::IndexOptions opts;
                        if (presetIdx == 0) {
                            opts.targetLeafSize = 20000; opts.rootSpacing = 0.0; opts.compress = true;
                        } else if (presetIdx == 1) {
                            opts.targetLeafSize = 50000; opts.rootSpacing = 0.0; opts.compress = true;
                        } else {
                            opts.targetLeafSize = 100000; opts.rootSpacing = 0.05; opts.compress = false;
                        }
                        
                        convertThread = std::thread([input = convInput, outDir = convOutput, opts]() {
                            bool success = pf::buildOctree(input, outDir, opts);
                            convertSuccess = success;
                            isConverting = false;
                            convertDone = true;
                        });
                    }
                }
            }
        }
        ImGui::End();

        ImGui::Render();"""

code = re.sub(
    r"// ---- ImGui overlay ------------------------------------------------.*?ImGui::Render\(\);",
    ui_code,
    code,
    flags=re.DOTALL
)

# 9. Thread joining
code = code.replace("""    // ---- shutdown ---------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();""",
"""    // ---- shutdown ---------------------------------------------------------
    if (convertThread.joinable()) {
        convertThread.join();
    }
    ImGui_ImplOpenGL3_Shutdown();""")

with open("src/viewer/main.cpp", "w", encoding="utf-8") as f:
    f.write(code)
