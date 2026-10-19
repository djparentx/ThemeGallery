#include "RemovingScreen.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "App.h"
#include "Gfx.h"
#include "Log.h"
#include "Paths.h"
#include "Style.h"

namespace {
constexpr Uint32 MIN_SHOW_MS = 500;  // avoid a one-frame flash on fast deletes
}

RemovingScreen::~RemovingScreen() {
    if (worker_.joinable()) worker_.join();
}

void RemovingScreen::enter() {
    startMs_ = SDL_GetTicks();
    const InstallLoc loc = app_.removalTarget(theme_);  // oldest copy if there are two
    const std::string src = loc.path();
    trash_ = loc.dir + "/.tg_old_" + loc.name + "_" + std::to_string((long long)std::time(nullptr));
    if (std::rename(src.c_str(), trash_.c_str()) != 0) {
        error_ = std::strerror(errno);
        Log::error("remove", src + ": " + error_);
        ok_ = false;
        done_ = true;
        return;
    }
    app_.markThemesChanged();  // the theme is gone from ES's view now, even if we exit mid-delete
    Log::info("remove", "uninstalling " + src);
    worker_ = std::thread([this] {
        ok_ = Paths::removeTree(trash_);
        done_ = true;
    });
}

void RemovingScreen::render(Uint32 now) {
    App& a = app_;
    const Layout& L = a.lay;

    if (done_ && !finished_ && now - startMs_ >= MIN_SHOW_MS) {
        finished_ = true;
        if (worker_.joinable()) worker_.join();
        a.popOverlay();
        if (!error_.empty()) {
            a.message("Uninstall Failed", "Could not remove the theme: " + error_);
        } else {
            a.refreshInstalled();
            a.refreshFreeSpace();
            Log::info("remove", ok_ ? "done" : "finished with errors");
            if (!ok_) a.message("Uninstall Incomplete", "Some files could not be deleted. See ThemeGallery.log.");
        }
    }

    const int pad = L.px(16);
    const int pw = std::min(L.w - 2 * L.margin, L.px(320));
    const int lh = TTF_FontLineSkip(a.fonts.body);
    const int spin = L.px(40);
    const int ph = pad + spin + L.px(12) + lh + pad;
    const SDL_Rect panel = a.panelRect(pw, ph);
    const int pr = L.px(12);
    Gfx::fillRoundRect(a.ren, panel, pr, Style::PANEL);
    Gfx::strokeRoundRect(a.ren, panel, pr, 1, Style::PANEL_BORDER);

    Gfx::spinner(a.ren, panel.x + pw / 2, panel.y + pad + spin / 2, L.px(14), std::max(3, L.px(5)), now, Style::ACCENT);

    const std::string msg = "Removing \"" + a.gallery.themes[theme_].name + "\"...";
    a.text->drawIn(a.fonts.body, a.text->fit(a.fonts.body, msg, pw - 2 * pad),
                   SDL_Rect{panel.x, panel.y + pad + spin + L.px(12), pw, lh}, Style::TEXT, Align::Center);
}
