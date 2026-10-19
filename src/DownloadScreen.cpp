#include "DownloadScreen.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include "App.h"
#include "Gfx.h"
#include "Installer.h"
#include "Log.h"
#include "Net.h"
#include "Paths.h"
#include "Style.h"
#include "Util.h"

namespace {
std::string hms(double secs) {
    const long s = (long)(secs + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}
}

DownloadScreen::DownloadScreen(App& app, size_t theme, const InstallLoc& target, bool needCheck)
    : app_(app),
      theme_(theme),
      target_(target.dir),
      url_(app.gallery.themes[theme].download),
      folder_(app.gallery.themes[theme].folder),
      work_(target.dir + "/.tg_tmp_" + std::to_string((long long)std::time(nullptr))),
      zip_(work_ + "/" + folder_ + ".zip"),
      extractDir_(work_ + "/x"),
      dest_(target.path()),  // reinstall keeps the existing folder name
      patch_(app.gallery.themes[theme].imagesDir.empty()
                 ? std::string()
                 : Paths::appDir() + "patches/" + app.gallery.themes[theme].imagesDir + ".patch"),
      cfgSize_(app.gallery.themes[theme].size),
      needCheck_(needCheck),
      phase_(needCheck ? CHECKING : DOWNLOADING) {}

DownloadScreen::~DownloadScreen() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (ok_) app_.markThemesChanged();  // exited before finish(): the swap still happened
}

void DownloadScreen::enter() {
    worker_ = std::thread(&DownloadScreen::run, this);
}

void DownloadScreen::handle(Action a) {
    if (a == Action::Cancel && canCancel_) cancel_ = true;
}

