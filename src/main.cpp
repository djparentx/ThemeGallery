#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <algorithm>
#include <string>
#include <vector>
#include "EsSettings.h"
#include "Input.h"
#include "Log.h"

namespace {
const char* ES_SETTINGS_PATH = "/home/ark/.emulationstation/es_settings.cfg";
const char* ES_INPUT_PATH    = "/home/ark/.emulationstation/es_input.cfg";

void drawText(SDL_Renderer* r, TTF_Font* f, const std::string& s, int x, int y, SDL_Color c) {
    if (s.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    if (t) {
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
}
}

int main(int, char**) {
    Log::header();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        Log::error("sdl", SDL_GetError());
        return 1;
    }
    SDL_version sv;
    SDL_GetVersion(&sv);
    Log::info("sdl", "SDL " + std::to_string(sv.major) + "." + std::to_string(sv.minor) + "." + std::to_string(sv.patch));

    if (TTF_Init() != 0) {
        Log::error("ttf", TTF_GetError());
        return 1;
    }
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) Log::warn("image", IMG_GetError());

    curl_global_init(CURL_GLOBAL_DEFAULT);
    Log::info("curl", curl_version());

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0) {
        Log::error("sdl", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("ThemeGallery", 0, 0, dm.w, dm.h, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (!win) {
        Log::error("sdl", SDL_GetError());
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        Log::error("sdl", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);

    SDL_RendererInfo ri;
    SDL_GetRendererInfo(ren, &ri);
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(ren, &w, &h);
    Log::info("video", std::string("renderer ") + ri.name + " " + std::to_string(w) + "x" + std::to_string(h));

    char* base = SDL_GetBasePath();
    std::string appDir = base ? base : "./";
    SDL_free(base);

    std::string fontPath = appDir + "fonts/Exo2-Regular.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), std::max(12, h / 18));
    if (!font) {
        Log::error("ttf", fontPath + ": " + TTF_GetError());
        return 1;
    }

    EsSettings es;
    es.load(ES_SETTINGS_PATH);
    Log::info("settings", std::string("InvertButtons=") + (es.invertButtons ? "true" : "false") + "  ThemeSet=" + es.themeSet);

    EsInputFile esInput;
    esInput.load(ES_INPUT_PATH);
    Input input;
    input.open(esInput, es.invertButtons);

    std::string lastAction = "-";
    std::vector<Action> actions;
    bool running = true;
    const SDL_Color white{235, 240, 250, 255};
    const SDL_Color accent{40, 160, 255, 255};

    while (running) {
        actions.clear();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            input.handleEvent(e, actions);
        }
        input.update(actions);
        for (Action a : actions) {
            lastAction = actionName(a);
            Log::info("input", std::string("action ") + lastAction);
            if (a == Action::Exit) running = false;
        }

        SDL_SetRenderDrawColor(ren, 11, 20, 36, 255);
        SDL_RenderClear(ren);

        int x = w / 20, y = h / 20, lh = TTF_FontLineSkip(font);
        drawText(ren, font, "ThemeGallery - stage 1 test", x, y, accent); y += lh * 2;
        drawText(ren, font, "Resolution: " + std::to_string(w) + "x" + std::to_string(h), x, y, white); y += lh;
        drawText(ren, font, std::string("Renderer: ") + ri.name, x, y, white); y += lh;
        drawText(ren, font, "Pad: " + input.deviceName() + (input.mapped() ? "" : " (incomplete map)"), x, y, white); y += lh;
        drawText(ren, font, std::string("InvertButtons: ") + (es.invertButtons ? "true" : "false"), x, y, white); y += lh;
        drawText(ren, font, "ThemeSet: " + es.themeSet, x, y, white); y += lh;
        drawText(ren, font, "Last action: " + lastAction, x, y, accent); y += lh * 2;
        drawText(ren, font, "Select+Start to exit", x, y, white);

        SDL_RenderPresent(ren);
    }

    input.close();
    TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}