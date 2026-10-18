#include "Gfx.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int SEGS = 8;  // segments per corner arc
constexpr float PI = 3.14159265f;

// Outline of a rounded rect, clockwise starting at the top-left arc
std::vector<SDL_FPoint> roundPoints(float x, float y, float w, float h, float r) {
    r = std::max(0.0f, std::min(r, std::min(w, h) / 2.0f));
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r};
    const float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float start[4] = {PI, 1.5f * PI, 0.0f, 0.5f * PI};
    std::vector<SDL_FPoint> pts;
    pts.reserve(4 * (SEGS + 1));
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i <= SEGS; i++) {
            float a = start[c] + (0.5f * PI) * i / SEGS;
            pts.push_back({cx[c] + r * std::cos(a), cy[c] + r * std::sin(a)});
        }
    }
    return pts;
}

// Triangle fan from the center; UVs map the rect onto the texture
void fan(SDL_Renderer* ren, SDL_Texture* tex, const SDL_Rect& rc, int radius, SDL_Color c) {
    if (rc.w <= 0 || rc.h <= 0) return;
    std::vector<SDL_FPoint> pts = roundPoints((float)rc.x, (float)rc.y, (float)rc.w, (float)rc.h, (float)radius);
    auto uv = [&](const SDL_FPoint& p) {
        return SDL_FPoint{(p.x - rc.x) / (float)rc.w, (p.y - rc.y) / (float)rc.h};
    };
    std::vector<SDL_Vertex> v;
    v.reserve(pts.size() + 1);
    const SDL_FPoint center{rc.x + rc.w / 2.0f, rc.y + rc.h / 2.0f};
    v.push_back({center, c, uv(center)});
    for (const auto& p : pts) v.push_back({p, c, uv(p)});
    const int n = (int)pts.size();
    std::vector<int> idx;
    idx.reserve(n * 3);
    for (int i = 0; i < n; i++) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    SDL_RenderGeometry(ren, tex, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}
}  // namespace

namespace Gfx {

void fillRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int radius, SDL_Color c) {
    fan(r, nullptr, rc, radius, c);
}

void drawRoundImage(SDL_Renderer* r, SDL_Texture* tex, const SDL_Rect& rc, int radius, SDL_Color mod) {
    if (tex) fan(r, tex, rc, radius, mod);
}

void strokeRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int radius, int t, SDL_Color c) {
    if (rc.w <= 2 * t || rc.h <= 2 * t || t <= 0) return;
    std::vector<SDL_FPoint> outer = roundPoints((float)rc.x, (float)rc.y, (float)rc.w, (float)rc.h, (float)radius);
    std::vector<SDL_FPoint> inner = roundPoints((float)(rc.x + t), (float)(rc.y + t), (float)(rc.w - 2 * t),
                                                (float)(rc.h - 2 * t), (float)std::max(0, radius - t));
    const int n = (int)outer.size();
    std::vector<SDL_Vertex> v;
    v.reserve(2 * n);
    for (int i = 0; i < n; i++) v.push_back({outer[i], c, {0.0f, 0.0f}});
    for (int i = 0; i < n; i++) v.push_back({inner[i], c, {0.0f, 0.0f}});
    std::vector<int> idx;
    idx.reserve(n * 6);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        idx.insert(idx.end(), {i, j, n + i, n + i, j, n + j});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

}  // namespace Gfx
