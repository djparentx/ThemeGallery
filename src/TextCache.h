#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <unordered_map>

enum class Align { Left, Center, Right };

// Keeps rendered text as textures so nothing is rasterized per frame
class TextCache {
public:
    explicit TextCache(SDL_Renderer* r) : ren_(r) {}
    ~TextCache();
    TextCache(const TextCache&) = delete;
    TextCache& operator=(const TextCache&) = delete;

    int width(TTF_Font* f, const std::string& s);
    std::string fit(TTF_Font* f, const std::string& s, int maxW);  // trims with "...", whole UTF-8 chars
    // x is the left edge, center or right edge depending on a; returns drawn width
    int draw(TTF_Font* f, const std::string& s, int x, int y, SDL_Color c, Align a = Align::Left);
    int drawIn(TTF_Font* f, const std::string& s, const SDL_Rect& rc, SDL_Color c, Align a);  // vertically centered
    void prune(Uint32 now);

private:
    struct Entry {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        Uint32 used = 0;
    };
    SDL_Renderer* ren_;
    std::unordered_map<std::string, Entry> tex_;
    std::unordered_map<std::string, int> widths_;
    std::unordered_map<std::string, std::string> fits_;
    Uint32 lastPrune_ = 0;

    static std::string key(TTF_Font* f, const std::string& s);
};
