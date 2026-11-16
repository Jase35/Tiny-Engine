#include "camera.hpp"

mat4 Camera::projection(float aspect){
    float s = 1.0f / glm::tan((fovDeg / 2.0f) * (glm::pi<float>() / 180.0f));

    return mat4{
        {s / aspect, 0.0f, 0.0f, 0.0f},
        {0.0f, s, 0.0f, 0.0f},
        {0.0f, 0.0f, -far / (far - near), -1.0f},
        {0.0f, 0.0f, -(far * near) / (far - near), 0.0f}
    };
}