#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include "EsSettings.h"
#include "Input.h"
#include "Log.h"

namespace {
const char* ES_SETTINGS_PATH = "/home/ark/.emulationstation/es_settings.cfg";
const char* ES_INPUT_PATH    = "/etc/emulationstation/es_input.cfg";

// DIAG: logs max alpha and coverage of a 32-bit surface
void logAlpha(const char* label, SDL_Surface* s) {
    if (!s || s->format->BytesPerPixel != 4) {
        Log::warn("diag", std::string(label) + ": not a 32-bit surface");
        return;
    }
    SDL_LockSurface(s);
    Uint32 maxA = 0;
    int nonzero = 0;
    for (int yy = 0; yy < s->h; yy++) {
        const Uint32* row = (const Uint32*)((const Uint8*)s->pixels + yy * s->pitch);
        for (int xx = 0; xx < s->w; xx++) {
            Uint32 a = (row[xx] & s->format->Amask) >> s->format->Ashift;
            if (a > maxA) maxA = a;
            if (a) nonzero++;
        }
    }
    SDL_UnlockSurface(s);
    Log::info("diag", std::string(label) + " alpha max=" + std::to_string(maxA) + "  nonzero=" + std::to_string(nonzero) +
                      "/" + std::to_string(s->w * s->h) + "  " + std::to_string(s->w) + "x" + std::to_string(s->h));
}

// DIAG: font identity, glyph 'A' metrics and rendered coverage
void glyphDiag(const char* label, TTF_Font* f) {
    const char* fam = TTF_FontFaceFamilyName(f);
    const char* sty = TTF_FontFaceStyleName(f);
    Log::info("diag", std::string(label) + " family=" + (fam ? fam : "?") + "  style=" + (sty ? sty : "?") +
                      "  faces=" + std::to_string(TTF_FontFaces(f)) + "  fixed=" + std::to_string(TTF_FontFaceIsFixedWidth(f)) +
                      "  height=" + std::to_string(TTF_FontHeight(f)));
    int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
    int provided = TTF_GlyphIsProvided32(f, 'A');
    if (TTF_GlyphMetrics32(f, 'A', &minx, &maxx, &miny, &maxy, &adv) == 0)
        Log::info("diag", std::string(label) + " 'A' provided=" + std::to_string(provided) +
                          "  x=" + std::to_string(minx) + ".." + std::to_string(maxx) +
                          "  y=" + std::to_string(miny) + ".." + std::to_string(maxy) + "  adv=" + std::to_string(adv));
    else
        Log::error("diag", std::string(label) + " TTF_GlyphMetrics32: " + TTF_GetError());
    SDL_Surface* g = TTF_RenderGlyph32_Blended(f, 'A', SDL_Color{255, 255, 255, 255});
    if (g) {
        logAlpha((std::string(label) + " 'A'").c_str(), g);
        SDL_FreeSurface(g);
    } else {
        Log::error("diag", std::string(label) + " TTF_RenderGlyph32_Blended: " + TTF_GetError());
    }
}

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
    if (!loggedAlpha) {
        logAlpha("drawText", surf);
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
    const SDL_version* tv = TTF_Linked_Version();
    int ftMaj = 0, ftMin = 0, ftPat = 0;
    TTF_GetFreeTypeVersion(&ftMaj, &ftMin, &ftPat);
    Log::info("ttf", "SDL_ttf " + std::to_string(tv->major) + "." + std::to_string(tv->minor) + "." + std::to_string(tv->patch) +
                     "  freetype " + std::to_string(ftMaj) + "." + std::to_string(ftMin) + "." + std::to_string(ftPat));
    {
        std::ifstream maps("/proc/self/maps");
        std::string ml, lastPath;
        while (std::getline(maps, ml)) {
            size_t p = ml.find('/');
            if (p == std::string::npos) continue;
            std::string path = ml.substr(p);
            if (path != lastPath && (path.find("freetype") != std::string::npos ||
                                     path.find("SDL2_ttf") != std::string::npos ||
                                     path.find("libSDL2-2.0") != std::string::npos))
                Log::info("ttf", "loaded " + path);
            lastPath = path;
        }
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

    // DIAG: compare Exo 2 against the ES fallback font
    glyphDiag("exo2", font);
    const char* FALLBACK_FONT = "/usr/bin/emulationstation/resources/fallback.ttf";
    TTF_Font* fallbackFont = TTF_OpenFont(FALLBACK_FONT, std::max(12, h / 18));
    if (fallbackFont) glyphDiag("fallback", fallbackFont);
    else Log::error("diag", std::string(FALLBACK_FONT) + ": " + TTF_GetError());

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
        if (fallbackFont) drawText(ren, fallbackFont, "FALLBACK TEST", w / 20, h / 2, white);
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
	
    TTF_CloseFont(font);
    if (fallbackFont) TTF_CloseFont(fallbackFont);
    input.close();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}