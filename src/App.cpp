#include "App.h"
#include <SDL_image.h>
#include <curl/curl.h>
#include <algorithm>
#include "CarouselScreen.h"
#include "Gfx.h"
#include "ListScreen.h"
#include "Log.h"
#include "Paths.h"
#include "Style.h"
#include "Util.h"

namespace {
const char* ES_SETTINGS_PATH = "/home/ark/.emulationstation/es_settings.cfg";
const char* ES_INPUT_PATH    = "/etc/emulationstation/es_input.cfg";
}

int App::run() {
    if (!init()) {
        shutdown();
        return 1;
    }
    std::vector<Action> actions;
    bool running = true;
    while (running) {
        actions.clear();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            input_.handleEvent(e, actions);
        }
        input_.update(actions);
        for (Action a : actions) {
            if (a == Action::Exit) {
                running = false;
                break;
            }
            screen_->handle(a);
        }
        if (hasPendingView_) {  // swapped here, never inside a screen's own handle()
            hasPendingView_ = false;
            applyView(pendingView_);
        }
        images->pump();

        const Uint32 now = SDL_GetTicks();
        SDL_SetRenderDrawColor(ren, Style::BG.r, Style::BG.g, Style::BG.b, 255);
        SDL_RenderClear(ren);
        screen_->render(now);
        SDL_RenderPresent(ren);
        text->prune(now);
    }
    shutdown();
    return 0;
}

bool App::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        Log::error("sdl", SDL_GetError());
        return false;
    }
    SDL_version sv;
    SDL_GetVersion(&sv);
    Log::info("sdl", "SDL " + std::to_string(sv.major) + "." + std::to_string(sv.minor) + "." + std::to_string(sv.patch));
    if (TTF_Init() != 0) {
        Log::error("ttf", TTF_GetError());
        return false;
    }
    const int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) Log::warn("image", IMG_GetError());
    curl_global_init(CURL_GLOBAL_DEFAULT);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");  // linear filtering for scaled textures

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0) {
        Log::error("sdl", SDL_GetError());
        return false;
    }
    win_ = SDL_CreateWindow("ThemeGallery", 0, 0, dm.w, dm.h, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (!win_) {
        Log::error("sdl", SDL_GetError());
        return false;
    }
    ren = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        Log::error("sdl", SDL_GetError());
        return false;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    int w = 0, h = 0;
    SDL_GetRendererOutputSize(ren, &w, &h);
    lay.compute(w, h);
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(ren, &ri);
    Log::info("video", std::string(ri.name) + " " + std::to_string(w) + "x" + std::to_string(h) +
                       "  scale " + std::to_string(lay.scale));

    const std::string appDir = Paths::appDir();
    const std::string semi = appDir + "fonts/Exo2-SemiBold.ttf";
    const std::string reg  = appDir + "fonts/Exo2-Regular.ttf";
    fonts.header    = TTF_OpenFont(semi.c_str(), lay.fontHeader);
    fonts.title     = TTF_OpenFont(semi.c_str(), lay.fontTitle);
    fonts.body      = TTF_OpenFont(reg.c_str(), lay.fontBody);
    fonts.bodyBold  = TTF_OpenFont(semi.c_str(), lay.fontBody);
    fonts.small     = TTF_OpenFont(reg.c_str(), lay.fontSmall);
    fonts.smallBold = TTF_OpenFont(semi.c_str(), lay.fontSmall);
    fonts.tiny      = TTF_OpenFont(semi.c_str(), lay.fontTiny);
    if (!fonts.header || !fonts.title || !fonts.body || !fonts.bodyBold || !fonts.small || !fonts.smallBold ||
        !fonts.tiny) {
        Log::error("ttf", TTF_GetError());
        return false;
    }

    text = std::make_unique<TextCache>(ren);
    icons = std::make_unique<Icons>(ren);
    icons->load(lay.px(22), lay.px(12), lay.px(18), lay.px(18) - 2 * lay.px(2));

    es.load(ES_SETTINGS_PATH);
    Log::info("settings", std::string("InvertButtons=") + (es.invertButtons ? "true" : "false") + "  ThemeSet=" + es.themeSet);
    EsInputFile esInput;
    esInput.load(ES_INPUT_PATH);
    if (!input_.open(esInput, es.invertButtons)) {
        SDL_SetRenderDrawColor(ren, Style::BG.r, Style::BG.g, Style::BG.b, 255);
        SDL_RenderClear(ren);
        text->draw(fonts.body, "Controller setup failed - see ThemeGallery.log", lay.margin, lay.margin, Style::ERR);
        SDL_RenderPresent(ren);
        SDL_Delay(5000);
        return false;
    }

    themesDir = Paths::selectThemesDir();
    freeSpace = Paths::freeBytes(themesDir);
    Log::info("paths", "free " + Util::formatBytes(freeSpace));
    gallery.load(appDir + "gallery.ini", appDir + "Images/");
    statePath_ = appDir + "themegallery.cfg";
    state.load(statePath_);
    refreshInstalled();

    images = std::make_unique<ImageCache>(ren, gallery, lay.preview.h, lay.thumbH);
    applyView(state.view);
    return true;
}

