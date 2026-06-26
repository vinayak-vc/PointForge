import re

with open("src/viewer/main.cpp", "r", encoding="utf-8") as f:
    code = f.read()

# 1. Add stereoSBS and eyeSeparation variables
var_block = """    std::atomic<bool> convertSuccess(false);
    std::thread convertThread;
    
    bool stereoSBS = false;
    float eyeSeparation = 0.05f;"""

code = code.replace("    std::atomic<bool> convertSuccess(false);\n    std::thread convertThread;", var_block)

# 2. Add UI toggles to ImGui Viewer section
# Find the end of Viewer section, right before ImGui::TextWrapped("RMB drag...
ui_toggles = """                ImGui::Checkbox("Round points", &roundPoints);
                ImGui::SameLine();
                ImGui::Checkbox("Attenuate", &attenuate);
                ImGui::Separator();
                ImGui::Checkbox("Stereoscopic (SBS)", &stereoSBS);
                if (stereoSBS) {
                    ImGui::SliderFloat("Eye Separation (IPD)", &eyeSeparation, 0.01f, 0.2f);
                }
                ImGui::Separator();
                ImGui::TextWrapped("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");"""

code = code.replace("""                ImGui::Checkbox("Round points", &roundPoints);
                ImGui::SameLine();
                ImGui::Checkbox("Attenuate", &attenuate);
                ImGui::TextWrapped("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast");""", ui_toggles)

# 3. Replace the rendering block
render_code = """        if (octreeLoaded) {
            glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
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
                const glm::dvec3 center = store.cubeCenter();
                const double ssFactor = (vpH * 0.5) / std::tan(glm::radians(cam.fovY) * 0.5);

                shader.use();
                shader.setMat4("uMVP", glm::value_ptr(vp));
                shader.setFloat("uPointSize", pointSize);
                shader.setFloat("uAttenuation", attenuate ? 1.0f : 0.0f);
                shader.setFloat("uViewportH", (float)vpH);
                shader.setInt("uRound", roundPoints ? 1 : 0);

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
            glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }"""

code = re.sub(
    r"        size_t visibleNodes = 0, drawnNodes = 0;\n        uint64_t drawnPoints = 0;.*?        } else \{",
    """        size_t visibleNodes = 0, drawnNodes = 0;
        uint64_t drawnPoints = 0;
""" + render_code + "\n\n        if (false) {",
    code,
    flags=re.DOTALL
)

# Fix the fact that my regex replaced the `} else {` of `if (octreeLoaded)` which doesn't exist in the current codebase!
# Wait, let's look at the original code again carefully.
"""
        size_t visibleNodes = 0, drawnNodes = 0;
        uint64_t drawnPoints = 0;

        glViewport(0, 0, winW, winH);
        glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (octreeLoaded) {
            cam.aspect = (winH > 0) ? (float)winW / (float)winH : 1.0f;
...
            visit(store.rootIndex());

            renderer.evictToBudget((size_t)gpuBudgetMB * 1024u * 1024u, frame, store);
        }
"""
# So I should regex from `size_t visibleNodes = 0` to `renderer.evictToBudget(...); \n        }`

render_code_final = """        size_t visibleNodes = 0, drawnNodes = 0;
        uint64_t drawnPoints = 0;

""" + render_code

code = re.sub(
    r"        size_t visibleNodes = 0, drawnNodes = 0;\n        uint64_t drawnPoints = 0;\n\n        glViewport.*?renderer\.evictToBudget\(\(size_t\)gpuBudgetMB \* 1024u \* 1024u, frame, store\);\n        \}",
    render_code_final,
    code,
    flags=re.DOTALL
)

with open("src/viewer/main.cpp", "w", encoding="utf-8") as f:
    f.write(code)
