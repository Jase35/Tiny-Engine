#pragma once

#include <cstddef>
#include <cstdint>

/*
    Package binary format (".mshp"), all values little-endian, tightly packed (no padding).
    Produced by tools/glb2bin.py.

    Header:
        char[4]  magic = "MSHP"
        u16      version = 2
        u16      textureCount
        u16      boneCount
        u16      meshCount

    Texture (x textureCount):
        u16      width
        u16      height
        u16[w*h] pixels, RGB565 (r << 11 | g << 5 | b), row-major, first row is v = 0

    Bone (x boneCount), every parent precedes its children:
        u16      parent       file-relative bone index, 0xFFFF = root
        f32[16]  transform    local transform relative to parent, column-major
        f32[16]  inverseBind  column-major

    Mesh (x meshCount):
        u16      texture      file-relative texture index, 0xFFFF = none
        u32      vertexCount
        u32      indexCount
        Vertex (x vertexCount):
            f32[3]  position
            f32[3]  normal
            u16[4]  boneIndices   file-relative bone indices, 0xFFFF = unused
            f32[4]  boneWeights
            f32[2]  uv
        u32[indexCount] indices   triangle list
*/

namespace package{
    constexpr uint16_t VERSION = 2;

    // Where the loaded items landed in the resources:: pools.
    struct LoadResult{
        size_t firstTexture, textureCount;
        size_t firstBone, boneCount;
        size_t firstMesh, meshCount;
    };

    // Appends the package's textures, bones and meshes to resources::textures/bones/meshes,
    // rebasing file-relative indices. Throws std::runtime_error on malformed data (pools are left untouched).
    LoadResult load(const uint8_t* data, size_t size);
};
