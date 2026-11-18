#include "ChunkRenderer.h"

#include <algorithm>

#include "Config.h"
#include "Faces.h"

vrtc::ChunkRenderer::ChunkRenderer()
    : quadShader(VRTC_ASSET_DIR "/shaders/chunk.vert", VRTC_ASSET_DIR "/shaders/chunk.frag") {
    uQuadViewProj = quadShader.Uniform("uViewProj");
    uQuadOrigin = quadShader.Uniform("uChunkOrigin");
    quadShader.SetInt(quadShader.Uniform("uTextures"), 0);
}

vrtc::ChunkRenderer::~ChunkRenderer() {
    for (Slot &slot: slots) {
        if (slot.vao) glDeleteVertexArrays(1, &slot.vao);
        if (slot.vbo) glDeleteBuffers(1, &slot.vbo);
    }
}

uint32_t vrtc::ChunkRenderer::Upload(const MeshOutput &mesh, uint32_t existing) {
    const auto quads = static_cast<uint32_t>(mesh.quads.size());
    if (quads == 0) {
        if (existing != NO_GPU_CHUNK) Free(existing);
        return NO_GPU_CHUNK;
    }

    uint32_t handle = existing;
    if (handle == NO_GPU_CHUNK) {
        if (!freeSlots.empty()) {
            handle = freeSlots.back();
            freeSlots.pop_back();
        } else {
            handle = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }
        Slot &slot = slots[handle];
        if (!slot.vao) {
            // One ChunkQuad (three uint32) per instance.
            glCreateVertexArrays(1, &slot.vao);
            glCreateBuffers(1, &slot.vbo);
            glVertexArrayVertexBuffer(slot.vao, 0, slot.vbo, 0, sizeof(ChunkQuad));
            glEnableVertexArrayAttrib(slot.vao, 0);
            glVertexArrayAttribIFormat(slot.vao, 0, 3, GL_UNSIGNED_INT, 0);
            glVertexArrayAttribBinding(slot.vao, 0, 0);
            glVertexArrayBindingDivisor(slot.vao, 0, 1);
        }
        slot.live = true;
        ++liveCount;
    }

    Slot &slot = slots[handle];
    gpuBytes -= slot.bytes;
    storedQuads -= slot.quads;
    slot.quads = quads;
    slot.pos = mesh.pos;
    slot.faceStart = mesh.faceStart;
    slot.bytes = mesh.quads.size() * sizeof(ChunkQuad);
    glNamedBufferData(slot.vbo, static_cast<GLsizeiptr>(slot.bytes), mesh.quads.data(), GL_STATIC_DRAW);
    gpuBytes += slot.bytes;
    storedQuads += quads;
    return handle;
}

void vrtc::ChunkRenderer::Free(uint32_t handle) {
    if (handle == NO_GPU_CHUNK || handle >= slots.size() || !slots[handle].live) return;
    Slot &slot = slots[handle];
    // Release the storage now so memory stays flat while streaming; the buffer object itself is reused.
    glNamedBufferData(slot.vbo, 0, nullptr, GL_STATIC_DRAW);
    gpuBytes -= slot.bytes;
    storedQuads -= slot.quads;
    slot.bytes = 0;
    slot.quads = 0;
    slot.live = false;
    --liveCount;
    freeSlots.push_back(handle);
}

vrtc::ChunkRenderer::DrawResult vrtc::ChunkRenderer::Draw(const glm::mat4 &viewProj, const Frustum &frustum,
                                                           ChunkPos cameraChunk, glm::vec3 eye,
                                                           TextureManager &textures, TextureHandle terrain,
                                                           const TerrainLighting &lighting) {
    DrawResult result;
    drawOrder.clear();
    const auto originOf = [&](const Slot &slot) {
        return glm::vec3(static_cast<float>((slot.pos.x - cameraChunk.x) * CHUNK_SIZE),
                         static_cast<float>((slot.pos.y - cameraChunk.y) * CHUNK_SIZE),
                         static_cast<float>((slot.pos.z - cameraChunk.z) * CHUNK_SIZE));
    };
    for (uint32_t i = 0; i < slots.size(); ++i) {
        const Slot &slot = slots[i];
        if (!slot.live) continue;
        const glm::vec3 origin = originOf(slot);
        if (!frustum.IntersectsBox(origin, origin + glm::vec3(CHUNK_SIZE))) {
            ++result.culled;
            continue;
        }
        const glm::vec3 centre = origin + glm::vec3(CHUNK_SIZE * 0.5f) - eye;
        drawOrder.emplace_back(glm::dot(centre, centre), i);
    }
    // Front to back, so the depth test rejects hidden fragments early.
    std::ranges::sort(drawOrder, [](const auto &a, const auto &b) { return a.first < b.first; });

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    textures.Bind(terrain, 0);
    quadShader.Bind();
    quadShader.SetMat4(uQuadViewProj, viewProj);
    quadShader.SetVec3(quadShader.Uniform("uEye"), eye);
    quadShader.SetVec3(quadShader.Uniform("uSunDir"), lighting.sunDir);
    quadShader.SetVec3(quadShader.Uniform("uSunColour"), lighting.sunColour);
    quadShader.SetVec3(quadShader.Uniform("uAmbient"), lighting.ambient);
    quadShader.SetVec3(quadShader.Uniform("uFogColour"), lighting.fogColour);
    glProgramUniform2f(quadShader.Id(), quadShader.Uniform("uFogRange"), lighting.fogStart, lighting.fogEnd);

    for (const auto &[distance, i]: drawOrder) {
        const Slot &slot = slots[i];
        const glm::vec3 origin = originOf(slot);
        // A face pointing along +axis can only be seen from beyond the chunk's near side on that axis, and the
        // reverse for -axis. Face order: +Z -Z +X -X +Y -Y.
        const glm::vec3 lo = origin, hi = origin + glm::vec3(CHUNK_SIZE);
        const bool faceVisible[FACE_COUNT] = {
            eye.z > lo.z, eye.z < hi.z, eye.x > lo.x, eye.x < hi.x, eye.y > lo.y, eye.y < hi.y,
        };
        bool bound = false;
        // Faces are stored in order, so adjacent visible directions merge into one draw.
        for (int f = 0; f < FACE_COUNT;) {
            if (!faceVisible[f]) {
                result.quadsSkipped += slot.faceStart[f + 1] - slot.faceStart[f];
                ++f;
                continue;
            }
            int end = f + 1;
            while (end < FACE_COUNT && faceVisible[end]) ++end;
            const uint32_t first = slot.faceStart[f], count = slot.faceStart[end] - first;
            if (count > 0) {
                if (!bound) {
                    glBindVertexArray(slot.vao);
                    quadShader.SetVec3(uQuadOrigin, origin);
                    bound = true;
                }
                glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count), first);
                ++result.drawCalls;
                result.quads += count;
            }
            f = end;
        }
        if (bound) ++result.drawn;
    }
    glBindVertexArray(0);
    return result;
}
