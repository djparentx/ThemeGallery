#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <algorithm>
#include <string>
#include <vector>
#include "Config.h"
#include "EsSettings.h"
#include "Input.h"
#include "Layout.h"
#include "Log.h"
#include "Paths.h"
#include "Util.h"

namespace {
const char* ES_SETTINGS_PATH = "/home/ark/.emulationstation/es_settings.cfg";
const char* ES_INPUT_PATH    = "/etc/emulationstation/es_input.cfg";

const SDL_Color COL_TEXT{235, 240, 250, 255};
const SDL_Color COL_ACCENT{40, 160, 255, 255};
const SDL_Color COL_DIM{140, 155, 180, 255};
const SDL_Color COL_ERROR{255, 90, 90, 255};

void drawText(SDL_Renderer* r, TTF_Font* f, const std::string& s, int x, int y, SDL_Color c) {
    static bool loggedFail = false;
    if (s.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) {
        if (!loggedFail) {
            Log::error("text", std::string("render: ") + TTF_GetError());
            loggedFail = true;
        }
        return;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst{x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    if (!t) {
        if (!loggedFail) {
            Log::error("text", std::string("texture: ") + SDL_GetError());
            loggedFail = true;
        }
        return;
    }
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

int textWidth(TTF_Font* f, const std::string& s) {
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

// Largest rect with the texture's aspect that fits inside box, centered
SDL_Rect fitRect(int tw, int th, const SDL_Rect& box) {
    float s = std::min((float)box.w / tw, (float)box.h / th);
    int w = (int)(tw * s), h = (int)(th * s);
    return {box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
}

void outline(SDL_Renderer* r, const SDL_Rect& rc) {
    SDL_SetRenderDrawColor(r, 40, 60, 90, 255);
    SDL_RenderDrawRect(r, &rc);
}

std::string firstLine(const std::string& s) {
    size_t p = s.find('\n');
    return p == std::string::npos ? s : s.substr(0, p) + " ...";
}
}

int main(int, char**) {
    Log::header();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        Log::error("sdl", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        Log::error("ttf", TTF_GetError());
        return 1;
    }
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) Log::warn("image", IMG_GetError());
    curl_global_init(CURL_GLOBAL_DEFAULT);

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

    int w = 0, h = 0;
    SDL_GetRendererOutputSize(ren, &w, &h);
    Layout lay;
    lay.compute(w, h);
    Log::info("layout", std::to_string(w) + "x" + std::to_string(h) + "  scale " + std::to_string(lay.scale));

    const std::string appDir = Paths::appDir();
    const std::string semi = appDir + "fonts/Exo2-SemiBold.ttf";
    const std::string reg  = appDir + "fonts/Exo2-Regular.ttf";
    TTF_Font* fontHeader = TTF_OpenFont(semi.c_str(), lay.fontHeader);
    TTF_Font* fontTitle  = TTF_OpenFont(semi.c_str(), lay.fontTitle);
    TTF_Font* fontBody   = TTF_OpenFont(reg.c_str(), lay.fontBody);
    TTF_Font* fontSmall  = TTF_OpenFont(reg.c_str(), lay.fontSmall);
    if (!fontHeader || !fontTitle || !fontBody || !fontSmall) {
        Log::error("ttf", TTF_GetError());
        return 1;
    }

    EsSettings es;
    es.load(ES_SETTINGS_PATH);
    EsInputFile esInput;
    esInput.load(ES_INPUT_PATH);
    Input input;
    if (!input.open(esInput, es.invertButtons)) {
        SDL_SetRenderDrawColor(ren, 11, 20, 36, 255);
        SDL_RenderClear(ren);
        drawText(ren, fontBody, "Controller setup failed - see ThemeGallery.log", lay.margin, lay.margin, COL_ERROR);
        SDL_RenderPresent(ren);
        SDL_Delay(5000);
        input.close();
        TTF_CloseFont(fontHeader);
        TTF_CloseFont(fontTitle);
        TTF_CloseFont(fontBody);
        TTF_CloseFont(fontSmall);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        curl_global_cleanup();
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    const std::string themesDir = Paths::selectThemesDir();
    const uint64_t freeSpace = Paths::freeBytes(themesDir);
    Log::info("paths", "free " + Util::formatBytes(freeSpace));

    Gallery gallery;
    gallery.load(appDir + "gallery.ini", appDir + "Images/");
    const std::string statePath = appDir + "themegallery.cfg";
    UserState state;
    state.load(statePath);

    const size_t n = gallery.themes.size();
    size_t cur = 0;
    SDL_Texture* preview = nullptr;
    int pw = 0, ph = 0;
    bool installed = false;

    auto refresh = [&]() {
        if (preview) {
            SDL_DestroyTexture(preview);
            preview = nullptr;
        }
        if (n == 0) return;
        const Theme& t = gallery.themes[cur];
        installed = Paths::isDir(themesDir + "/" + t.folder);
        if (t.images.empty()) return;
        preview = IMG_LoadTexture(ren, t.images[0].c_str());
        if (!preview) {
            Log::warn("image", t.images[0] + ": " + IMG_GetError());
            return;
        }
        SDL_QueryTexture(preview, nullptr, nullptr, &pw, &ph);
    };
    refresh();

    std::vector<Action> actions;
    bool running = true;

    while (running) {
        actions.clear();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            input.handleEvent(e, actions);
        }
        input.update(actions);
        for (Action a : actions) {
            if (a == Action::Exit) running = false;
            if (n == 0) continue;
            if (a == Action::Down) {
                cur = (cur + 1) % n;
                refresh();
            } else if (a == Action::Up) {
                cur = (cur + n - 1) % n;
                refresh();
            } else if (a == Action::Y) {
                state.toggleFavorite(gallery.themes[cur].folder);
                state.save(statePath);
            }
        }

        SDL_SetRenderDrawColor(ren, 11, 20, 36, 255);
        SDL_RenderClear(ren);
        outline(ren, lay.header);
        outline(ren, lay.preview);
        outline(ren, lay.title);
        outline(ren, lay.info);
        outline(ren, lay.footer);

        drawText(ren, fontHeader, "ThemeGallery", lay.margin,
                 lay.header.y + (lay.header.h - TTF_FontHeight(fontHeader)) / 2, COL_ACCENT);

        if (n == 0) {
            drawText(ren, fontBody, "No themes in gallery.ini", lay.info.x + lay.px(8), lay.info.y + lay.px(6), COL_ERROR);
        } else {
            const Theme& t = gallery.themes[cur];

            std::string counter = std::to_string(cur + 1) + "/" + std::to_string(n);
            drawText(ren, fontSmall, counter, w - lay.margin - textWidth(fontSmall, counter),
                     lay.header.y + (lay.header.h - TTF_FontHeight(fontSmall)) / 2, COL_DIM);

            if (preview) {
                SDL_Rect dst = fitRect(pw, ph, lay.preview);
                SDL_RenderCopy(ren, preview, nullptr, &dst);
            } else {
                const std::string msg = "No preview";
                drawText(ren, fontBody, msg, lay.preview.x + (lay.preview.w - textWidth(fontBody, msg)) / 2,
                         lay.preview.y + (lay.preview.h - TTF_FontHeight(fontBody)) / 2, COL_DIM);
            }

            drawText(ren, fontTitle, t.name, lay.title.x + (lay.title.w - textWidth(fontTitle, t.name)) / 2,
                     lay.title.y + (lay.title.h - TTF_FontHeight(fontTitle)) / 2, COL_TEXT);

            std::vector<std::string> lines = {
                "Author: " + t.author,
                "Folder: " + t.folder,
                "Size: " + (t.size ? Util::formatBytes(t.size) : std::string("unknown")),
                "Images: " + std::to_string(t.images.size()),
                std::string("Installed: ") + (installed ? "yes" : "no") +
                    "   Favorite: " + (state.isFavorite(t.folder) ? "yes" : "no"),
                themesDir + "   free " + Util::formatBytes(freeSpace),
                firstLine(t.description)};
            int ly = lay.info.y + lay.px(6);
            int lh = TTF_FontLineSkip(fontBody);
            for (const auto& l : lines) {
                if (ly + lh > lay.info.y + lay.info.h) break;
                drawText(ren, fontBody, l, lay.info.x + lay.px(8), ly, COL_TEXT);
                ly += lh;
            }
        }

        drawText(ren, fontSmall, "Up/Down theme    Y favorite    Select+Start exit", lay.margin,
                 lay.footer.y + (lay.footer.h - TTF_FontHeight(fontSmall)) / 2, COL_DIM);

        SDL_RenderPresent(ren);
    }

    if (preview) SDL_DestroyTexture(preview);
    input.close();
    TTF_CloseFont(fontHeader);
    TTF_CloseFont(fontTitle);
    TTF_CloseFont(fontBody);
    TTF_CloseFont(fontSmall);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}