#include "package.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/bones.hpp"
#include "engine/resources.hpp"

namespace package{
    namespace{
        class Reader{
            public:
            Reader(const uint8_t* data, size_t size) : data(data), size(size){}

            void need(size_t bytes) const {
                if(bytes > size - offset){
                    throw std::runtime_error("package: unexpected end of data at byte " + std::to_string(offset));
                }
            }

            uint16_t u16(){
                need(2);
                const uint16_t v = static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
                offset += 2;
                return v;
            }

            uint32_t u32(){
                need(4);
                const uint32_t v = static_cast<uint32_t>(data[offset])
                    | (static_cast<uint32_t>(data[offset + 1]) << 8)
                    | (static_cast<uint32_t>(data[offset + 2]) << 16)
                    | (static_cast<uint32_t>(data[offset + 3]) << 24);
                offset += 4;
                return v;
            }

            float f32(){
                const uint32_t bits = u32();
                float v;
                std::memcpy(&v, &bits, sizeof(v));
                return v;
            }

            mat4 matrix(){
                mat4 m;
                for(int c = 0; c < 4; c++){
                    for(int r = 0; r < 4; r++){
                        m[c][r] = f32();
                    }
                }
                return m;
            }

            void bytes(void* out, size_t count){
                need(count);
                std::memcpy(out, data + offset, count);
                offset += count;
            }

            private:
            const uint8_t* data;
            size_t size;
            size_t offset = 0;
        };

        struct BoneRecord{
            uint16_t parent;
            mat4 transform;
            mat4 inverseBind;
        };
    }

    LoadResult load(const uint8_t* data, size_t size){
        if(!data){
            throw std::runtime_error("package: null data");
        }
        Reader in(data, size);

        char magic[4];
        in.bytes(magic, 4);
        if(std::memcmp(magic, "MSHP", 4) != 0){
            throw std::runtime_error("package: bad magic");
        }
        const uint16_t version = in.u16();
        if(version != VERSION){
            throw std::runtime_error("package: unsupported version " + std::to_string(version));
        }

        const uint16_t textureCount = in.u16();
        const uint16_t boneCount = in.u16();
        const uint16_t meshCount = in.u16();

        LoadResult result{};
        result.firstTexture = resources::textures.size();
        result.firstBone = resources::bones.size();
        result.firstMesh = resources::meshes.size();
        result.textureCount = textureCount;
        result.boneCount = boneCount;
        result.meshCount = meshCount;

        // Indices are stored as u16 with 0xFFFF reserved, so the pools can't grow past that.
        if(result.firstTexture + textureCount >= NO_TEXTURE){
            throw std::runtime_error("package: texture pool would overflow");
        }
        if(result.firstBone + boneCount >= NO_BONE){
            throw std::runtime_error("package: bone pool would overflow");
        }

        // Parse everything before touching the pools so a malformed package leaves them unchanged.
        std::vector<Texture> textures(textureCount);
        for(Texture& texture : textures){
            texture.width = in.u16();
            texture.height = in.u16();
            const size_t pixelCount = static_cast<size_t>(texture.width) * texture.height;
            in.need(pixelCount * 2);
            texture.data.resize(pixelCount);
            for(uint16_t& pixel : texture.data){
                pixel = in.u16();
            }
        }

        std::vector<BoneRecord> boneRecords(boneCount);
        for(uint16_t i = 0; i < boneCount; i++){
            BoneRecord& bone = boneRecords[i];
            bone.parent = in.u16();
            if(bone.parent != NO_BONE && bone.parent >= i){
                throw std::runtime_error("package: bone " + std::to_string(i) + " has invalid parent");
            }
            bone.transform = in.matrix();
            bone.inverseBind = in.matrix();
        }

        std::vector<Mesh> meshes(meshCount);
        for(Mesh& mesh : meshes){
            const uint16_t texture = in.u16();
            if(texture != NO_TEXTURE && texture >= textureCount){
                throw std::runtime_error("package: mesh references missing texture");
            }
            mesh.texture = texture == NO_TEXTURE
                ? NO_TEXTURE
                : static_cast<uint16_t>(result.firstTexture + texture);

            const uint32_t vertexCount = in.u32();
            const uint32_t indexCount = in.u32();
            constexpr size_t vertexSize = 3 * 4 + 3 * 4 + 4 * 2 + 4 * 4 + 2 * 4;
            in.need(static_cast<size_t>(vertexCount) * vertexSize);

            mesh.vertices.resize(vertexCount);
            mesh.uvs.resize(vertexCount);
            for(uint32_t v = 0; v < vertexCount; v++){
                Vertex& vertex = mesh.vertices[v];
                for(int k = 0; k < 3; k++) vertex.position[k] = in.f32();
                for(int k = 0; k < 3; k++) vertex.normal[k] = in.f32();
                for(int k = 0; k < 4; k++){
                    const uint16_t bone = in.u16();
                    if(bone != NO_BONE && bone >= boneCount){
                        throw std::runtime_error("package: vertex references missing bone");
                    }
                    vertex.boneIndices[k] = bone == NO_BONE
                        ? NO_BONE
                        : static_cast<uint16_t>(result.firstBone + bone);
                }
                for(int k = 0; k < 4; k++) vertex.boneWeights[k] = in.f32();
                for(int k = 0; k < 2; k++) mesh.uvs[v][k] = in.f32();
            }

            in.need(static_cast<size_t>(indexCount) * 4);
            mesh.indices.resize(indexCount);
            for(uint32_t& index : mesh.indices){
                index = in.u32();
                if(index >= vertexCount){
                    throw std::runtime_error("package: index out of range");
                }
            }
        }

        for(Texture& texture : textures){
            resources::textures.push_back(std::move(texture));
        }
        for(const BoneRecord& record : boneRecords){
            const uint16_t parent = record.parent == NO_BONE
                ? NO_BONE
                : static_cast<uint16_t>(result.firstBone + record.parent);
            const uint16_t index = bones::create(parent, record.transform);
            resources::bones[index].inverseBind = record.inverseBind;
        }
        for(Mesh& mesh : meshes){
            resources::meshes.push_back(std::move(mesh));
        }

        if(boneCount > 0){
            bones::updateGlobals();
        }

        return result;
    }
};
