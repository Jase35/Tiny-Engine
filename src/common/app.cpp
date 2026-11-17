#include "app.hpp"
#include "assets/models.hpp"

#include <algorithm>
#include <cmath>

App::App(unsigned cores, WorkerPool::ConfigureThread configureThread)
    : workers(cores, configureThread){
    model = package::load(YUU, YUU_SIZE);

    Texture texture = {
        4, 4,
        {
            0xF800, 0x001F, 0xF800, 0x001F,
            0x001F, 0xF800, 0x001F, 0xF800,
            0xF800, 0x001F, 0xF800, 0x001F,
            0x001F, 0xF800, 0x001F, 0xF800
        }
    };

    resources::textures.push_back(texture);

    Texture texture2 = {
        1, 1,
        {
            0x07E0
        }
    };

    resources::textures.push_back(texture2);

    // Demo skeleton: a root bone with one child pivoting at the mesh center (0.5, 0.5).
    const uint16_t rootBone = bones::create(NO_BONE, mat4(1.0f));
    armBone = bones::create(rootBone, glm::translate(mat4(1.0f), vec3(0.5f, 0.5f, 0.0f)));
    //armBone = bones::create(rootBone, glm::translate(mat4(1.0f), vec3(0.5f, 0.5f, 1.0f)));
    bones::updateGlobals();
    bones::setBindPose();

    // Size the scratch for every rendered mesh's vertices now, so rendering never reallocates it.
    size_t totalVertices = 0;
    for(size_t i = 0; i < model.meshCount; i++){
        totalVertices += resources::meshes[model.firstMesh + i].vertices.size();
    }
    scratch.reserve(totalVertices);
}

void App::update(float deltaTime){
    time += deltaTime;

    resources::bones[armBone].transform =
        glm::translate(mat4(1.0f), vec3(0.5f, 0.5f, 0.0f))
        * glm::rotate(mat4(1.0f), std::sin(time) * 0.75f, vec3(0.0f, 0.0f, 1.0f));
    bones::updateGlobals();
}

const std::vector<uint16_t>& App::render(uint16_t defaultColor, int width, int height){
    const size_t pixelCount = static_cast<size_t>(width) * height;
    // Only (re)allocates when the resolution grows; otherwise the existing storage is reused.
    colorBuffer.resize(pixelCount);
    zBuffer.resize(pixelCount);

    Camera camera{};
    GlobalData globalData;
    globalData.viewMatrix = mat4(1.0f);
    globalData.projectionMatrix = camera.projection(static_cast<float>(width) / static_cast<float>(height));
    globalData.boneMatrices = bones::skinMatrices();

    // If re-enabled, move this below the workers.run() phases: phase 1 clears the buffers.
    /*Mesh mesh(0);
    // Bind the second triangle to the animated arm bone.
    for(size_t i = 3; i < 6 && i < mesh.vertices.size(); i++){
        mesh.vertices[i].boneIndices = u16vec4(armBone, NO_BONE, NO_BONE, NO_BONE);
        mesh.vertices[i].boneWeights = vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    ModelData modelData;
    // Center the mesh (authored around 0.5, 0.5) and push it in front of the camera (-Z).
    modelData.modelMatrix = glm::translate(mat4(1.0f), vec3(-0.5f, -0.5f, -0.65f));
    modelData.cullBackFaces = false; // The demo triangles are wound clockwise.

    renderer::drawMesh(
        mesh,
        globalData,
        modelData,
        scratch,
        colorBuffer,
        zBuffer,
        width,
        height
    );

    Mesh mesh2(1);
    ModelData modelData2;
    // Center the mesh (authored around 0.5, 0.5) and push it in front of the camera (-Z).
    modelData2.modelMatrix = glm::translate(mat4(1.0f), vec3(-0.25f, -0.25f, -2.0f));
    modelData2.cullBackFaces = false;

    renderer::drawMesh(
        mesh2,
        globalData,
        modelData2,
        scratch,
        colorBuffer,
        zBuffer,
        width,
        height
    );*/

    ModelData modelData;
    // Model is ~2 units tall with feet at y = 0: center it vertically, place it in front of the camera, and spin it.
    //modelData.modelMatrix = glm::translate(mat4(1.0f), vec3(0.0f, -1.8f, -0.75f)) * glm::rotate(mat4(1.0f), time, vec3(0.0f, 1.0f, 0.0f));
    modelData.modelMatrix = glm::translate(mat4(1.0f), vec3(0.0f, 0.25f, -2.5f)) * glm::rotate(mat4(1.0f), time, vec3(0.0f, 1.0f, 0.0f));

    const unsigned workerCount = workers.size();
    renderer::ScreenVertex* screenVertices = scratch.vertices.data();

    // Phase 1: each worker clears its share of the buffers and shades its share of every
    // mesh's vertices. run() returns only when all workers are done, so phase 2 sees it all.
    workers.run([&](unsigned worker){
        const size_t clearBegin = pixelCount * worker / workerCount;
        const size_t clearEnd = pixelCount * (worker + 1) / workerCount;
        std::fill(colorBuffer.begin() + clearBegin, colorBuffer.begin() + clearEnd, defaultColor);
        std::fill(zBuffer.begin() + clearBegin, zBuffer.begin() + clearEnd, renderer::DEPTH_CLEAR);

        size_t offset = 0;
        for(size_t i = 0; i < model.meshCount; i++){
            const Mesh& mesh = resources::meshes[model.firstMesh + i];
            const size_t count = mesh.vertices.size();
            renderer::shadeVertices(
                mesh,
                globalData,
                modelData,
                width,
                height,
                screenVertices + offset,
                count * worker / workerCount,
                count * (worker + 1) / workerCount
            );
            offset += count;
        }
    });

    // Phase 2: every worker draws all meshes, but only into its own interleaved rows,
    // so no two workers ever write the same pixel.
    workers.run([&](unsigned worker){
        const renderer::RowSet rows{static_cast<int>(worker), static_cast<int>(workerCount)};
        size_t offset = 0;
        for(size_t i = 0; i < model.meshCount; i++){
            const Mesh& mesh = resources::meshes[model.firstMesh + i];
            renderer::rasterizeMesh(mesh, modelData, screenVertices + offset, colorBuffer, zBuffer, width, height, rows);
            offset += mesh.vertices.size();
        }
    });

    return colorBuffer;
}

std::string prompt(std::string inputPrompt){
    // TODO
    return "";
}
