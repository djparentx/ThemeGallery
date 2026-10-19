#include "App.h"
#include <SDL_image.h>
#include <curl/curl.h>
#include <dirent.h>
#include <algorithm>
#include <cstdint>
#include "CarouselScreen.h"
#include "DialogScreen.h"
#include "DownloadScreen.h"
#include "Gfx.h"
#include "ListScreen.h"
#include "Log.h"
#include "MenuScreen.h"
#include "Net.h"
#include "Paths.h"
#include "RemovingScreen.h"
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
            Screen* target = overlays_.empty() ? screen_.get() : overlays_.back().get();
            target->handle(a);
            applyPending();
            if (exitRequested_) {
                running = false;
                break;
            }
        }

        // launch connection check: report once if offline
        if (!netReported_ && netState_ != NET_CHECKING) {
            netReported_ = true;
            if (netState_ == NET_OFFLINE) {
                message("Offline", "Internet Required to Download - Browsing Only");
                applyPending();
            }
        }
        images->pump();

        const Uint32 now = SDL_GetTicks();
        SDL_SetRenderDrawColor(ren, Style::BG.r, Style::BG.g, Style::BG.b, 255);
        SDL_RenderClear(ren);
        screen_->render(now);
        for (auto& o : overlays_) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
            SDL_RenderFillRect(ren, nullptr);
            o->render(now);
        }
        SDL_RenderPresent(ren);
        applyPending();  // overlays may finish during render
        text->prune(now);
    }
    shutdown();
    return themesChanged_ ? 3 : 0;
}

void App::applyPending() {
    for (; pendingPops_ > 0 && !overlays_.empty(); pendingPops_--) overlays_.pop_back();
    pendingPops_ = 0;
    std::vector<std::unique_ptr<Screen>> pushes = std::move(pendingPush_);
    pendingPush_.clear();
    for (auto& s : pushes) {
        overlays_.push_back(std::move(s));
        overlays_.back()->enter();  // pushes made here land next frame
    }
    if (hasPendingView_) {
        hasPendingView_ = false;
        applyView(pendingView_);
    }
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
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

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
    scanDirs_ = Paths::scanDirs(themesDir);
    refreshFreeSpace();
    gallery.load(appDir + "gallery.ini", appDir + "Images/");
    statePath_ = appDir + "themegallery.cfg";
    state.load(statePath_);
    refreshInstalled();
    rebuildVisible();
    cleanupLeftovers();

    images = std::make_unique<ImageCache>(ren, gallery, lay.preview.h, lay.thumbH);
    applyView(state.view);

    netThread_ = std::thread([this] {
        const int result = Net::checkInternet() ? NET_ONLINE : NET_OFFLINE;
        int expected = NET_CHECKING;
        netState_.compare_exchange_strong(expected, result);  // a user-started check may have answered first
    });
    return true;
}

