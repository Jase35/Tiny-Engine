#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#include "engine/render/mesh.hpp"

namespace bones{
    // Adds a bone to resources::bones under `parent` (or as a root if NO_BONE). Returns its index.
    uint16_t create(uint16_t parent, const mat4& localTransform);

    // Recomputes every bone's globalTransform by walking the hierarchy from resources::rootBones.
    void updateGlobals();

    // Captures the current global transforms as the bind pose.
    void setBindPose();

    // Per-bone skinning matrices (globalTransform * inverseBind), indexed like resources::bones.
    std::vector<mat4> skinMatrices();
};
