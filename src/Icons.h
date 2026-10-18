#pragma once
#include <SDL.h>
#include <string>

// Order matters: arrows first, then face buttons (used as array indices)
enum class Glyph { Up, Down, Left, Right, A, B, X, Y, Start, Select };

// SVG icons rasterized once at the exact pixel size they are drawn at
class Icons {
public:
    explicit Icons(SDL_Renderer* r) : ren_(r) {}
    ~Icons();
    Icons(const Icons&) = delete;
    Icons& operator=(const Icons&) = delete;

    void load(int badge, int smallBadge, int glyph, int arrow);
    SDL_Texture* installed(bool small) const { return small ? installedSmall_ : installed_; }
    SDL_Texture* heart(bool small) const { return small ? heartSmall_ : heart_; }
    SDL_Texture* arrow(Glyph g) const { return arrows_[(int)g]; }                  // Up..Right
    SDL_Texture* button(Glyph g) const { return buttons_[(int)g - (int)Glyph::A]; } // A..Y

private:
    SDL_Renderer* ren_;
    SDL_Texture* installed_ = nullptr;
    SDL_Texture* installedSmall_ = nullptr;
    SDL_Texture* heart_ = nullptr;
    SDL_Texture* heartSmall_ = nullptr;
    SDL_Texture* arrows_[4] = {};
    SDL_Texture* buttons_[4] = {};

    SDL_Texture* svg(const std::string& src, int w, int h);
};
