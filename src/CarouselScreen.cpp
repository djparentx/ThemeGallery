#include "CarouselScreen.h"
#include <algorithm>
#include <cmath>
#include "App.h"
#include "Gfx.h"
#include "Style.h"
#include "Util.h"

namespace {
constexpr Uint32 H_SLIDE_MS = 150;
constexpr Uint32 V_SLIDE_MS = 200;
}

void CarouselScreen::enter() {
    focus_ = 0;
    hFrom_ = 0.0f;
    vDir_ = 0.0f;
    requestImages();
}

void CarouselScreen::requestImages() {
    if (app_.visibleCount() == 0) return;
    app_.images->want({app_.current, app_.neighbor(1), app_.neighbor(-1)}, {});
}

float CarouselScreen::hOffset(Uint32 now) {
    if (hFrom_ == 0.0f) return 0.0f;
    const Uint32 el = now - hStart_;
    if (el >= H_SLIDE_MS) {
        hFrom_ = 0.0f;
        return 0.0f;
    }
    return hFrom_ * (1.0f - Util::easeOut((float)el / H_SLIDE_MS));
}

void CarouselScreen::handle(Action a) {
    if (a == Action::Start) {
        app_.openMenu();
        return;
    }
    if (a == Action::Back) {
        app_.confirmExit();
        return;
    }
    const size_t n = app_.visibleCount();
    if (n == 0) return;
    const Uint32 now = SDL_GetTicks();
    switch (a) {
    case Action::Up:
    case Action::Down:
    case Action::PageUp:
    case Action::PageDown: {
        if (n < 2) break;
        const bool down = a == Action::Down || a == Action::PageDown;
        const int dist = (a == Action::PageUp || a == Action::PageDown) ? 10 : 1;
        prevTheme_ = app_.current;
        prevFocus_ = focus_;
        app_.step(down ? dist : -dist);
        vDir_ = down ? 1.0f : -1.0f;
        vStart_ = now;
        focus_ = 0;
        hFrom_ = 0.0f;
        requestImages();
        break;
    }
    case Action::Left:
    case Action::Right: {
        const auto* imgs = app_.images->images(app_.current);
        if (!imgs || imgs->size() < 2) break;
        const float dir = (a == Action::Right) ? 1.0f : -1.0f;
        const float off = hOffset(now);
        focus_ = Util::wrapIndex(focus_ + (int)dir, (int)imgs->size());
        hFrom_ = off + dir;  // new focus starts where it was drawn and slides to center
        hStart_ = now;
        break;
    }
    case Action::Ok:
        app_.openThemeActions();
        break;
    case Action::Y:
        app_.toggleFavorite(app_.current);
        break;
    default:
        break;
    }
}

void CarouselScreen::render(Uint32 now) {
    App& a = app_;
    const Layout& L = a.lay;
    const size_t n = a.visibleCount();
    a.drawHeader(n ? std::to_string(a.visiblePos() + 1) + "/" + std::to_string(n) : "");

    if (n == 0) {
        a.text->drawIn(a.fonts.body, "No themes in gallery.ini", L.preview, Style::ERR, Align::Center);
    } else {
        const int top = L.header.y + L.header.h;
        const SDL_Rect clip{0, top, L.w, L.footer.y - top};
        float t = 1.0f;
        if (vDir_ != 0.0f) {
            const Uint32 el = now - vStart_;
            if (el >= V_SLIDE_MS) vDir_ = 0.0f;
            else t = Util::easeOut((float)el / V_SLIDE_MS);
        }
        SDL_RenderSetClipRect(a.ren, &clip);
        if (vDir_ != 0.0f) {
            const int dyNew = (int)std::lround(vDir_ * clip.h * (1.0f - t));
            const int dyOld = dyNew - (int)std::lround(vDir_ * clip.h);
            drawTheme(prevTheme_, prevFocus_, 0.0f, dyOld);
            drawTheme(a.current, focus_, 0.0f, dyNew);
        } else {
            drawTheme(a.current, focus_, hOffset(now), 0);
        }
        SDL_RenderSetClipRect(a.ren, nullptr);
    }

    a.drawFooter({{{Glyph::Up, Glyph::Down}, "Browse"},
                  {{Glyph::L1, Glyph::R1}, "Page"},
                  {{Glyph::Left, Glyph::Right}, "Images"},
                  {{a.okGlyph()}, "Select"},
                  {{a.backGlyph()}, "Exit"},
                  {{Glyph::Y}, "Fav"},
                  {{Glyph::Start}, "Menu"}});
}

