#include "shader.hpp"

// Linear blend skinning: weighted sum of the influencing bones' matrices (weights normalized).
// Returns false, leaving skin untouched, when no bone influences the vertex.
static bool skinMatrix(const Vertex& vertex, const std::vector<mat4>& boneMatrices, mat4& skin){
    float totalWeight = 0.0f;
    for(int k = 0; k < 4; k++){
        const uint16_t bone = vertex.boneIndices[k];
        const float weight = vertex.boneWeights[k];
        if(bone == NO_BONE || bone >= boneMatrices.size() || weight <= 0.0f){
            continue;
        }
        if(totalWeight == 0.0f){
            skin = mat4(0.0f);
        }
        skin += boneMatrices[bone] * weight;
        totalWeight += weight;
    }
    if(totalWeight <= 0.0f){
        return false;
    }
    skin *= 1.0f / totalWeight;
    return true;
}

Vertex shadeVertex(
    const Vertex& vertex,
    const vec2& uv,
    const mat4& modelViewProjection,
    const GlobalData& globalData,
    VertexData& vertexData
){
    mat4 skin;
    const bool skinned = skinMatrix(vertex, globalData.boneMatrices, skin);
    const mat4 mvp = skinned ? modelViewProjection * skin : modelViewProjection;
    const glm::vec4 clip = mvp * glm::vec4(vertex.position, 1.0f);

    vertexData.uv = uv;
    vertexData.invW = 1.0f / clip.w;

    Vertex out = vertex;
    out.position = vec3(clip) * vertexData.invW;
    out.normal = vec3(mvp * glm::vec4(vertex.normal, 0.0f));
    return out;
}

// Nearest-neighbour sample with repeat wrapping. v = 0 is the first (top) row of texture data.
static uint16_t sampleTexture(const Texture& texture, vec2 uv){
    uv -= glm::floor(uv);
    const int x = glm::min(static_cast<int>(uv.x * texture.width), texture.width - 1);
    const int y = glm::min(static_cast<int>(uv.y * texture.height), texture.height - 1);
    return texture.data[static_cast<size_t>(y) * texture.width + x];
}

uint16_t shadeFragment(vec2 uv, const Texture* texture){
    if(texture){
        return sampleTexture(*texture, uv);
    }

    /*vec3 color = v0.color * bary.x + v1.color * bary.y + v2.color * bary.z;
    // Clamp so a channel at exactly 1.0 can't overflow into the neighbouring byte.
    glm::uvec3 c = glm::uvec3(glm::clamp(color, 0.0f, 1.0f) * 255.0f + 0.5f);
    return 0xFF000000
        | (c.x << 16)
        | (c.y << 8)
        | c.z;*/

    return 0x07FF; // Cyan.
}