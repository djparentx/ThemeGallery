#include "ListScreen.h"
#include <algorithm>
#include "App.h"
#include "Gfx.h"
#include "Style.h"

SDL_Rect ListScreen::area() const {
    const Layout& L = app_.lay;
    const int y = L.header.y + L.header.h + L.px(6);
    return {L.margin, y, L.w - 2 * L.margin, L.footer.y - L.px(6) - y};
}

int ListScreen::rows() const {
    return std::max(1, area().h / app_.lay.listRowH);
}

void ListScreen::enter() {
    keepVisible();
    requestThumbs();
}

void ListScreen::keepVisible() {
    const size_t n = app_.visibleCount();
    const size_t r = (size_t)rows();
    if (n <= r) {
        top_ = 0;
        return;
    }
    const size_t pos = app_.visiblePos();
    if (pos < top_) top_ = pos;
    else if (pos >= top_ + r) top_ = pos - r + 1;
    top_ = std::min(top_, n - r);
}

void ListScreen::requestThumbs() {
    const size_t n = app_.visibleCount();
    if (n == 0) return;
    const size_t r = (size_t)rows();
    const size_t visEnd = std::min(n, top_ + r);
    const size_t from = top_ > r ? top_ - r : 0;
    const size_t to = std::min(n, top_ + 2 * r);
    std::vector<size_t> thumbs;
    for (size_t i = top_; i < visEnd; i++) thumbs.push_back(app_.visibleAt(i));  // visible rows first
    for (size_t i = from; i < to; i++)
        if (i < top_ || i >= visEnd) thumbs.push_back(app_.visibleAt(i));  // one page either side
    app_.images->want({app_.current}, thumbs);  // keep current full so the carousel opens instantly
}

void ListScreen::handle(Action a) {
    if (a == Action::Start) {
        app_.openMenu();
        return;
    }
    if (app_.visibleCount() == 0) return;
    switch (a) {
    case Action::Down:
        app_.step(1);
        break;
    case Action::Up:
        app_.step(-1);
        break;
    case Action::Ok:
        app_.openThemeActions();
        return;
    case Action::Y:
        app_.toggleFavorite(app_.current);
        return;
    default:
        return;
    }
    keepVisible();
    requestThumbs();
}

void ListScreen::render(Uint32) {
    App& a = app_;
    const Layout& L = a.lay;
    const size_t n = a.visibleCount();
    a.drawHeader(n ? std::to_string(a.visiblePos() + 1) + "/" + std::to_string(n) : "");

    const std::vector<Hint> hints = {{{Glyph::Up, Glyph::Down}, "Browse"},
                                     {{a.okGlyph()}, "Select"},
                                     {{a.backGlyph()}, "Back"},
                                     {{Glyph::Y}, "Fav"},
                                     {{Glyph::Start}, "Menu"}};
    if (n == 0) {
        a.text->drawIn(a.fonts.body, "No themes in gallery.ini", area(), Style::ERR, Align::Center);
        a.drawFooter(hints);
        return;
    }

    const SDL_Rect ar = area();
    const int rh = L.listRowH;
    const size_t nRows = (size_t)rows();
    const bool scroll = n > nRows;
    const int sbW = scroll ? L.px(4) : 0;
    const int rowW = ar.w - (scroll ? sbW + L.px(6) : 0);
    const int radius = L.px(8);

    for (size_t i = 0; i < nRows && top_ + i < n; i++) {
        const size_t idx = a.visibleAt(top_ + i);
        const Theme& t = a.gallery.themes[idx];
        const SDL_Rect row{ar.x, ar.y + (int)i * rh, rowW, rh - L.px(4)};
        if (idx == a.current) {
            Gfx::fillRoundRect(a.ren, row, radius, Style::HIGHLIGHT);
            Gfx::strokeRoundRect(a.ren, row, radius, std::max(1, L.px(1.5f)), Style::ACCENT);
        }

        // thumbnail, 4:3 slot
        const int th = L.thumbH;
        const int tw = th * 4 / 3;
        SDL_Rect tbox{row.x + L.px(6), row.y + (row.h - th) / 2, tw, th};
        if (const CachedImage* ci = a.images->thumb(idx)) {
            const int iw = std::min(tw, th * ci->w / ci->h);
            const int ih = iw * ci->h / ci->w;
            tbox = {tbox.x + (tw - iw) / 2, tbox.y + (th - ih) / 2, iw, ih};
            Gfx::drawRoundImage(a.ren, ci->tex, tbox, L.px(4), SDL_Color{255, 255, 255, 255});
        } else {
            Gfx::fillRoundRect(a.ren, tbox, L.px(4), Style::PANEL);
        }

        // tiny markers on the thumbnail
        const int ms = L.px(12), mi = L.px(2);
        int mx = tbox.x + mi;
        if (a.isInstalled(idx)) {
            const SDL_Rect d{mx, tbox.y + mi, ms, ms};
            SDL_RenderCopy(a.ren, a.icons->installed(true), nullptr, &d);
            mx += ms + mi;
        }
        if (a.isFavorite(idx)) {
            const SDL_Rect d{mx, tbox.y + mi, ms, ms};
            SDL_RenderCopy(a.ren, a.icons->heart(true), nullptr, &d);
        }

        // name + author
        const int tx = row.x + L.px(6) + tw + L.px(10);
        const int textW = row.x + row.w - L.px(10) - tx;
        const int nameH = TTF_FontLineSkip(a.fonts.bodyBold);
        const int authH = TTF_FontLineSkip(a.fonts.small);
        const int ty = row.y + (row.h - nameH - authH) / 2;
        a.text->draw(a.fonts.bodyBold, a.text->fit(a.fonts.bodyBold, t.name, textW), tx, ty, Style::TEXT);
        a.text->draw(a.fonts.small, a.text->fit(a.fonts.small, t.author, textW), tx, ty + nameH, Style::ACCENT);
    }

    if (scroll) {
        const SDL_Rect track{ar.x + ar.w - sbW, ar.y, sbW, (int)nRows * rh - L.px(4)};
        Gfx::fillRoundRect(a.ren, track, sbW / 2, Style::PANEL);
        const int thumbH = std::max(L.px(16), track.h * (int)nRows / (int)n);
        const int maxTop = (int)(n - nRows);
        const int ty = track.y + (track.h - thumbH) * (int)top_ / maxTop;
        Gfx::fillRoundRect(a.ren, SDL_Rect{track.x, ty, sbW, thumbH}, sbW / 2, Style::ACCENT);
    }

    a.drawFooter(hints);
}
