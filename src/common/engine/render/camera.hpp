#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

using glm::mat4;

class Camera{
    public:
    // aspect = viewport width / height.
    mat4 projection(float aspect);

    private:
    float near = 0.1f;
    float far = 10000.0f;
    float fovDeg = 75.0f;
};