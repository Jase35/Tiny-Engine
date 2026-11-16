#pragma once

#include <vector>
#include <cstdint>
#include <limits>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

#include "engine/resources.hpp"
#include "engine/bones.hpp"

#include "engine/render/renderer.hpp"
#include "engine/render/shader.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/package.hpp"
#include "util/worker_pool.hpp"

using glm::vec2;
using glm::vec3;
using glm::uvec2;

class App{
    public:
    // renderCores: how many threads (including the one calling render()) share the rendering
    // work. configureThread is passed to WorkerPool; on ESP-IDF use it to set each extra
    // worker's stack size and core affinity via esp_pthread_set_cfg().
    explicit App(unsigned cores = 1, WorkerPool::ConfigureThread configureThread = nullptr);
    void update(float deltaTime);
    // Renders into an internal RGB565 buffer (width * height) that is reused across frames.
    const std::vector<uint16_t>& render(uint16_t defaultColor, int width, int height);
    std::string prompt(std::string inputPrompt);

    private:
    WorkerPool workers;
    std::vector<uint16_t> colorBuffer;
    std::vector<uint16_t> zBuffer;
    renderer::Scratch scratch; // Shaded vertices of every rendered mesh, back to back.
    float time = 0.0f;
    uint16_t armBone = NO_BONE;
    package::LoadResult miku{};
};