void App::shutdown() {
    overlays_.clear();  // running removals/installs cancel and join here
    pendingPush_.clear();
    screen_.reset();
    images.reset();  // joins the decode worker and frees textures before the renderer goes
    icons.reset();
    text.reset();
    input_.close();
    if (netThread_.joinable()) netThread_.join();
    if (cleanupThread_.joinable()) cleanupThread_.join();
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

void App::cleanupLeftovers() {
    // snapshot now, so a work folder created later is never touched
    std::vector<std::string> stale;
    for (const auto& d : scanDirs_) {
        DIR* dir = opendir(d.c_str());
        if (!dir) continue;
        while (dirent* e = readdir(dir)) {
            const std::string n = e->d_name;
            if (n.rfind(".tg_tmp_", 0) == 0 || n.rfind(".tg_old_", 0) == 0) stale.push_back(d + "/" + n);
        }
        closedir(dir);
    }
    if (stale.empty()) return;
    cleanupThread_ = std::thread([stale] {
        for (const auto& p : stale) {
            Log::info("cleanup", "removing " + p);
            Paths::removeTree(p);
        }
    });
}

void App::refreshInstalled() {
    installedIn_.assign(gallery.themes.size(), {});
    for (size_t i = 0; i < gallery.themes.size(); i++)
        for (const auto& d : scanDirs_)
            if (Paths::isDir(d + "/" + gallery.themes[i].folder)) installedIn_[i].push_back(d);
}

void App::refreshFreeSpace() {
    freeSpace = Paths::freeBytes(themesDir);
    Log::info("paths", "free " + Util::formatBytes(freeSpace));
}

std::string App::installTarget(size_t theme) const {
    const std::vector<std::string>& dirs = installedIn_[theme];
    if (dirs.empty()) return themesDir;
    const std::string& folder = gallery.themes[theme].folder;
    std::string best = dirs.front();
    long long newest = Paths::mtime(best + "/" + folder);
    for (size_t i = 1; i < dirs.size(); i++) {
        const long long m = Paths::mtime(dirs[i] + "/" + folder);
        if (m > newest) {
            newest = m;
            best = dirs[i];
        }
    }
    return best;
}

size_t App::visiblePos() const {
    for (size_t i = 0; i < visible_.size(); i++)
        if (visible_[i] == current) return i;
    return 0;
}

size_t App::neighbor(int dir) const {
    if (visible_.empty()) return current;
    return visible_[Util::wrapIndex((int)visiblePos() + dir, (int)visible_.size())];
}

bool App::matches(size_t theme, Filter f) const {
    switch (f) {
    case Filter::Favorites: return isFavorite(theme);
    case Filter::Batocera:  return gallery.themes[theme].category == Category::Batocera;
    case Filter::Fcamod:    return gallery.themes[theme].category == Category::Fcamod;
    case Filter::All:       break;
    }
    return true;
}

const char* App::filterName(Filter f) {
    switch (f) {
    case Filter::Favorites: return "Favorites";
    case Filter::Batocera:  return "Batocera";
    case Filter::Fcamod:    return "FCAMOD";
    case Filter::All:       break;
    }
    return "All";
}

void App::rebuildVisible() {
    visible_.clear();
    for (size_t i = 0; i < gallery.themes.size(); i++)
        if (matches(i, filter_)) visible_.push_back(i);
    if (!visible_.empty() && std::find(visible_.begin(), visible_.end(), current) == visible_.end())
        current = visible_.front();
}

size_t App::nextMatch(size_t from, Filter f) const {
    const size_t n = gallery.themes.size();
    for (size_t k = 1; k <= n; k++) {
        const size_t i = (from + k) % n;
        if (matches(i, f)) return i;
    }
    return SIZE_MAX;
}

bool App::setFilter(Filter f) {
    if (f == filter_) return true;
    if (gallery.themes.empty()) return false;
    const size_t next = matches(current, f) ? current : nextMatch(current, f);
    if (next == SIZE_MAX) return false;  // nothing matches: ignore
    current = next;
    filter_ = f;
    rebuildVisible();
    screen_->enter();
    return true;
}

void App::toggleFavorite(size_t theme) {
    const std::string& folder = gallery.themes[theme].folder;
    state.toggleFavorite(folder);
    state.save(statePath_);
    if (filter_ != Filter::Favorites || state.isFavorite(folder)) return;
    // removed while filtering favorites: move to the next one, or back to All if none remain
    const size_t next = nextMatch(theme, Filter::Favorites);
    if (next == SIZE_MAX) filter_ = Filter::All;
    else current = next;
    rebuildVisible();
    screen_->enter();
}

void App::drawCategoryBadge(const SDL_Rect& r, bool small) {
    SDL_RenderCopy(ren, icons->batocera(small), nullptr, &r);
    text->drawIn(small ? fonts.tiny : fonts.bodyBold, "b", r, SDL_Color{0, 0, 0, 255}, Align::Center);
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

void App::openMenu() {
    pushOverlay(std::make_unique<MenuScreen>(*this));
}

void App::confirmExit() {
    DialogSpec spec;
    spec.title = "Exit?";
    spec.buttons = {{"Yes", [this] { requestExit(); }}, {"No", nullptr}};
    spec.defaultButton = 1;  // No
    pushOverlay(std::make_unique<DialogScreen>(*this, std::move(spec)));
}

void App::message(const std::string& title, const std::string& msg) {
    DialogSpec spec;
    spec.title = title;
    spec.message = msg;
    spec.buttons = {{"OK", nullptr}};
    pushOverlay(std::make_unique<DialogScreen>(*this, std::move(spec)));
}

void App::openThemeActions() {
    if (visible_.empty()) return;
    const size_t theme = current;
    const Theme& t = gallery.themes[theme];
    DialogSpec spec;
    spec.theme = theme;
    if (!isInstalled(theme)) {
        spec.title = "Install Theme";
        spec.message = "Do you want to install \"" + t.name + "\"?";
        spec.buttons = {{"Install", [this, theme] { startInstall(theme); }}, {"Cancel", nullptr}};
        spec.showThumb = true;
    } else {
        spec.title = t.name;
        spec.message = "Theme is already installed.";
        const bool current = isCurrentTheme(theme);
        spec.buttons = {{current ? "Current Theme" : "Set Theme", [this, theme] { setTheme(theme); }, current},
                        {"Reinstall", [this, theme] { startInstall(theme); }},
                        {"Uninstall", [this, theme] { confirmUninstall(theme); }}};
        spec.defaultButton = current ? 1 : 0;
        spec.style = ButtonStyle::List;
    }
    pushOverlay(std::make_unique<DialogScreen>(*this, std::move(spec)));
}

void App::setTheme(size_t theme) {
    const Theme& t = gallery.themes[theme];
    if (!es.saveThemeSet(t.folder)) {
        message("Set Theme Failed", "Could not update " + es.path + ". See ThemeGallery.log.");
        return;
    }
    markThemesChanged();  // exit code 3: ES restarts with the new theme
    message("Theme Set", "\"" + t.name + "\" will be applied when you exit ThemeGallery.");
}

void App::confirmUninstall(size_t theme) {
    const Theme& t = gallery.themes[theme];
    if (t.folder == es.themeSet) {
        message("Theme In Use",
                "This theme is in use by EmulationStation. Select a different theme in ES before uninstalling.");
        return;
    }
    DialogSpec spec;
    spec.title = "Uninstall Theme";
    spec.message = "Uninstall \"" + t.name + "\"? It will be deleted from " + installTarget(theme) + ".";
    spec.buttons = {{"Uninstall", [this, theme] { pushOverlay(std::make_unique<RemovingScreen>(*this, theme)); }},
                    {"Cancel", nullptr}};
    spec.defaultButton = 1;  // destructive: Cancel is preselected
    spec.theme = theme;
    pushOverlay(std::make_unique<DialogScreen>(*this, std::move(spec)));
}

void App::startInstall(size_t theme) {
    // offline or launch check still running: the download screen checks first
    pushOverlay(std::make_unique<DownloadScreen>(*this, theme, installTarget(theme), !online()));
}

void App::drawHeader(const std::string& counter) {
    const SDL_Rect& hd = lay.header;
    const int y = hd.y + (hd.h - TTF_FontHeight(fonts.header)) / 2;
    const int x = lay.margin + text->draw(fonts.header, "Theme", lay.margin, y, Style::TEXT);
    text->draw(fonts.header, "Gallery", x, y, Style::ACCENT);

    const int cw = counter.empty() ? 0 : text->width(fonts.small, counter);
    if (!counter.empty())
        text->drawIn(fonts.small, counter, SDL_Rect{hd.x, hd.y, hd.w - lay.margin, hd.h}, Style::DIM, Align::Right);

    if (filter_ != Filter::All) {
        const std::string lbl = filterName(filter_);
        const int ms = lay.px(12);
        const bool icon = filter_ != Filter::Fcamod;
        const int lw = text->width(fonts.small, lbl);
        const int chipW = lay.px(7) + (icon ? ms + lay.px(4) : 0) + lw + lay.px(8);
        const int chipH = lay.px(20);
        const SDL_Rect chip{hd.w - lay.margin - cw - lay.px(10) - chipW, hd.y + (hd.h - chipH) / 2, chipW, chipH};
        Gfx::strokeRoundRect(ren, chip, chipH / 2, 1, Style::PANEL_BORDER);
        int tx = chip.x + lay.px(7);
        if (icon) {
            const SDL_Rect ir{tx, chip.y + (chipH - ms) / 2, ms, ms};
            if (filter_ == Filter::Favorites) SDL_RenderCopy(ren, icons->heart(true), nullptr, &ir);
            else drawCategoryBadge(ir, true);
            tx += ms + lay.px(4);
        }
        text->drawIn(fonts.small, lbl, SDL_Rect{tx, chip.y, lw, chipH}, Style::DIM, Align::Left);
    }

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
            // dark letter on the yellow button, white on the rest
            text->drawIn(fonts.smallBold, letter, box, g == Glyph::B ? Style::BG : Style::TEXT, Align::Center);
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
    SDL_SetRenderDrawColor(ren, Style::BG.r, Style::BG.g, Style::BG.b, 255);
    SDL_RenderFillRect(ren, &f);  // overlays draw their own footer over the dimmed one
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