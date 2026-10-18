#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "Config.h"
#include "EsSettings.h"
#include "Icons.h"
#include "ImageCache.h"
#include "Input.h"
#include "Layout.h"
#include "Screen.h"
#include "TextCache.h"

struct Fonts {
    TTF_Font* header = nullptr;
    TTF_Font* title = nullptr;
    TTF_Font* body = nullptr;
    TTF_Font* bodyBold = nullptr;
    TTF_Font* small = nullptr;
    TTF_Font* smallBold = nullptr;
    TTF_Font* tiny = nullptr;
};

struct Hint {
    std::vector<Glyph> glyphs;
    std::string label;
};

class App {
public:
    int run();  // 0 = normal exit, 3 = themes changed (launcher restarts ES), 1 = init failure

    // shared with screens
    SDL_Renderer* ren = nullptr;
    Layout lay;
    Fonts fonts;
    std::unique_ptr<TextCache> text;
    std::unique_ptr<Icons> icons;
    std::unique_ptr<ImageCache> images;
    Gallery gallery;
    UserState state;
    EsSettings es;
    std::string themesDir;  // install target
    uint64_t freeSpace = 0;
    size_t current = 0;  // selected theme, index into gallery.themes

    // visible list: all themes, or favorites only
    size_t visibleCount() const { return visible_.size(); }
    size_t visibleAt(size_t pos) const { return visible_[pos]; }
    size_t visiblePos() const;
    size_t neighbor(int dir) const;  // theme dir steps from current in the visible list, wrapping
    void step(int dir) { current = neighbor(dir); }
    bool favoritesOnly() const { return favoritesOnly_; }
    bool setFilter(bool favoritesOnly);  // false = Favorites requested but none exist (ignored)

    bool isInstalled(size_t theme) const { return theme < installedIn_.size() && !installedIn_[theme].empty(); }
    const std::vector<std::string>& installedIn(size_t theme) const { return installedIn_[theme]; }
    bool isCurrentTheme(size_t theme) const { return gallery.themes[theme].folder == es.themeSet; }
    bool isFavorite(size_t theme) const { return state.isFavorite(gallery.themes[theme].folder); }
    void toggleFavorite(size_t theme);
    void refreshInstalled();
    void refreshFreeSpace();
    void markThemesChanged() { themesChanged_ = true; }

    // network state; safe from any thread
    bool online() const { return netState_ == NET_ONLINE; }
    void setOnline(bool on) {
        netState_ = on ? NET_ONLINE : NET_OFFLINE;
        netReported_ = true;  // a check the user started reports its own result
    }

    // navigation; all changes are applied after the current action finishes
    void requestView(ViewMode v) {
        pendingView_ = v;
        hasPendingView_ = true;
    }
    void pushOverlay(std::unique_ptr<Screen> s) { pendingPush_.push_back(std::move(s)); }
    void popOverlay() { pendingPops_++; }
    void requestExit() { exitRequested_ = true; }
    void openMenu();
    void openThemeActions();  // OK on the selected theme
    void message(const std::string& title, const std::string& text);

    Glyph okGlyph() const { return es.invertButtons ? Glyph::B : Glyph::A; }
    Glyph backGlyph() const { return es.invertButtons ? Glyph::A : Glyph::B; }

    void drawHeader(const std::string& counter);
    void drawFooter(const std::vector<Hint>& hints);
    SDL_Rect panelRect(int w, int h) const { return {(lay.w - w) / 2, (lay.h - h) / 2, w, h}; }

private:
    enum { NET_CHECKING, NET_ONLINE, NET_OFFLINE };

    SDL_Window* win_ = nullptr;
    Input input_;
    std::unique_ptr<Screen> screen_;
    std::vector<std::unique_ptr<Screen>> overlays_;
    std::vector<std::unique_ptr<Screen>> pendingPush_;
    int pendingPops_ = 0;
    std::vector<std::vector<std::string>> installedIn_;  // per theme: dirs that hold it, in scan order
    std::vector<std::string> scanDirs_;
    std::vector<size_t> visible_;
    std::string statePath_;
    ViewMode pendingView_ = ViewMode::Carousel;
    bool hasPendingView_ = false;
    bool favoritesOnly_ = false;
    bool themesChanged_ = false;
    bool exitRequested_ = false;

    std::atomic<int> netState_{NET_CHECKING};
    std::atomic<bool> netReported_{false};
    std::thread netThread_;
    std::thread cleanupThread_;

    bool init();
    void shutdown();
    void applyPending();
    void applyView(ViewMode v);
    void rebuildVisible();
    size_t nextFavorite(size_t from) const;  // SIZE_MAX if none
    void confirmUninstall(size_t theme);
    void setTheme(size_t theme);
    void startInstall(size_t theme);
    std::string installTarget(size_t theme) const;  // existing (newest) copy's dir, else themesDir
    void cleanupLeftovers();
    int glyph(Glyph g, int x, int cy, bool draw);  // returns width
};