#include "bones.hpp"
#include "resources.hpp"

#include <stdexcept>

namespace bones{
    uint16_t create(uint16_t parent, const mat4& localTransform){
        auto& pool = resources::bones;
        if(pool.size() >= NO_BONE){
            throw std::length_error("bone pool full");
        }
        if(parent != NO_BONE && parent >= pool.size()){
            throw std::out_of_range("invalid parent bone");
        }

        const uint16_t index = static_cast<uint16_t>(pool.size());
        Bone bone;
        bone.transform = localTransform;
        bone.parent = parent;

        if(parent == NO_BONE){
            resources::rootBones.push_back(index);
        }else{
            bone.nextSibling = pool[parent].firstChild;
            pool[parent].firstChild = index;
        }

        pool.push_back(bone);
        return index;
    }

    void updateGlobals(){
        auto& pool = resources::bones;
        std::vector<uint16_t> stack;

        for(uint16_t root : resources::rootBones){
            pool[root].globalTransform = pool[root].transform;
            stack.push_back(root);

            while(!stack.empty()){
                const uint16_t current = stack.back();
                stack.pop_back();

                for(uint16_t child = pool[current].firstChild; child != NO_BONE; child = pool[child].nextSibling){
                    pool[child].globalTransform = pool[current].globalTransform * pool[child].transform;
                    stack.push_back(child);
                }
            }
        }
    }

    void setBindPose(){
        for(Bone& bone : resources::bones){
            bone.inverseBind = glm::inverse(bone.globalTransform);
        }
    }

    std::vector<mat4> skinMatrices(){
        std::vector<mat4> matrices;
        matrices.reserve(resources::bones.size());
        for(const Bone& bone : resources::bones){
            matrices.push_back(bone.globalTransform * bone.inverseBind);
        }
        return matrices;
    }
};
