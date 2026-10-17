#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <algorithm>
#include <cmath>
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

const Uint32 SLIDE_MS = 150;

struct Img {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

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

void outline(SDL_Renderer* r, const SDL_Rect& rc) {
    SDL_SetRenderDrawColor(r, 40, 60, 90, 255);
    SDL_RenderDrawRect(r, &rc);
}

std::string firstLine(const std::string& s) {
    size_t p = s.find('\n');
    return p == std::string::npos ? s : s.substr(0, p) + " ...";
}

float easeOut(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

int wrapIndex(int i, int count) {
    return ((i % count) + count) % count;
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
    bool installed = false;

    // Image carousel state: positions are focus-relative, offset slides toward 0
    std::vector<Img> imgs;
    int focus = 0;
    float slideFrom = 0.0f;
    Uint32 slideStart = 0;

    auto slideOffset = [&]() -> float {
        if (slideFrom == 0.0f) return 0.0f;
        Uint32 el = SDL_GetTicks() - slideStart;
        if (el >= SLIDE_MS) {
            slideFrom = 0.0f;
            return 0.0f;
        }
        return slideFrom * (1.0f - easeOut((float)el / SLIDE_MS));
    };

    auto freeImages = [&]() {
        for (auto& im : imgs)
            if (im.tex) SDL_DestroyTexture(im.tex);
        imgs.clear();
    };

    auto refresh = [&]() {
        freeImages();
        focus = 0;
        slideFrom = 0.0f;
        if (n == 0) return;
        const Theme& t = gallery.themes[cur];
        installed = Paths::isDir(themesDir + "/" + t.folder);
        for (const auto& p : t.images) {
            Img im;
            im.tex = IMG_LoadTexture(ren, p.c_str());
            if (!im.tex) {
                Log::warn("image", p + ": " + IMG_GetError());
                continue;
            }
            SDL_QueryTexture(im.tex, nullptr, nullptr, &im.w, &im.h);
            SDL_SetTextureBlendMode(im.tex, SDL_BLENDMODE_BLEND);  // JPGs have no alpha; needed for the fade
            imgs.push_back(im);
        }
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
            } else if ((a == Action::Right || a == Action::Left) && imgs.size() > 1) {
                const int count = (int)imgs.size();
                const float dir = (a == Action::Right) ? 1.0f : -1.0f;
                const float off = slideOffset();
                focus = wrapIndex(focus + (int)dir, count);
                slideFrom = off + dir;  // new focus starts where it was drawn, slides to center
                slideStart = SDL_GetTicks();
            } else if (a == Action::Y) {
                state.toggleFavorite(gallery.themes[cur].folder);
                state.save(statePath);
            }
        }

        SDL_SetRenderDrawColor(ren, 11, 20, 36, 255);
        SDL_RenderClear(ren);
        outline(ren, lay.header);
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

            if (imgs.empty()) {
                const std::string msg = "No preview";
                drawText(ren, fontBody, msg, (w - textWidth(fontBody, msg)) / 2,
                         lay.preview.y + (lay.preview.h - TTF_FontHeight(fontBody)) / 2, COL_DIM);
            } else {
                const int count = (int)imgs.size();
                const int bandH = lay.preview.h;
                const int maxW = w - 2 * lay.margin;
                auto sizeOf = [&](const Img& im) {
                    int iw = std::min(maxW, bandH * im.w / im.h);
                    return SDL_Point{iw, iw * im.h / im.w};
                };
                int slot = 0;
                for (const auto& im : imgs) slot = std::max(slot, sizeOf(im).x);
                const int step = slot + lay.gap;
                const float off = slideOffset();
                const int cx = w / 2, cy = lay.preview.y + bandH / 2;
                const int span = count > 1 ? 2 : 0;

                // pass 0: side images, pass 1: focus slot, so focus is drawn on top
                for (int pass = 0; pass < 2; pass++) {
                    for (int r = -span; r <= span; r++) {
                        if ((pass == 1) != (r == 0)) continue;
                        const int idx = wrapIndex(focus + r, count);
                        if (r != 0 && idx == focus) continue;  // never repeat the focused image beside itself
                        const Img& im = imgs[idx];
                        const float pos = r + off;
                        const SDL_Point sz = sizeOf(im);
                        SDL_Rect dst{cx + (int)std::lround(pos * step) - sz.x / 2, cy - sz.y / 2, sz.x, sz.y};
                        if (dst.x + dst.w < 0 || dst.x > w) continue;
                        const float d = std::min(1.0f, std::fabs(pos));
                        const Uint8 bright = (Uint8)(255 - 115 * d);
                        const Uint8 alpha  = (Uint8)(255 - 55 * d);
                        SDL_SetTextureColorMod(im.tex, bright, bright, bright);
                        SDL_SetTextureAlphaMod(im.tex, alpha);
                        SDL_RenderCopy(ren, im.tex, nullptr, &dst);
                        if (r == 0) outline(ren, dst);
                    }
                }
            }

            drawText(ren, fontTitle, t.name, lay.title.x + (lay.title.w - textWidth(fontTitle, t.name)) / 2,
                     lay.title.y + (lay.title.h - TTF_FontHeight(fontTitle)) / 2, COL_TEXT);

            std::vector<std::string> lines = {
                "Author: " + t.author,
                "Folder: " + t.folder,
                "Size: " + (t.size ? Util::formatBytes(t.size) : std::string("unknown")),
                "Image: " + (imgs.empty() ? std::string("none")
                                          : std::to_string(focus + 1) + "/" + std::to_string(imgs.size())),
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

        drawText(ren, fontSmall, "Up/Down theme    Left/Right image    Y favorite    Select+Start exit", lay.margin,
                 lay.footer.y + (lay.footer.h - TTF_FontHeight(fontSmall)) / 2, COL_DIM);

        SDL_RenderPresent(ren);
    }

    freeImages();
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