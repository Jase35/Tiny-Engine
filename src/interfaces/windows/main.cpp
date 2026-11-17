#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cstdint>
#include <thread>
#include <vector>

#include "app.hpp"

int main(int argc, char** argv){
    //const int W = 1920, H = 1080;
    const int W = 640, H = 480;
    //const int W = 320, H = 240;

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window*   win = SDL_CreateWindow("Pixels", SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED, W, H, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture*  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                                          SDL_TEXTUREACCESS_STREAMING, W, H);

    // Use every hardware thread for rendering (hardware_concurrency() may return 0; App treats that as 1).
    //App app{std::thread::hardware_concurrency()};
    App app{1};

    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    uint64_t lastTime = SDL_GetPerformanceCounter();
    double fpsTimer = 0.0;
    int fpsFrames = 0;

    bool running = true;
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT){
                running = false;
            }
        }

        uint64_t now = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>((now - lastTime) / freq);
        lastTime = now;

        // Refresh the FPS in the title twice per second to keep it readable
        fpsTimer += deltaTime;
        fpsFrames++;
        if(fpsTimer >= 0.5){
            char title[64];
            SDL_snprintf(title, sizeof(title), "Tiny Engine [%.1f FPS]", fpsFrames / fpsTimer);
            SDL_SetWindowTitle(win, title);
            fpsTimer = 0.0;
            fpsFrames = 0;
        }

        app.update(deltaTime);

        const std::vector<uint16_t>& pixels = app.render(0x0000, W, H);

        SDL_UpdateTexture(tex, nullptr, pixels.data(), W * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}