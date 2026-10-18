#include "DialogScreen.h"
#include <algorithm>
#include "App.h"
#include "Gfx.h"
#include "Style.h"
#include "Util.h"

void DialogScreen::handle(Action a) {
    const int n = (int)spec_.buttons.size();
    if (n == 0) {
        if (a == Action::Ok || a == Action::Back) app_.popOverlay();
        return;
    }
    const bool row = spec_.style == ButtonStyle::Row;
    auto move = [&](int dir) {
        for (int k = 1; k <= n; k++) {
            const int j = Util::wrapIndex(sel_ + dir * k, n);
            if (!spec_.buttons[j].disabled) {
                sel_ = j;
                return;
            }
        }
    };
    if ((row && a == Action::Left) || (!row && a == Action::Up)) {
        move(-1);
    } else if ((row && a == Action::Right) || (!row && a == Action::Down)) {
        move(1);
    } else if (a == Action::Ok) {
        if (spec_.buttons[sel_].disabled) return;
        std::function<void()> action = spec_.buttons[sel_].action;
        app_.popOverlay();  // applied after this action, so the next dialog stacks on the right parent
        if (action) action();
    } else if (a == Action::Back) {
        app_.popOverlay();
    }
}

void DialogScreen::render(Uint32) {
    App& a = app_;
    const Layout& L = a.lay;
    const int pad = L.px(16);
    const int pw = std::min(L.w - 2 * L.margin, L.px(400));

    const int thumbH = spec_.showThumb ? L.px(64) : 0;
    const CachedImage* thumb = spec_.showThumb ? a.images->thumb(spec_.theme) : nullptr;
    int thumbW = 0;
    if (spec_.showThumb) thumbW = thumb ? std::min(thumbH * thumb->w / thumb->h, thumbH * 16 / 9) : thumbH * 4 / 3;

    const int textX0 = pad + (thumbW ? thumbW + L.px(12) : 0);
    const int textW = pw - textX0 - pad;
    const std::vector<std::string> lines = a.text->wrap(a.fonts.body, spec_.message, textW);
    const int lh = TTF_FontLineSkip(a.fonts.body);
    const int titleH = TTF_FontLineSkip(a.fonts.title);
    const int bodyH = std::max(thumbH, (int)lines.size() * lh);
    const int nb = (int)spec_.buttons.size();
    const int btnH = L.px(34), btnGap = L.px(8);
    const int buttonsH = nb == 0 ? 0 : (spec_.style == ButtonStyle::Row ? btnH : nb * btnH + (nb - 1) * btnGap);
    const int ph = pad + titleH + L.px(10) + bodyH + L.px(16) + buttonsH + pad;
    const SDL_Rect panel = a.panelRect(pw, ph);
    const int pr = L.px(12);

    Gfx::fillRoundRect(a.ren, panel, pr, Style::PANEL);
    Gfx::strokeRoundRect(a.ren, panel, pr, 1, Style::PANEL_BORDER);

    int y = panel.y + pad;
    a.text->drawIn(a.fonts.title, a.text->fit(a.fonts.title, spec_.title, pw - 2 * pad), SDL_Rect{panel.x, y, pw, titleH},
                   Style::TEXT, Align::Center);
    y += titleH + L.px(10);

    if (spec_.showThumb) {
        const SDL_Rect tr{panel.x + pad, y + (bodyH - thumbH) / 2, thumbW, thumbH};
        if (thumb) Gfx::drawRoundImage(a.ren, thumb->tex, tr, L.px(6), SDL_Color{255, 255, 255, 255});
        else Gfx::fillRoundRect(a.ren, tr, L.px(6), Style::BG);
    }
    int ty = y + (bodyH - (int)lines.size() * lh) / 2;
    for (const auto& l : lines) {
        if (spec_.showThumb) a.text->draw(a.fonts.body, l, panel.x + textX0, ty, Style::TEXT);
        else a.text->draw(a.fonts.body, l, panel.x + pw / 2, ty, Style::TEXT, Align::Center);
        ty += lh;
    }
    y += bodyH + L.px(16);

    const int br = L.px(8);
    for (int i = 0; i < nb; i++) {
        const bool sel = i == sel_;
        const std::string& label = spec_.buttons[i].label;
        if (spec_.style == ButtonStyle::Row) {
            const int bw = (pw - 2 * pad - (nb - 1) * btnGap) / nb;
            const SDL_Rect b{panel.x + pad + i * (bw + btnGap), y, bw, btnH};
            if (sel) {
                Gfx::fillRoundRect(a.ren, b, br, Style::ACCENT);
            } else {
                Gfx::fillRoundRect(a.ren, b, br, Style::BG);
                Gfx::strokeRoundRect(a.ren, b, br, 1, Style::PANEL_BORDER);
            }
            a.text->drawIn(a.fonts.bodyBold, label, b, sel ? Style::TEXT : Style::DIM, Align::Center);
        } else {
            const SDL_Rect b{panel.x + pad, y + i * (btnH + btnGap), pw - 2 * pad, btnH};
            if (sel) {
                Gfx::fillRoundRect(a.ren, b, br, Style::HIGHLIGHT);
                Gfx::strokeRoundRect(a.ren, b, br, std::max(1, L.px(1.5f)), Style::ACCENT);
            }
            a.text->drawIn(a.fonts.body, label, SDL_Rect{b.x + L.px(12), b.y, b.w - L.px(12), b.h},
                           spec_.buttons[i].disabled ? Style::DIM : Style::TEXT, Align::Left);
        }
    }

    a.drawFooter({{{a.okGlyph()}, "Select"}, {{a.backGlyph()}, "Back"}});
}
