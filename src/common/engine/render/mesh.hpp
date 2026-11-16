#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::u16vec4;
using glm::mat4;

// Sentinel for "no bone" in bone indices and hierarchy links.
constexpr uint16_t NO_BONE = 0xFFFF;
// Sentinel for "no texture" on a mesh.
constexpr uint16_t NO_TEXTURE = 0xFFFF;

struct Vertex{
    vec3 position;
    vec3 normal;
    u16vec4 boneIndices; // Indices into resources::bones, NO_BONE if unused.
    vec4 boneWeights;
};

struct Bone{
    mat4 transform{1.0f};       // Local transform relative to the parent bone (animated).
    mat4 globalTransform{1.0f}; // Model-space transform, computed by bones::updateGlobals().
    mat4 inverseBind{1.0f};     // Inverse of globalTransform at bind time.

    uint16_t parent = NO_BONE;
    uint16_t firstChild = NO_BONE;
    uint16_t nextSibling = NO_BONE;
};

// Transforms position as a point (with w-divide) and normal as a direction (w = 0).
inline Vertex operator*(const glm::mat4& m, const Vertex& v){
    const glm::vec4 p = m * glm::vec4(v.position, 1.0f);
    Vertex out = v;
    out.position = vec3(p) / p.w;
    out.normal = vec3(m * glm::vec4(v.normal, 0.0f));
    return out;
}

struct Texture{
    uint16_t width;
    uint16_t height;
    std::vector<uint16_t> data; // RGB565, row-major.
};

class Mesh{
    public:
    Mesh() = default;
    Mesh(uint16_t texture);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<vec2> uvs;

    uint16_t texture = NO_TEXTURE;
};