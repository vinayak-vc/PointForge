#pragma once
#include <GL/glew.h>
#include <string>

namespace pf {

// Minimal GLSL program wrapper: compile a vertex + fragment shader from files,
// link, and set uniforms. GLSL is "#version 330 core", matching the original app.
class Shader {
public:
    bool loadFromFiles(const std::string& vertPath, const std::string& fragPath);
    // Compile + link from in-memory GLSL source (used for shaders embedded in
    // the binary, so the viewer needs no external shaders/ folder).
    bool loadFromSource(const std::string& vertSrc, const std::string& fragSrc);
    void use() const { glUseProgram(program_); }
    GLuint id() const { return program_; }
    ~Shader();

    void setMat4(const char* name, const float* m) const;
    void setFloat(const char* name, float v) const;
    void setInt(const char* name, int v) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec3(const char* name, float x, float y, float z) const;

private:
    static GLuint compile(GLenum type, const std::string& src, const std::string& label);
    GLuint program_ = 0;
};

} // namespace pf
