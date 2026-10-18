#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <memory>
#include <string>
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
    int run();

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
    std::string themesDir;
    uint64_t freeSpace = 0;
    size_t current = 0;  // selected theme, shared by both views

    bool isInstalled(size_t theme) const { return theme < installed_.size() && installed_[theme]; }
    bool isFavorite(size_t theme) const { return state.isFavorite(gallery.themes[theme].folder); }
    void toggleFavorite(size_t theme);
    void requestView(ViewMode v) {
        pendingView_ = v;
        hasPendingView_ = true;
    }

    Glyph okGlyph() const { return es.invertButtons ? Glyph::B : Glyph::A; }
    Glyph backGlyph() const { return es.invertButtons ? Glyph::A : Glyph::B; }

    void drawHeader(const std::string& counter);
    void drawFooter(const std::vector<Hint>& hints);

private:
    SDL_Window* win_ = nullptr;
    Input input_;
    std::unique_ptr<Screen> screen_;
    std::vector<bool> installed_;
    std::string statePath_;
    ViewMode pendingView_ = ViewMode::Carousel;
    bool hasPendingView_ = false;

    bool init();
    void shutdown();
    void refreshInstalled();
    void applyView(ViewMode v);
    int glyph(Glyph g, int x, int cy, bool draw);  // returns width
};