void App::shutdown() {
    screen_.reset();
    images.reset();  // joins the worker and frees textures before the renderer goes
    icons.reset();
    text.reset();
    input_.close();
    for (TTF_Font* f : {fonts.header, fonts.title, fonts.body, fonts.bodyBold, fonts.small, fonts.smallBold, fonts.tiny})
        if (f) TTF_CloseFont(f);
    fonts = Fonts{};
    if (ren) SDL_DestroyRenderer(ren);
    if (win_) SDL_DestroyWindow(win_);
    ren = nullptr;
    win_ = nullptr;
    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

void App::refreshInstalled() {
    installed_.assign(gallery.themes.size(), false);
    for (size_t i = 0; i < gallery.themes.size(); i++)
        installed_[i] = Paths::isDir(themesDir + "/" + gallery.themes[i].folder);
}

void App::toggleFavorite(size_t theme) {
    state.toggleFavorite(gallery.themes[theme].folder);
    state.save(statePath_);
}

void App::applyView(ViewMode v) {
    if (v == ViewMode::List) screen_ = std::make_unique<ListScreen>(*this);
    else screen_ = std::make_unique<CarouselScreen>(*this);
    screen_->enter();
    if (state.view != v) {
        state.view = v;
        state.save(statePath_);
    }
}

void App::drawHeader(const std::string& counter) {
    const SDL_Rect& hd = lay.header;
    const int y = hd.y + (hd.h - TTF_FontHeight(fonts.header)) / 2;
    const int x = lay.margin + text->draw(fonts.header, "Theme", lay.margin, y, Style::TEXT);
    text->draw(fonts.header, "Gallery", x, y, Style::ACCENT);
    if (!counter.empty())
        text->drawIn(fonts.small, counter, SDL_Rect{hd.x, hd.y, hd.w - lay.margin, hd.h}, Style::DIM, Align::Right);
    SDL_SetRenderDrawColor(ren, Style::PANEL_BORDER.r, Style::PANEL_BORDER.g, Style::PANEL_BORDER.b, 255);
    SDL_RenderDrawLine(ren, 0, hd.y + hd.h - 1, lay.w, hd.y + hd.h - 1);
}

int App::glyph(Glyph g, int x, int cy, bool draw) {
    const int gs = lay.px(18);
    const SDL_Rect box{x, cy - gs / 2, gs, gs};
    switch (g) {
    case Glyph::Up:
    case Glyph::Down:
    case Glyph::Left:
    case Glyph::Right:
        if (draw) {
            Gfx::strokeRoundRect(ren, box, lay.px(4), 1, Style::DIM);
            const int in = lay.px(2);
            SDL_Rect d{box.x + in, box.y + in, gs - 2 * in, gs - 2 * in};
            SDL_RenderCopy(ren, icons->arrow(g), nullptr, &d);
        }
        return gs;
    case Glyph::A:
    case Glyph::B:
    case Glyph::X:
    case Glyph::Y:
        if (draw) {
            SDL_RenderCopy(ren, icons->button(g), nullptr, &box);
            const char* letter = g == Glyph::A ? "A" : g == Glyph::B ? "B" : g == Glyph::X ? "X" : "Y";
            text->drawIn(fonts.smallBold, letter, box, g == Glyph::Y ? Style::BG : Style::TEXT, Align::Center);
        }
        return gs;
    case Glyph::Start:
    case Glyph::Select: {
        const char* label = g == Glyph::Start ? "START" : "SELECT";
        const int w = text->width(fonts.tiny, label) + lay.px(10);
        if (draw) {
            const SDL_Rect pill{x, box.y, w, gs};
            Gfx::strokeRoundRect(ren, pill, gs / 2, 1, Style::DIM);
            text->drawIn(fonts.tiny, label, pill, Style::DIM, Align::Center);
        }
        return w;
    }
    }
    return 0;
}

void App::drawFooter(const std::vector<Hint>& hints) {
    const SDL_Rect& f = lay.footer;
    SDL_SetRenderDrawColor(ren, Style::PANEL_BORDER.r, Style::PANEL_BORDER.g, Style::PANEL_BORDER.b, 255);
    SDL_RenderDrawLine(ren, 0, f.y, lay.w, f.y);

    const int cy = f.y + f.h / 2;
    const int gGap = lay.px(3), labelGap = lay.px(5);
    std::vector<int> widths;
    int total = 0;
    for (const auto& h : hints) {
        int w = 0;
        for (size_t i = 0; i < h.glyphs.size(); i++) w += (i ? gGap : 0) + glyph(h.glyphs[i], 0, cy, false);
        w += labelGap + text->width(fonts.small, h.label);
        widths.push_back(w);
        total += w;
    }
    int spacing = 0;
    if (hints.size() > 1)
        spacing = std::clamp((lay.w - 2 * lay.margin - total) / (int)(hints.size() - 1), lay.px(8), lay.px(28));

    const int ly = cy - TTF_FontHeight(fonts.small) / 2;
    int x = lay.margin;
    for (size_t k = 0; k < hints.size(); k++) {
        const Hint& h = hints[k];
        int gx = x;
        for (size_t i = 0; i < h.glyphs.size(); i++) {
            if (i) gx += gGap;
            gx += glyph(h.glyphs[i], gx, cy, true);
        }
        text->draw(fonts.small, h.label, gx + labelGap, ly, Style::DIM);
        x += widths[k] + spacing;
    }
}
