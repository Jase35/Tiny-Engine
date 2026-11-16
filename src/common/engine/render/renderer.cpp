#include "renderer.hpp"

#include <algorithm>

namespace renderer{
    namespace{
        constexpr float DEPTH_MAX = static_cast<float>(DEPTH_CLEAR - 1);
        constexpr float INV_PERSPECTIVE_SPAN = 1.0f / static_cast<float>(PERSPECTIVE_SPAN);

        // The edge function is affine in p, so after evaluating it once it can be advanced
        // by a constant per pixel step. Pixel coordinates are integers, so this is exact.
        struct Edge{
            int32_t value;
            int32_t stepX;
            int32_t stepY;
        };

        Edge makeEdge(glm::ivec2 a, glm::ivec2 b, glm::ivec2 p){
            return {
                (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x),
                a.y - b.y,
                b.x - a.x
            };
        }

        // Narrows [lo, hi] to the k >= 0 where an edge stepped k pixels right, w + k * step,
        // stays non-negative. Numerators are non-negative here, so '/' is an exact floor.
        void clipEdge(int32_t w, int32_t step, int32_t& lo, int32_t& hi){
            if(step > 0){
                if(w < 0){
                    lo = std::max(lo, (-w + step - 1) / step);
                }
            }else if(w < 0){
                hi = -1; // Already outside and not moving back in.
            }else if(step < 0){
                hi = std::min(hi, w / -step);
            }
        }

        const Texture* validTexture(const Mesh& mesh){
            if(mesh.texture >= resources::textures.size()){
                return nullptr;
            }
            const Texture& texture = resources::textures[mesh.texture];
            if(texture.width == 0 || texture.height == 0
                || texture.data.size() < static_cast<size_t>(texture.width) * texture.height){
                return nullptr;
            }
            return &texture;
        }
    }

    void shadeVertices(
        const Mesh& mesh,
        const GlobalData& globalData,
        const ModelData& modelData,
        int width,
        int height,
        ScreenVertex* out,
        size_t begin,
        size_t end
    ){
        const mat4 modelViewProjection = globalData.projectionMatrix * globalData.viewMatrix * modelData.modelMatrix;
        const vec2 size(width, height);

        for(size_t i = begin; i < end; i++){
            VertexData data;
            const Vertex vertex = shadeVertex(
                mesh.vertices[i],
                i < mesh.uvs.size() ? mesh.uvs[i] : vec2(0.0f),
                modelViewProjection,
                globalData,
                data
            );

            // NDC is [-1, 1] with +Y up; pixels are [0, size] with +Y down.
            const vec2 uv(vertex.position.x * 0.5f + 0.5f, 0.5f - vertex.position.y * 0.5f);
            // Off-screen vertices keep their true position (the rasterizer clips to the screen);
            // the guard band only bounds coordinates so the integer edge math can't overflow.
            const vec2 pixel = glm::clamp(glm::round(uv * size), vec2(-GUARD_BAND), vec2(GUARD_BAND));
            out[i] = {glm::ivec2(pixel), vertex.position.z, data};
        }
    }