void DownloadScreen::run() {
    auto cleanup = [this] {
        if (Paths::isDir(work_)) Paths::removeTree(work_);
    };
    auto stopCancelled = [&] {
        cleanup();
        Log::info("install", "cancelled");
        cancelled_ = true;
        done_ = true;
    };
    auto fail = [&](const std::string& msg) {
        Log::error("install", msg);
        error_ = msg;
        cleanup();
        done_ = true;
    };

    if (needCheck_) {
        const bool on = Net::checkInternet();
        app_.setOnline(on);
        if (!on) {
            offline_ = true;
            done_ = true;
            return;
        }
    }
    if (cancel_) return stopCancelled();

    phase_ = DOWNLOADING;
    if (cfgSize_ && Paths::freeBytes(target_) < cfgSize_ * 5 / 2)
        return fail("Not enough free space in " + target_ + ".");
    if (mkdir(work_.c_str(), 0755) != 0) return fail("Cannot create " + work_ + ": " + std::strerror(errno));

    std::string err;
    Log::info("install", "downloading " + url_ + " to " + target_);
    if (!Net::download(url_, zip_, cancel_, received_, total_, err)) {
        if (cancel_) return stopCancelled();
        return fail("Download failed: " + err);
    }
    if (cancel_) return stopCancelled();

    phase_ = INSTALLING;
    struct stat st;
    const uint64_t zipSize = stat(zip_.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0;
    if (Paths::freeBytes(target_) < zipSize * 3 / 2) return fail("Not enough free space to extract the theme.");
    if (mkdir(extractDir_.c_str(), 0755) != 0)
        return fail("Cannot create " + extractDir_ + ": " + std::strerror(errno));
    if (!Installer::extract(zip_, extractDir_, cancel_, err)) {
        if (cancel_) return stopCancelled();
        return fail("Extract failed: " + err);
    }
    std::remove(zip_.c_str());  // free the space before the swap

    std::string root;
    if (!Installer::findRoot(extractDir_, root, err)) return fail(err);
    if (cancel_) return stopCancelled();
    canCancel_ = false;  // from here the swap always completes

    if (!patch_.empty() && stat(patch_.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        if (Installer::applyPatch(root, patch_, err)) {
            Log::info("install", "patched with " + patch_);
        } else {
            Log::warn("install", "patch " + patch_ + " " + err);
            unpatched_ = true;
        }
    }

    std::string trash;
    if (!Installer::swapIn(root, dest_, trash, err)) return fail(err);
    Log::info("install", "installed " + dest_);
    if (!trash.empty() && !Paths::removeTree(trash)) Log::warn("install", "old copy not fully removed: " + trash);
    cleanup();
    ok_ = true;
    done_ = true;
}

void DownloadScreen::finish() {
    finished_ = true;
    if (worker_.joinable()) worker_.join();
    App& a = app_;
    a.popOverlay();
    const std::string& name = a.gallery.themes[theme_].name;
    if (offline_) {
        a.message("Offline", "Internet Required to Download - Browsing Only");
    } else if (cancelled_) {
        a.message("Download Cancelled", "Download has been cancelled. Partial files have been removed.");
    } else if (ok_) {
        a.refreshInstalled();
        a.refreshFreeSpace();
        a.markThemesChanged();
        a.message("Theme Installed", "\"" + name + "\" was installed to " + target_ + "." +
                  (unpatched_ ? " The theme patch could not be applied, so it was installed unpatched." : ""));
    } else {
        a.message("Install Failed", error_);
    }
}

void DownloadScreen::render(Uint32 now) {
    if (done_ && !finished_) finish();  // this frame still draws; the pop applies after render

    App& a = app_;
    const Layout& L = a.lay;
    const Theme& t = a.gallery.themes[theme_];
    const int phase = phase_;
    const int pad = L.px(16);
    const int pw = std::min(L.w - 2 * L.margin, L.px(470));
    const int pr = L.px(12);
    const std::vector<Hint> cancelHint = {{{Glyph::Select, Glyph::X}, "Cancel"}};

    if (phase != DOWNLOADING) {
        const int lh = TTF_FontLineSkip(a.fonts.body);
        const int lhS = TTF_FontLineSkip(a.fonts.small);
        const int spin = L.px(40);
        const bool showCancel = canCancel_;
        const int ph = pad + spin + L.px(12) + lh + (showCancel ? lhS + L.px(4) : 0) + pad;
        const SDL_Rect panel = a.panelRect(pw, ph);
        Gfx::fillRoundRect(a.ren, panel, pr, Style::PANEL);
        Gfx::strokeRoundRect(a.ren, panel, pr, 1, Style::PANEL_BORDER);
        Gfx::spinner(a.ren, panel.x + pw / 2, panel.y + pad + spin / 2, L.px(14), std::max(3, L.px(5)), now,
                     Style::ACCENT);
        const std::string msg = phase == CHECKING ? "Checking connection..." : "Installing \"" + t.name + "\"...";
        int y = panel.y + pad + spin + L.px(12);
        a.text->drawIn(a.fonts.body, a.text->fit(a.fonts.body, msg, pw - 2 * pad), SDL_Rect{panel.x, y, pw, lh},
                       Style::TEXT, Align::Center);
        if (showCancel) {
            y += lh + L.px(4);
            a.text->drawIn(a.fonts.small, "Press SELECT + X to cancel", SDL_Rect{panel.x, y, pw, lhS}, Style::DIM,
                           Align::Center);
        }
        a.drawFooter(showCancel ? cancelHint : std::vector<Hint>{});
        return;
    }

    // speed: sampled every 0.5 s, smoothed; ETA text refreshed once a second
    const uint64_t got = received_;
    if (sampleMs_ == 0) {
        sampleMs_ = now;
        sampleBytes_ = got;
    } else if (now - sampleMs_ >= 500) {
        const double inst = (double)(got >= sampleBytes_ ? got - sampleBytes_ : 0) * 1000.0 / (now - sampleMs_);
        speed_ = speed_ <= 0.0 ? inst : 0.3 * inst + 0.7 * speed_;
        sampleMs_ = now;
        sampleBytes_ = got;
    }
    const uint64_t total = total_ ? total_.load() : cfgSize_;
    if (now - etaMs_ >= 1000) {
        etaMs_ = now;
        if (total && got >= total) etaText_ = "00:00:00";
        else if (total && speed_ > 1.0) etaText_ = hms((double)(total - got) / speed_);
        else etaText_ = "--:--";
    }

    const int titleH = TTF_FontLineSkip(a.fonts.title);
    const int lhB = TTF_FontLineSkip(a.fonts.bodyBold);
    const int lhS = TTF_FontLineSkip(a.fonts.small);
    const int thumbH = L.px(72);
    const int barH = L.px(10);
    const int rightH = lhB + lhS + L.px(8) + barH + L.px(8) + lhS + lhS;
    const int bodyH = std::max(thumbH, rightH);
    const int infoH = L.px(40);
    const int ph = pad + titleH + L.px(10) + bodyH + L.px(14) + infoH + pad;
    const SDL_Rect panel = a.panelRect(pw, ph);
    Gfx::fillRoundRect(a.ren, panel, pr, Style::PANEL);
    Gfx::strokeRoundRect(a.ren, panel, pr, 1, Style::PANEL_BORDER);

    int y = panel.y + pad;
    a.text->draw(a.fonts.title, "Downloading Theme", panel.x + pad, y, Style::TEXT);
    y += titleH + L.px(4);
    SDL_SetRenderDrawColor(a.ren, Style::PANEL_BORDER.r, Style::PANEL_BORDER.g, Style::PANEL_BORDER.b, 255);
    SDL_RenderDrawLine(a.ren, panel.x + pad, y, panel.x + pw - pad, y);
    y += L.px(6);

    // thumbnail
    const CachedImage* th = a.images->thumb(theme_);
    const int thumbW = th ? std::min(thumbH * th->w / th->h, thumbH * 16 / 9) : thumbH * 4 / 3;
    const SDL_Rect tr{panel.x + pad, y + (bodyH - thumbH) / 2, thumbW, thumbH};
    if (th) Gfx::drawRoundImage(a.ren, th->tex, tr, L.px(6), SDL_Color{255, 255, 255, 255});
    else Gfx::fillRoundRect(a.ren, tr, L.px(6), Style::BG);

    // right column: name, author, bar, sizes + speed, ETA
    const int rx = tr.x + thumbW + L.px(14);
    const int rw = panel.x + pw - pad - rx;
    int ry = y + (bodyH - rightH) / 2;
    a.text->draw(a.fonts.bodyBold, a.text->fit(a.fonts.bodyBold, t.name, rw), rx, ry, Style::TEXT);
    ry += lhB;
    const int lw = a.text->draw(a.fonts.small, "Author: ", rx, ry, Style::DIM);
    a.text->draw(a.fonts.small, a.text->fit(a.fonts.small, t.author, rw - lw), rx + lw, ry, Style::ACCENT);
    ry += lhS + L.px(8);

    const int pctW = a.text->width(a.fonts.smallBold, "100%");
    const SDL_Rect bar{rx, ry, rw - pctW - L.px(8), barH};
    Gfx::fillRoundRect(a.ren, bar, barH / 2, Style::BG);
    Gfx::strokeRoundRect(a.ren, bar, barH / 2, 1, Style::PANEL_BORDER);
    if (total) {
        const uint64_t shown = std::min(got, total);
        const int fw = (int)((double)bar.w * shown / total);
        if (fw > 0) Gfx::fillRoundRect(a.ren, SDL_Rect{bar.x, bar.y, std::max(fw, barH), barH}, barH / 2, Style::ACCENT);
        const std::string pct = std::to_string((int)(shown * 100 / total)) + "%";
        a.text->draw(a.fonts.smallBold, pct, bar.x + bar.w + L.px(8) + pctW,
                     bar.y + (barH - TTF_FontHeight(a.fonts.smallBold)) / 2, Style::TEXT, Align::Right);
    } else {
        // size unknown: a segment sweeps across the bar
        const int segW = bar.w / 3;
        const int sx = bar.x - segW + (int)((now % 1400) * (bar.w + segW) / 1400);
        SDL_RenderSetClipRect(a.ren, &bar);
        Gfx::fillRoundRect(a.ren, SDL_Rect{sx, bar.y, segW, barH}, barH / 2, Style::ACCENT);
        SDL_RenderSetClipRect(a.ren, nullptr);
    }
    ry += barH + L.px(8);

    const std::string sizes =
        total ? Util::formatBytes(got) + " / " + Util::formatBytes(total) : Util::formatBytes(got) + " downloaded";
    a.text->draw(a.fonts.small, sizes, rx, ry, Style::TEXT);
    if (speed_ > 0.0)
        a.text->draw(a.fonts.small, Util::formatBytes((uint64_t)speed_) + "/s", rx + rw, ry, Style::DIM, Align::Right);
    ry += lhS;
    const int ew = a.text->draw(a.fonts.small, "Estimated time remaining: ", rx, ry, Style::DIM);
    a.text->draw(a.fonts.small, etaText_, rx + ew, ry, Style::TEXT);
    y += bodyH + L.px(14);

    // info box
    const SDL_Rect ib{panel.x + pad, y, pw - 2 * pad, infoH};
    Gfx::fillRoundRect(a.ren, ib, L.px(8), Style::BG);
    Gfx::strokeRoundRect(a.ren, ib, L.px(8), 1, Style::PANEL_BORDER);
    const int ic = L.px(20);
    const SDL_Rect icr{ib.x + L.px(10), ib.y + (infoH - ic) / 2, ic, ic};
    Gfx::fillRoundRect(a.ren, icr, ic / 2, Style::ACCENT);
    a.text->drawIn(a.fonts.smallBold, "i", icr, Style::TEXT, Align::Center);
    const int tx = icr.x + ic + L.px(10);
    const std::string note = "Press SELECT + X to cancel download and clean up the files.";
    a.text->drawIn(a.fonts.small, a.text->fit(a.fonts.small, note, ib.x + ib.w - L.px(10) - tx),
                   SDL_Rect{tx, ib.y, ib.w, infoH}, Style::DIM, Align::Left);

    a.drawFooter(cancelHint);
}
