#include "mesh.hpp"

Mesh::Mesh(uint16_t texture) : texture(texture){
    uint16_t non = 0xFFFF;

    vertices = {
        {{0.4f, 0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {non, non, non, non}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{0.45f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {non, non, non, non}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{0.6f, 0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {non, non, non, non}, {0.0f, 0.0f, 0.0f, 0.0f}},

        {{0.4f, 0.6f, 0.5f}, {0.0f, 0.0f, 1.0f}, {non, non, non, non}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{0.5f, 0.35f, -0.5f}, {0.0f, 0.0f, 1.0f}, {non, non, non, non}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{0.6f, 0.6f, -0.5f}, {0.0f, 0.0f, 1.0f}, {non, non, non, non}, {0.0f, 0.0f, 0.0f, 0.0f}}
    };

    indices = {0, 1, 2, 3, 4, 5};

    uvs = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 0.0f},

        {0.0f, 1.0f},
        {0.1f, 0.0f},
        {0.1f, 1.0f}
    };
}