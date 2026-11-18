#include "Renderer.h"

#include <algorithm>

#include "Shader.h"

vrtc::Renderer::Renderer(const MaterialManager &materials, TextureManager &textures)
    : materials(materials), textures(textures) {
}

vrtc::Renderer::~Renderer() {
    for (auto &mesh: meshes) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ebo);
    }
}

vrtc::MeshHandle vrtc::Renderer::CreateMesh(std::span<const EntityVertex> vertices, std::span<const uint32_t> indices,
                                            GLenum primitive) {
    Mesh mesh;
    mesh.indexCount = static_cast<GLsizei>(indices.size());
    mesh.primitive = primitive;
    mesh.bytes = vertices.size_bytes() + indices.size_bytes();

    glCreateVertexArrays(1, &mesh.vao);
    glCreateBuffers(1, &mesh.vbo);
    glCreateBuffers(1, &mesh.ebo);
    glNamedBufferData(mesh.vbo, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STATIC_DRAW);
    glNamedBufferData(mesh.ebo, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(), GL_STATIC_DRAW);

    glVertexArrayVertexBuffer(mesh.vao, 0, mesh.vbo, 0, sizeof(EntityVertex));
    glVertexArrayElementBuffer(mesh.vao, mesh.ebo);
    const auto attribute = [&](GLuint index, GLint size, GLuint offset) {
        glEnableVertexArrayAttrib(mesh.vao, index);
        glVertexArrayAttribFormat(mesh.vao, index, size, GL_FLOAT, GL_FALSE, offset);
        glVertexArrayAttribBinding(mesh.vao, index, 0);
    };
    attribute(0, 3, offsetof(EntityVertex, position));
    attribute(1, 3, offsetof(EntityVertex, normal));
    attribute(2, 2, offsetof(EntityVertex, uv));
    attribute(3, 1, offsetof(EntityVertex, layer));

    meshBytes += mesh.bytes;
    meshes.push_back(mesh);
    return static_cast<MeshHandle>(meshes.size() - 1);
}

void vrtc::Renderer::Flush(const glm::mat4 &viewProj) {
    std::ranges::sort(commands, [](const DrawCommand &a, const DrawCommand &b) { return a.sortKey < b.sortKey; });

    Shader *boundShader = nullptr;
    GLint mvpLocation = -1, useTextureLocation = -1, colourLocation = -1;
    TextureHandle boundTexture = NO_TEXTURE;
    MaterialHandle boundMaterial = ~0u;
    GLuint boundVao = 0;
    stats = {};

    for (const DrawCommand &command: commands) {
        const Material &material = materials.Get(command.material);
        const Mesh &mesh = meshes[command.mesh];

        if (material.shader != boundShader) {
            boundShader = material.shader;
            boundShader->Bind();
            boundShader->SetInt(boundShader->Uniform("uTextures"), 0);
            mvpLocation = boundShader->Uniform("uMVP");
            useTextureLocation = boundShader->Uniform("uUseTexture");
            colourLocation = boundShader->Uniform("uColour");
            boundMaterial = ~0u;
            ++stats.shaderBinds;
        }
        if (command.material != boundMaterial) {
            boundMaterial = command.material;
            boundShader->SetInt(useTextureLocation, material.texture != NO_TEXTURE ? 1 : 0);
            glProgramUniform4f(boundShader->Id(), colourLocation, material.colour.r, material.colour.g,
                               material.colour.b, material.colour.a);
        }
        if (material.texture != boundTexture && material.texture != NO_TEXTURE) {
            textures.Bind(material.texture, 0);
            boundTexture = material.texture;
            ++stats.textureBinds;
        }
        boundShader->SetMat4(mvpLocation, viewProj * command.transform);
        if (mesh.vao != boundVao) {
            glBindVertexArray(mesh.vao);
            boundVao = mesh.vao;
        }
        glDrawElements(mesh.primitive, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        ++stats.drawCalls;
        if (mesh.primitive == GL_TRIANGLES) stats.triangles += mesh.indexCount / 3;
    }
    glBindVertexArray(0);
    commands.clear();
}