void CarouselScreen::drawTheme(size_t theme, int focus, float hOff, int dy) {
    App& a = app_;
    const Layout& L = a.lay;
    const Theme& t = a.gallery.themes[theme];
    const int radius = L.px(10);

    SDL_Rect band = L.preview;
    band.y += dy;
    const std::vector<CachedImage>* imgs = a.images->images(theme);

    if (!imgs || imgs->empty()) {
        const int pw = std::min(L.w - 2 * L.margin, band.h * 4 / 3);
        const SDL_Rect box{(L.w - pw) / 2, band.y, pw, band.h};
        Gfx::fillRoundRect(a.ren, box, radius, Style::PANEL);
        Gfx::strokeRoundRect(a.ren, box, radius, 1, Style::PANEL_BORDER);
        a.text->drawIn(a.fonts.body, imgs ? "No preview" : "Loading...", box, Style::DIM, Align::Center);
        drawBadges(theme, box);
    } else {
        const int count = (int)imgs->size();
        const int maxW = L.w - 2 * L.margin;
        auto sizeOf = [&](const CachedImage& im) {
            const int iw = std::min(maxW, band.h * im.w / im.h);
            return SDL_Point{iw, iw * im.h / im.w};
        };
        int slot = 0;
        for (const auto& im : *imgs) slot = std::max(slot, sizeOf(im).x);
        const int step = slot + L.gap;
        const int cx = L.w / 2, cy = band.y + band.h / 2;
        const int span = count > 1 ? 2 : 0;
        const int fIdx = Util::wrapIndex(focus, count);
        SDL_Rect focusRect{0, 0, 0, 0};

        // pass 0: side images, pass 1: focused image on top
        for (int pass = 0; pass < 2; pass++) {
            for (int r = -span; r <= span; r++) {
                if ((pass == 1) != (r == 0)) continue;
                const int idx = Util::wrapIndex(fIdx + r, count);
                if (r != 0 && idx == fIdx) continue;  // never repeat the focused image beside itself
                const CachedImage& im = (*imgs)[idx];
                const float pos = r + hOff;
                const SDL_Point sz = sizeOf(im);
                const SDL_Rect dst{cx + (int)std::lround(pos * step) - sz.x / 2, cy - sz.y / 2, sz.x, sz.y};
                if (dst.x + dst.w < 0 || dst.x > L.w) continue;
                const float d = std::min(1.0f, std::fabs(pos));
                const Uint8 bright = (Uint8)(255 - 115 * d);
                const Uint8 alpha = (Uint8)(255 - 55 * d);
                Gfx::drawRoundImage(a.ren, im.tex, dst, radius, SDL_Color{bright, bright, bright, alpha});
                if (r == 0) focusRect = dst;
            }
        }
        Gfx::strokeRoundRect(a.ren, focusRect, radius, std::max(2, L.px(2)), Style::ACCENT);
        drawBadges(theme, focusRect);
    }

    SDL_Rect title = L.title;
    title.y += dy;
    a.text->drawIn(a.fonts.title, a.text->fit(a.fonts.title, t.name, title.w), title, Style::TEXT, Align::Center);

    SDL_Rect info = L.info;
    info.y += dy;
    const int pr = L.px(8);
    Gfx::fillRoundRect(a.ren, info, pr, Style::PANEL);
    Gfx::strokeRoundRect(a.ren, info, pr, 1, Style::PANEL_BORDER);

    const int pad = L.px(10);
    const int colW = info.w / 2 - 2 * pad;
    const int lh = TTF_FontLineSkip(a.fonts.body);
    const int row0 = info.y + (info.h - 2 * lh) / 2;
    const int midX = info.x + info.w / 2;
    const std::string size = t.size ? Util::formatBytes(t.size) : "unknown";
    std::string location;
    if (a.isInstalled(theme)) {
        for (const auto& d : a.installedIn(theme)) location += (location.empty() ? "" : ", ") + d;
    } else {
        location = a.themesDir + " (" + Util::formatBytes(a.freeSpace) + " free)";
    }
    field(info.x + pad, row0, colW, "Author:", t.author, Style::ACCENT);
    field(info.x + pad, row0 + lh, colW, "Folder:", t.folder, Style::TEXT);
    field(midX + pad, row0, colW, "Size:", size, Style::TEXT);
    field(midX + pad, row0 + lh, colW, "Location:", location, Style::TEXT);

    SDL_SetRenderDrawColor(a.ren, Style::PANEL_BORDER.r, Style::PANEL_BORDER.g, Style::PANEL_BORDER.b, 255);
    SDL_RenderDrawLine(a.ren, midX, info.y + pad / 2, midX, info.y + info.h - pad / 2);
}

void CarouselScreen::drawBadges(size_t theme, const SDL_Rect& r) {
    App& a = app_;
    const int bs = a.lay.px(22), inset = a.lay.px(8), gap = a.lay.px(4);
    int x = r.x + inset;
    const int y = r.y + inset;
    if (a.isInstalled(theme)) {
        const SDL_Rect d{x, y, bs, bs};
        SDL_RenderCopy(a.ren, a.icons->installed(false), nullptr, &d);
        x += bs + gap;
    }
    if (a.isFavorite(theme)) {
        const SDL_Rect d{x, y, bs, bs};
        SDL_RenderCopy(a.ren, a.icons->heart(false), nullptr, &d);
        x += bs + gap;
    }
    if (a.isCurrentTheme(theme)) {
        const SDL_Rect d{x, y, bs, bs};
        SDL_RenderCopy(a.ren, a.icons->star(false), nullptr, &d);
        x += bs + gap;
    }
    if (a.gallery.themes[theme].category == Category::Batocera) a.drawCategoryBadge(SDL_Rect{x, y, bs, bs}, false);
}

void CarouselScreen::field(int x, int y, int maxW, const std::string& label, const std::string& value,
                           SDL_Color valueColor) {
    App& a = app_;
    const int lw = a.text->draw(a.fonts.body, label, x, y, Style::DIM);
    const int gap = a.lay.px(5);
    a.text->draw(a.fonts.body, a.text->fit(a.fonts.body, value, maxW - lw - gap), x + lw + gap, y, valueColor);
}
