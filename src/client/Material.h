#pragma once
// Shader, texture array and tint for the entity renderer. A material without a texture draws in its flat colour,
// which is what debug outlines use.
#include <cstdint>
#include <vector>
#include <glm/vec4.hpp>

#include "Texture.h"

namespace vrtc {
    class Shader;
    using MaterialHandle = uint32_t;

    struct Material {
        Shader *shader; // not owned, must outlive the manager
        TextureHandle texture; // array sampled with each vertex's layer, or NO_TEXTURE
        glm::vec4 colour; // multiplied in; the whole colour when there is no texture
    };

    class MaterialManager {
        std::vector<Material> materials;

    public:
        MaterialHandle Create(Shader &shader, TextureHandle texture = NO_TEXTURE, glm::vec4 colour = glm::vec4(1.f)) {
            materials.push_back({&shader, texture, colour});
            return static_cast<MaterialHandle>(materials.size() - 1);
        }

        [[nodiscard]] const Material &Get(MaterialHandle handle) const { return materials[handle]; }
    };
}
