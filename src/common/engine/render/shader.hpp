#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#include "mesh.hpp"

using glm::vec2;
using glm::vec3;
using glm::mat4;

struct GlobalData{
    mat4 viewMatrix;
    mat4 projectionMatrix;
    std::vector<mat4> boneMatrices; // Skinning matrices, indexed by Vertex::boneIndices.
};

struct ModelData{
    mat4 modelMatrix;
    // Skip triangles that are clockwise on screen (glTF convention: counter-clockwise = front).
    bool cullBackFaces = true;
};

// Per-vertex outputs of shadeVertex, interpolated for shadeFragment.
struct VertexData{
    vec2 uv;
    float invW; // 1 / clip-space w, for perspective-correct interpolation.
};

// modelViewProjection is projection * view * model, computed once per mesh by the renderer.
// Skinning is applied on top only for vertices with at least one bone influence.
Vertex shadeVertex(
    const Vertex& vertex,
    const vec2& uv,
    const mat4& modelViewProjection,
    const GlobalData& globalData,
    VertexData& vertexData
);

// Returns an RGB565 color. uv is already perspective-corrected by the rasterizer.
// texture is either null or a valid texture (checked once per mesh by the renderer).
uint16_t shadeFragment(vec2 uv, const Texture* texture);