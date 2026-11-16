#pragma once

#include <vector>
#include <cstdint>

#include "engine/render/mesh.hpp"

namespace resources{
    extern std::vector<uint16_t> rootBones;
    
    // Pool.
    extern std::vector<Mesh> meshes;
    extern std::vector<Texture> textures;
    extern std::vector<Bone> bones;
};