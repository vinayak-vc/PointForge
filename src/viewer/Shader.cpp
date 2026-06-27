#include "viewer/Shader.h"
#include "common/Log.h"
#include <fstream>
#include <sstream>
#include <vector>

namespace pf {

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { logError("Shader: cannot open " + path); return ""; }
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

GLuint Shader::compile(GLenum type, const std::string& src, const std::string& label) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        logError("Shader compile (" + label + "): " + std::string(log.data()));
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::loadFromFiles(const std::string& vertPath, const std::string& fragPath) {
    std::string vsrc = readFile(vertPath);
    std::string fsrc = readFile(fragPath);
    if (vsrc.empty() || fsrc.empty()) return false;
    return loadFromSource(vsrc, fsrc);
}

bool Shader::loadFromSource(const std::string& vsrc, const std::string& fsrc) {
    if (vsrc.empty() || fsrc.empty()) return false;

    GLuint vs = compile(GL_VERTEX_SHADER, vsrc, "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc, "fragment");
    if (!vs || !fs) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    GLint ok = 0; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        GLint len = 0; glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetProgramInfoLog(program_, len, nullptr, log.data());
        logError("Shader link: " + std::string(log.data()));
        glDeleteProgram(program_); program_ = 0;
        return false;
    }
    return true;
}

Shader::~Shader() { if (program_) glDeleteProgram(program_); }

void Shader::setMat4(const char* name, const float* m) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, m);
}
void Shader::setFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(program_, name), v);
}
void Shader::setInt(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(program_, name), v);
}
void Shader::setVec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(program_, name), x, y);
}
void Shader::setVec3(const char* name, float x, float y, float z) const {
    glUniform3f(glGetUniformLocation(program_, name), x, y, z);
}

} // namespace pf
