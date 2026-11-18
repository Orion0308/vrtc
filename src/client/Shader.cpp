#include "Shader.h"

#include <fstream>
#include <sstream>

#include "Log.h"

GLuint vrtc::Shader::Compile(GLenum stage, const char *path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) log::Fatal("shader: cannot open '{}'", path);
    std::stringstream source;
    source << file.rdbuf();
    const std::string code = source.str();
    const char *text = code.c_str();

    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[2048];
        glGetShaderInfoLog(shader, sizeof info, nullptr, info);
        log::Fatal("shader: compile error in '{}':\n{}", path, info);
    }
    return shader;
}

vrtc::Shader::Shader(const char *vertexPath, const char *fragmentPath) : name(vertexPath) {
    const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexPath);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentPath);
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[2048];
        glGetProgramInfoLog(program, sizeof info, nullptr, info);
        log::Fatal("shader: link error for '{}' + '{}':\n{}", vertexPath, fragmentPath, info);
    }
}

vrtc::Shader::~Shader() {
    if (program) glDeleteProgram(program);
}

GLint vrtc::Shader::Uniform(const char *uniformName) {
    const auto it = uniforms.find(uniformName);
    if (it != uniforms.end()) return it->second;
    const GLint location = glGetUniformLocation(program, uniformName);
    if (location < 0) log::Warn("shader: '{}' has no uniform '{}'", name, uniformName);
    uniforms.emplace(uniformName, location);
    return location;
}