    void rasterizeMesh(
        const Mesh& mesh,
        const ModelData& modelData,
        const ScreenVertex* vertices,
        std::vector<uint16_t>& colorBuffer,
        std::vector<uint16_t>& zBuffer,
        int width,
        int height,
        RowSet rows
    ){
        const Texture* texture = validTexture(mesh);

        auto ownsRowBlock = [&rows](int block){
            return block % rows.stride == rows.offset;
        };
        // Whether any of rows [minY, maxY) belong to this RowSet.
        auto ownsAnyRow = [&](int minY, int maxY){
            const int first = minY / ROW_BLOCK;
            const int last = (maxY - 1) / ROW_BLOCK;
            if(last - first + 1 >= rows.stride){
                return true;
            }
            for(int block = first; block <= last; block++){
                if(ownsRowBlock(block)){
                    return true;
                }
            }
            return false;
        };

        for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3){
            const ScreenVertex& s0 = vertices[mesh.indices[i]];
            const ScreenVertex& s1 = vertices[mesh.indices[i + 1]];
            const ScreenVertex& s2 = vertices[mesh.indices[i + 2]];

            // Reject triangles with any vertex outside [near, far]. This includes points
            // behind the camera, whose w-divide flips them (they land at NDC z > 1).
            auto depthOutside = [](float z){ return z < 0.0f || z > 1.0f; };
            if(depthOutside(s0.z) || depthOutside(s1.z) || depthOutside(s2.z)){
                continue;
            }

            const glm::ivec2 p0 = s0.pixel;
            const glm::ivec2 p1 = s1.pixel;
            const glm::ivec2 p2 = s2.pixel;

            int32_t area = makeEdge(p0, p1, p2).value;
            if(area == 0){
                continue; // Degenerate triangle.
            }
            // Pixel space has +Y down, which flips the winding: front faces have negative area.
            if(modelData.cullBackFaces && area > 0){
                continue;
            }

            const glm::ivec2 minPix = glm::min(glm::min(p0, p1), p2);
            const glm::ivec2 maxPix = glm::max(glm::max(p0, p1), p2);
            // The part of the bounding box that's on screen; the edges are still set up from minPix.
            const glm::ivec2 clipMin = glm::max(minPix, glm::ivec2(0));
            const glm::ivec2 clipMax = glm::min(maxPix, glm::ivec2(width, height));
            // Skip before the per-triangle setup if no row of it is ours (or it covers no pixels).
            if(clipMin.x >= clipMax.x || clipMin.y >= clipMax.y || !ownsAnyRow(clipMin.y, clipMax.y)){
                continue;
            }

            // Each edge function is the (signed) area opposite its vertex.
            Edge e0 = makeEdge(p1, p2, minPix);
            Edge e1 = makeEdge(p2, p0, minPix);
            Edge e2 = makeEdge(p0, p1, minPix);
            // Flip clockwise triangles so the interior is always non-negative.
            if(area < 0){
                for(Edge* e : {&e0, &e1, &e2}){
                    e->value = -e->value;
                    e->stepX = -e->stepX;
                    e->stepY = -e->stepY;
                }
                area = -area;
            }
            const float invArea = 1.0f / static_cast<float>(area);

            // Attributes that vary linearly in screen space: depth (pre-scaled to the z-buffer
            // range), and 1/w, u/w, v/w for perspective-correct texturing. The value at a pixel
            // is sum(edge_i * a_i) / area, so one pixel step right adds dAdx.
            auto attributes = [](const ScreenVertex& vertex){
                return vec4(vertex.z * DEPTH_MAX, vertex.data.invW, vertex.data.uv * vertex.data.invW);
            };
            const vec4 a0 = attributes(s0);
            const vec4 a1 = attributes(s1);
            const vec4 a2 = attributes(s2);
            const vec4 dAdx = (static_cast<float>(e0.stepX) * a0
                + static_cast<float>(e1.stepX) * a1
                + static_cast<float>(e2.stepX) * a2) * invArea;

            const int32_t firstColumn = clipMin.x - minPix.x;
            const int32_t lastColumn = clipMax.x - minPix.x - 1;

            // w0..w2 are the edge values at (minPix.x, y).
            auto drawRow = [&](int y, int32_t w0, int32_t w1, int32_t w2){
                // A triangle covers one contiguous run per row; solve each edge for its ends
                // instead of testing every pixel of the bounding box.
                int32_t lo = firstColumn;
                int32_t hi = lastColumn;
                clipEdge(w0, e0.stepX, lo, hi);
                clipEdge(w1, e1.stepX, lo, hi);
                clipEdge(w2, e2.stepX, lo, hi);
                if(lo > hi){
                    return;
                }

                // Recomputed from exact integer weights each row, so error can't build up across rows.
                const vec4 start = (static_cast<float>(w0 + lo * e0.stepX) * a0
                    + static_cast<float>(w1 + lo * e1.stepX) * a1
                    + static_cast<float>(w2 + lo * e2.stepX) * a2) * invArea;
                const int32_t last = hi - lo;
                const size_t rowStart = static_cast<size_t>(y) * width + minPix.x + lo;
                uint16_t* color = colorBuffer.data() + rowStart;
                uint16_t* depth = zBuffer.data() + rowStart;
                float z = start.x;

                // Must be called for k = 0..last in order, since it steps z.
                auto plot = [&](int32_t k, vec2 uv){
                    const uint16_t zq = static_cast<uint16_t>(glm::clamp(z, 0.0f, DEPTH_MAX) + 0.5f);
                    if(zq < depth[k]){
                        color[k] = shadeFragment(uv, texture);
                        depth[k] = zq;
                    }
                    z += dAdx.x;
                };

                if(!texture){
                    for(int32_t k = 0; k <= last; k++){
                        plot(k, vec2(0.0f));
                    }
                    return;
                }

                // Exact perspective-correct uv at pixel k of the run.
                auto uvAt = [&](int32_t k){
                    const vec4 a = start + static_cast<float>(k) * dAdx;
                    return vec2(a.z, a.w) * (1.0f / a.y);
                };

                vec2 uv = uvAt(0);
                int32_t k = 0;
                while(k < last){
                    const int32_t length = std::min(PERSPECTIVE_SPAN, last - k);
                    const vec2 uvEnd = uvAt(k + length);
                    const vec2 uvStep = (uvEnd - uv) * (length == PERSPECTIVE_SPAN
                        ? INV_PERSPECTIVE_SPAN
                        : 1.0f / static_cast<float>(length));
                    for(const int32_t end = k + length; k < end; k++){
                        plot(k, uv);
                        uv += uvStep;
                    }
                    uv = uvEnd;
                }
                plot(last, uv);
            };

            for(int block = clipMin.y / ROW_BLOCK; block * ROW_BLOCK < clipMax.y; block++){
                if(!ownsRowBlock(block)){
                    continue;
                }
                const int yBegin = std::max(clipMin.y, block * ROW_BLOCK);
                const int yEnd = std::min(clipMax.y, (block + 1) * ROW_BLOCK);
                const int32_t rowsDown = yBegin - minPix.y;
                int32_t w0 = e0.value + rowsDown * e0.stepY;
                int32_t w1 = e1.value + rowsDown * e1.stepY;
                int32_t w2 = e2.value + rowsDown * e2.stepY;
                for(int y = yBegin; y < yEnd; y++){
                    drawRow(y, w0, w1, w2);
                    w0 += e0.stepY;
                    w1 += e1.stepY;
                    w2 += e2.stepY;
                }
            }
        }
    }

    void drawMesh(
        const Mesh& mesh,
        const GlobalData& globalData,
        const ModelData& modelData,
        Scratch& scratch,
        std::vector<uint16_t>& colorBuffer,
        std::vector<uint16_t>& zBuffer,
        int width,
        int height
    ){
        const size_t vertexCount = mesh.vertices.size();
        scratch.reserve(vertexCount);
        shadeVertices(mesh, globalData, modelData, width, height, scratch.vertices.data(), 0, vertexCount);
        rasterizeMesh(mesh, modelData, scratch.vertices.data(), colorBuffer, zBuffer, width, height);
    }
};