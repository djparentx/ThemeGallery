#include "TextCache.h"
#include "Log.h"
#include <cstdint>
#include <cstdio>

namespace {
constexpr Uint32 KEEP_MS = 10000;  // unused textures freed after this
}

TextCache::~TextCache() {
    for (auto& kv : tex_)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
}

std::string TextCache::key(TTF_Font* f, const std::string& s) {
    return std::to_string((uintptr_t)f) + '\x1f' + s;
}

int TextCache::width(TTF_Font* f, const std::string& s) {
    const std::string k = key(f, s);
    auto it = widths_.find(k);
    if (it != widths_.end()) return it->second;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s.c_str(), &w, &h);
    widths_[k] = w;
    return w;
}

std::string TextCache::fit(TTF_Font* f, const std::string& s, int maxW) {
    const std::string k = key(f, s) + '\x1f' + std::to_string(maxW);
    auto it = fits_.find(k);
    if (it != fits_.end()) return it->second;
    std::string out;
    if (width(f, s) <= maxW) {
        out = s;
    } else {
        std::string t = s;
        while (!t.empty()) {
            while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();  // continuation bytes
            if (!t.empty()) t.pop_back();                                                  // lead byte or ASCII
            const std::string cand = t + "...";
            int cw = 0, ch = 0;
            TTF_SizeUTF8(f, cand.c_str(), &cw, &ch);
            if (cw <= maxW) {
                out = cand;
                break;
            }
        }
    }
    fits_[k] = out;
    return out;
}

int TextCache::draw(TTF_Font* f, const std::string& s, int x, int y, SDL_Color c, Align a) {
    static bool loggedFail = false;
    if (s.empty()) return 0;
    char ck[12];
    std::snprintf(ck, sizeof(ck), "%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    const std::string k = key(f, s) + '\x1f' + ck;
    auto it = tex_.find(k);
    if (it == tex_.end()) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) {
            if (!loggedFail) Log::error("text", std::string("render: ") + TTF_GetError());
            loggedFail = true;
            return 0;
        }
        Entry e;
        e.tex = SDL_CreateTextureFromSurface(ren_, surf);
        e.w = surf->w;
        e.h = surf->h;
        SDL_FreeSurface(surf);
        if (!e.tex) {
            if (!loggedFail) Log::error("text", std::string("texture: ") + SDL_GetError());
            loggedFail = true;
            return 0;
        }
        it = tex_.emplace(k, e).first;
    }
    Entry& e = it->second;
    e.used = SDL_GetTicks();
    int dx = x;
    if (a == Align::Center) dx = x - e.w / 2;
    else if (a == Align::Right) dx = x - e.w;
    SDL_Rect dst{dx, y, e.w, e.h};
    SDL_RenderCopy(ren_, e.tex, nullptr, &dst);
    return e.w;
}

int TextCache::drawIn(TTF_Font* f, const std::string& s, const SDL_Rect& rc, SDL_Color c, Align a) {
    const int y = rc.y + (rc.h - TTF_FontHeight(f)) / 2;
    const int x = a == Align::Left ? rc.x : (a == Align::Center ? rc.x + rc.w / 2 : rc.x + rc.w);
    return draw(f, s, x, y, c, a);
}

void TextCache::prune(Uint32 now) {
    if (now - lastPrune_ < 1000) return;
    lastPrune_ = now;
    for (auto it = tex_.begin(); it != tex_.end();) {
        if (now - it->second.used > KEEP_MS) {
            SDL_DestroyTexture(it->second.tex);
            it = tex_.erase(it);
        } else {
            ++it;
        }
    }
    if (widths_.size() > 4000) widths_.clear();
    if (fits_.size() > 4000) fits_.clear();
}

std::vector<std::string> TextCache::wrap(TTF_Font* f, const std::string& s, int maxW) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        const std::string word = s.substr(i, j - i);
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(f, cand) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = cand;
        }
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    for (auto& l : lines) l = fit(f, l, maxW);  // a single word wider than maxW gets trimmed
    return lines;
}