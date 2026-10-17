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
const char* ES_INPUT_PATH    = "/etc/emulationstation/es_input.cfg";

void drawText(SDL_Renderer* r, TTF_Font* f, const std::string& s, int x, int y, SDL_Color c, bool noBlend = false) {
    static bool loggedSurfFail = false, loggedTexFail = false, loggedCopyFail = false, loggedFormat = false;
    if (s.empty()) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) {
        if (!loggedSurfFail) {
            Log::error("diag", std::string("TTF_RenderUTF8_Blended: ") + TTF_GetError());
            loggedSurfFail = true;
        }
        return;
    }

    static bool loggedAlpha = false;
    if (!loggedAlpha && surf->format->BytesPerPixel == 4) {
        SDL_LockSurface(surf);
        Uint32 amask = surf->format->Amask;
        Uint8 ashift = surf->format->Ashift;
        Uint32 maxA = 0;
        int nonzero = 0;
        for (int yy = 0; yy < surf->h; yy++) {
            const Uint32* row = (const Uint32*)((const Uint8*)surf->pixels + yy * surf->pitch);
            for (int xx = 0; xx < surf->w; xx++) {
                Uint32 a = (row[xx] & amask) >> ashift;
                if (a > maxA) maxA = a;
                if (a) nonzero++;
            }
        }
        SDL_UnlockSurface(surf);
        Log::info("diag", "surface alpha max=" + std::to_string(maxA) + "  nonzero=" + std::to_string(nonzero) +
                          "/" + std::to_string(surf->w * surf->h) + "  Ashift=" + std::to_string((int)ashift));
        loggedAlpha = true;
    }

    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    if (!t) {
        if (!loggedTexFail) {
            Log::error("diag", std::string("SDL_CreateTextureFromSurface: ") + SDL_GetError());
            loggedTexFail = true;
        }
        SDL_FreeSurface(surf);
        return;
    }

    if (!loggedFormat) {
        Uint32 tf = 0;
        SDL_BlendMode bm = SDL_BLENDMODE_NONE;
        SDL_QueryTexture(t, &tf, nullptr, nullptr, nullptr);
        SDL_GetTextureBlendMode(t, &bm);
        Log::info("diag", std::string("surface ") + SDL_GetPixelFormatName(surf->format->format) + " " +
                          std::to_string(surf->w) + "x" + std::to_string(surf->h) +
                          "  texture " + SDL_GetPixelFormatName(tf) + "  blend " + std::to_string((int)bm));
        loggedFormat = true;
    }

    if (noBlend) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);

    if (SDL_RenderCopy(r, t, nullptr, &dst) != 0 && !loggedCopyFail) {
        Log::error("diag", std::string("SDL_RenderCopy: ") + SDL_GetError());
        loggedCopyFail = true;
    }
    SDL_DestroyTexture(t);
}

// DIAG: uploads and draws a surface, then frees it (takes ownership)
void diagDraw(SDL_Renderer* r, SDL_Surface* surf, int x, int y) {
    static bool loggedFail = false;
    if (!surf) {
        if (!loggedFail) {
            Log::error("diag", std::string("diag surface null: ") + SDL_GetError() + " / " + TTF_GetError());
            loggedFail = true;
        }
        return;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    if (!t) {
        if (!loggedFail) {
            Log::error("diag", std::string("diag texture null: ") + SDL_GetError());
            loggedFail = true;
        }
        return;
    }
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
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

    for (Uint32 i = 0; i < ri.num_texture_formats; i++)
        Log::info("video", std::string("texture format ") + SDL_GetPixelFormatName(ri.texture_formats[i]));
    Log::info("video", "max texture " + std::to_string(ri.max_texture_width) + "x" + std::to_string(ri.max_texture_height));

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
    if (!input.open(esInput, es.invertButtons)) {
        SDL_SetRenderDrawColor(ren, 11, 20, 36, 255);
        SDL_RenderClear(ren);
        int lh = TTF_FontLineSkip(font);
        drawText(ren, font, "Controller setup failed", w / 20, h / 20, SDL_Color{255, 90, 90, 255});
        drawText(ren, font, "See ThemeGallery.log", w / 20, h / 20 + lh, SDL_Color{235, 240, 250, 255});
        SDL_RenderPresent(ren);
        SDL_Delay(5000);
        input.close();
        TTF_CloseFont(font);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        curl_global_cleanup();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

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

 // DIAG: solid rect = primitive path, NO-BLEND text = texture path without alpha
        SDL_Rect diagRect{w - w / 8, h / 20, w / 10, h / 10};
        SDL_SetRenderDrawColor(ren, accent.r, accent.g, accent.b, 255);
        SDL_RenderFillRect(ren, &diagRect);
        drawText(ren, font, "NO-BLEND TEST", w / 20, h - h / 6, white, true);

        // DIAG: Shaded = 8-bit surface, no alpha; ABGR = Blended converted to GLES-native order before upload
        diagDraw(ren, TTF_RenderUTF8_Shaded(font, "SHADED TEST", white, SDL_Color{11, 20, 36, 255}), w / 20, h - h / 3);
        if (SDL_Surface* s = TTF_RenderUTF8_Blended(font, "ABGR TEST", white)) {
            diagDraw(ren, SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0), w / 2, h - h / 3);
            SDL_FreeSurface(s);
        }

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