#pragma once

#include <vector>
#include <cstdint>
#include <limits>
#include <glm/glm.hpp>

#include "mesh.hpp"
#include "shader.hpp"
#include "engine/resources.hpp"

using glm::vec2;
using glm::vec3;
using glm::uvec2;

namespace renderer{
    // Cleared depth value; anything at or beyond the far plane never passes the test.
    constexpr uint16_t DEPTH_CLEAR = 0xFFFF;

    // Perspective-correct UVs are computed exactly every this many pixels along a row and
    // interpolated linearly in between, trading a per-pixel divide for one per span.
    constexpr int32_t PERSPECTIVE_SPAN = 8;

    // Rows are dealt out to rasterizer threads in blocks of this many, round-robin.
    // Interleaving keeps the load balanced when the model only covers part of the screen.
    constexpr int ROW_BLOCK = 16;

    // Screen-space vertices are clamped to [-GUARD_BAND, GUARD_BAND] pixels. Edge function
    // products are then at most (2 * GUARD_BAND)^2 = 2^28 each, comfortably within int32_t.
    constexpr float GUARD_BAND = 8192.0f;

    // A vertex after shadeVertices: everything the rasterizer needs, already in pixel space.
    struct ScreenVertex{
        glm::ivec2 pixel;
        float z; // NDC depth, [0, 1] between the near and far planes.
        VertexData data;
    };

    // The rows a rasterizeMesh call may write: block b = y / ROW_BLOCK belongs to it when
    // b % stride == offset. Threads with distinct offsets never touch the same pixel.
    struct RowSet{
        int offset = 0;
        int stride = 1;
    };

    // Per-vertex working memory for drawMesh. Keep one alive and reuse it so the vector
    // stops allocating once it's grown to the largest mesh (or call reserve() up front).
    struct Scratch{
        std::vector<ScreenVertex> vertices;

        void reserve(size_t vertexCount){
            if(vertices.size() < vertexCount){
                vertices.resize(vertexCount);
            }
        }
    };

    // Transforms mesh.vertices[begin, end) into out[begin, end). Disjoint ranges can run in parallel.
    void shadeVertices(
        const Mesh& mesh,
        const GlobalData& globalData,
        const ModelData& modelData,
        int width,
        int height,
        ScreenVertex* out,
        size_t begin,
        size_t end
    );

    // Draws the mesh's triangles from already-shaded vertices, writing only the rows in `rows`.
    // colorBuffer is RGB565. zBuffer holds NDC depth [0, 1] quantized to [0, DEPTH_CLEAR - 1].
    void rasterizeMesh(
        const Mesh& mesh,
        const ModelData& modelData,
        const ScreenVertex* vertices,
        std::vector<uint16_t>& colorBuffer,
        std::vector<uint16_t>& zBuffer,
        int width,
        int height,
        RowSet rows = {}
    );

    // Single-threaded convenience: shadeVertices + rasterizeMesh over the whole screen.
    void drawMesh(
        const Mesh& mesh,
        const GlobalData& globalData,
        const ModelData& modelData,
        Scratch& scratch,
        std::vector<uint16_t>& colorBuffer,
        std::vector<uint16_t>& zBuffer,
        int width,
        int height
    );
};