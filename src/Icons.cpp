#include "Icons.h"
#include "Log.h"
#include <SDL_image.h>

namespace {
const char* SVG_INSTALLED =
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">)"
    R"(<circle cx="12" cy="12" r="11" fill="#2ecc71" stroke="#0b1424" stroke-width="1"/>)"
    R"(<path d="M6.5 12.5 L10.5 16.5 L17.5 8.5" fill="none" stroke="#ffffff" stroke-width="2.6")"
    R"( stroke-linecap="round" stroke-linejoin="round"/></svg>)";

const char* SVG_HEART =
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">)"
    R"(<path d="M12 20.5 C12 20.5 3 14.6 3 8.9 C3 6 5.2 3.8 8 3.8 C9.8 3.8 11.2 4.8 12 6.2)"
    R"( C12.8 4.8 14.2 3.8 16 3.8 C18.8 3.8 21 6 21 8.9 C21 14.6 12 20.5 12 20.5 Z")"
    R"( fill="#ff3b5c" stroke="#0b1424" stroke-width="1"/></svg>)";

std::string arrowSvg(const char* path) {
    return std::string(R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d=")") +
           path + R"(" fill="none" stroke="#c8d2e6" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg>)";
}

std::string circleSvg(const char* color) {
    return std::string(R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><circle cx="12" cy="12" r="11.5" fill=")") +
           color + R"("/></svg>)";
}
}  // namespace

Icons::~Icons() {
    for (SDL_Texture* t : {installed_, installedSmall_, heart_, heartSmall_})
        if (t) SDL_DestroyTexture(t);
    for (SDL_Texture* t : arrows_)
        if (t) SDL_DestroyTexture(t);
    for (SDL_Texture* t : buttons_)
        if (t) SDL_DestroyTexture(t);
}

SDL_Texture* Icons::svg(const std::string& src, int w, int h) {
    SDL_RWops* rw = SDL_RWFromConstMem(src.data(), (int)src.size());
    if (!rw) return nullptr;
    SDL_Surface* s = IMG_LoadSizedSVG_RW(rw, w, h);
    SDL_RWclose(rw);
    if (!s) {
        Log::error("icons", std::string("svg: ") + IMG_GetError());
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(ren_, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

void Icons::load(int badge, int smallBadge, int glyph, int arrow) {
    installed_      = svg(SVG_INSTALLED, badge, badge);
    installedSmall_ = svg(SVG_INSTALLED, smallBadge, smallBadge);
    heart_          = svg(SVG_HEART, badge, badge);
    heartSmall_     = svg(SVG_HEART, smallBadge, smallBadge);

    const char* paths[4] = {"M6 15 L12 9 L18 15", "M6 9 L12 15 L18 9", "M15 6 L9 12 L15 18", "M9 6 L15 12 L9 18"};
    for (int i = 0; i < 4; i++) arrows_[i] = svg(arrowSvg(paths[i]), arrow, arrow);

    const char* colors[4] = {"#e74c3c", "#f1c40f", "#3498db", "#2ecc71"};  // A red, B yellow, X blue, Y green
    for (int i = 0; i < 4; i++) buttons_[i] = svg(circleSvg(colors[i]), glyph, glyph);
}
