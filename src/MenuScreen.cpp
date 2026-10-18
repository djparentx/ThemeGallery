#include "MenuScreen.h"
#include <algorithm>
#include "App.h"
#include "Gfx.h"
#include "Style.h"
#include "Util.h"

namespace {
constexpr int ROWS = 3;
}

void MenuScreen::change(int row) {
    if (row == 0)
        app_.requestView(app_.state.view == ViewMode::Carousel ? ViewMode::List : ViewMode::Carousel);
    else
        app_.setFilter(!app_.favoritesOnly());  // Favorites with none saved is ignored
}

void MenuScreen::handle(Action a) {
    switch (a) {
    case Action::Up:
        sel_ = Util::wrapIndex(sel_ - 1, ROWS);
        break;
    case Action::Down:
        sel_ = Util::wrapIndex(sel_ + 1, ROWS);
        break;
    case Action::Left:
    case Action::Right:
        if (sel_ < 2) change(sel_);
        break;
    case Action::Ok:
        if (sel_ < 2) change(sel_);
        else app_.requestExit();
        break;
    case Action::Back:
    case Action::Start:
        app_.popOverlay();
        break;
    default:
        break;
    }
}

void MenuScreen::render(Uint32) {
    App& a = app_;
    const Layout& L = a.lay;
    const int pad = L.px(14), rowH = L.px(38), gap = L.px(6);
    const int pw = std::min(L.w - 2 * L.margin, L.px(360));
    const int titleH = TTF_FontLineSkip(a.fonts.title);
    const int ph = pad + titleH + L.px(8) + ROWS * rowH + (ROWS - 1) * gap + pad;
    const SDL_Rect panel = a.panelRect(pw, ph);
    const int pr = L.px(12), br = L.px(8);

    Gfx::fillRoundRect(a.ren, panel, pr, Style::PANEL);
    Gfx::strokeRoundRect(a.ren, panel, pr, 1, Style::PANEL_BORDER);

    int y = panel.y + pad;
    a.text->drawIn(a.fonts.title, "Menu", SDL_Rect{panel.x, y, pw, titleH}, Style::TEXT, Align::Center);
    y += titleH + L.px(8);

    const char* labels[ROWS] = {"View Mode", "Filter", "Exit"};
    const std::string values[ROWS] = {a.state.view == ViewMode::Carousel ? "Carousel" : "List",
                                      a.favoritesOnly() ? "Favorites" : "All", ""};
    // fixed value column so the arrows don't jump when the value changes
    const int colW = std::max({a.text->width(a.fonts.body, "Carousel"), a.text->width(a.fonts.body, "List"),
                               a.text->width(a.fonts.body, "Favorites"), a.text->width(a.fonts.body, "All")});
    const int as = L.px(18) - 2 * L.px(2);  // arrow icon size

    for (int i = 0; i < ROWS; i++) {
        const SDL_Rect r{panel.x + pad, y + i * (rowH + gap), pw - 2 * pad, rowH};
        if (i == sel_) {
            Gfx::fillRoundRect(a.ren, r, br, Style::HIGHLIGHT);
            Gfx::strokeRoundRect(a.ren, r, br, std::max(1, L.px(1.5f)), Style::ACCENT);
        }
        a.text->drawIn(a.fonts.body, labels[i], SDL_Rect{r.x + L.px(12), r.y, r.w, r.h}, Style::TEXT, Align::Left);
        if (values[i].empty()) continue;

        const int rightX = r.x + r.w - L.px(10) - as;
        const int colX = rightX - L.px(6) - colW;
        const int leftX = colX - L.px(6) - as;
        const int ay = r.y + (r.h - as) / 2;
        const SDL_Rect la{leftX, ay, as, as}, ra{rightX, ay, as, as};
        SDL_RenderCopy(a.ren, a.icons->arrow(Glyph::Left), nullptr, &la);
        SDL_RenderCopy(a.ren, a.icons->arrow(Glyph::Right), nullptr, &ra);
        a.text->drawIn(a.fonts.body, values[i], SDL_Rect{colX, r.y, colW, r.h}, i == sel_ ? Style::ACCENT : Style::DIM,
                       Align::Center);
    }

    a.drawFooter({{{Glyph::Up, Glyph::Down}, "Move"},
                  {{Glyph::Left, Glyph::Right}, "Change"},
                  {{a.okGlyph()}, "Select"},
                  {{a.backGlyph()}, "Close"}});
}
