#pragma once
// GL program wrapper. Section 19 fixes: deletes the program, non-copyable, caches uniform locations, stops on
// compile or link failure.
#include <string>
#include <unordered_map>
#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace vrtc {
    class Shader {
        GLuint program = 0;
        std::string name;
        std::unordered_map<std::string, GLint> uniforms;

        static GLuint Compile(GLenum stage, const char *path);

    public:
        Shader(const char *vertexPath, const char *fragmentPath);

        ~Shader();

        Shader(const Shader &) = delete;

        Shader &operator=(const Shader &) = delete;

        void Bind() const { glUseProgram(program); }

        // Cached after the first lookup. -1 (with a warning once) when the shader has no such uniform.
        GLint Uniform(const char *uniformName);

        void SetMat4(GLint location, const glm::mat4 &value) const {
            glProgramUniformMatrix4fv(program, location, 1, GL_FALSE, &value[0][0]);
        }

        void SetVec3(GLint location, glm::vec3 value) const { glProgramUniform3f(program, location, value.x, value.y, value.z); }

        void SetInt(GLint location, int value) const { glProgramUniform1i(program, location, value); }

        void SetFloat(GLint location, float value) const { glProgramUniform1f(program, location, value); }

        [[nodiscard]] GLuint Id() const { return program; }
    };
}
