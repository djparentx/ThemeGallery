#include "ImageCache.h"
#include "Log.h"
#include <SDL_image.h>
#include <algorithm>

namespace {
// New ARGB8888 copy scaled down to targetH (never upscaled); nullptr on failure
SDL_Surface* scaledCopy(SDL_Surface* src, int targetH) {
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!conv || targetH <= 0 || conv->h <= targetH) return conv;
    const int tw = std::max(1, conv->w * targetH / conv->h);
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, tw, targetH, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return conv;
    if (SDL_SoftStretchLinear(conv, nullptr, dst, nullptr) != 0) {
        SDL_FreeSurface(dst);
        return conv;
    }
    SDL_FreeSurface(conv);
    return dst;
}

void freeResult(std::vector<SDL_Surface*>& images, SDL_Surface*& thumb) {
    for (SDL_Surface* s : images)
        if (s) SDL_FreeSurface(s);
    images.clear();
    if (thumb) SDL_FreeSurface(thumb);
    thumb = nullptr;
}
}  // namespace

ImageCache::ImageCache(SDL_Renderer* ren, const Gallery& gallery, int bandH, int thumbH)
    : ren_(ren), gallery_(gallery), bandH_(bandH), thumbH_(thumbH) {
    worker_ = std::thread(&ImageCache::run, this);
}

ImageCache::~ImageCache() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        quit_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    for (auto& r : done_) freeResult(r.images, r.thumb);
    for (auto& kv : entries_) {
        freeImages(kv.second);
        freeThumb(kv.second);
    }
}

void ImageCache::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            job = jobs_.front();
            jobs_.pop_front();
        }
        const std::vector<std::string>& paths = gallery_.themes[job.theme].images;
        Result r{job.theme, job.full, {}, nullptr};
        for (size_t i = 0; i < paths.size() && (job.full || i == 0) && !quit_; i++) {
            SDL_Surface* src = IMG_Load(paths[i].c_str());
            if (!src) {
                Log::warn("image", paths[i] + ": " + IMG_GetError());
                continue;
            }
            SDL_Surface* band = job.full ? scaledCopy(src, bandH_) : nullptr;
            if (i == 0) r.thumb = scaledCopy(band ? band : src, thumbH_);
            if (job.full) r.images.push_back(band);
            SDL_FreeSurface(src);
        }
        std::lock_guard<std::mutex> lk(mtx_);
        done_.push_back(std::move(r));
    }
}

void ImageCache::want(const std::vector<size_t>& full, const std::vector<size_t>& thumbs) {
    wantFull_ = std::set<size_t>(full.begin(), full.end());
    wantThumb_ = std::set<size_t>(thumbs.begin(), thumbs.end());

    std::lock_guard<std::mutex> lk(mtx_);

    // drop queued jobs that are no longer wanted
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const bool keep = it->full ? wantFull_.count(it->theme) != 0 : wantThumb_.count(it->theme) != 0;
        if (keep) {
            ++it;
            continue;
        }
        auto e = entries_.find(it->theme);
        if (e != entries_.end()) (it->full ? e->second.fullQueued : e->second.thumbQueued) = false;
        it = jobs_.erase(it);
    }

    // free textures no longer wanted; drop empty entries with nothing in flight
    for (auto it = entries_.begin(); it != entries_.end();) {
        Entry& e = it->second;
        const bool f = wantFull_.count(it->first) != 0;
        const bool t = f || wantThumb_.count(it->first) != 0;
        if (!f) freeImages(e);
        if (!t) freeThumb(e);
        if (!f && !t && !e.fullQueued && !e.thumbQueued) it = entries_.erase(it);
        else ++it;
    }

    for (size_t i : full) {
        Entry& e = entries_[i];
        if (!e.fullReady && !e.fullQueued) {
            jobs_.push_back({i, true});
            e.fullQueued = true;
        }
    }
    for (size_t i : thumbs) {
        Entry& e = entries_[i];
        if (!e.thumbReady && !e.thumbQueued && !e.fullQueued) {
            jobs_.push_back({i, false});
            e.thumbQueued = true;
        }
    }
    cv_.notify_one();
}

void ImageCache::pump() {
    std::vector<Result> res;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        res.swap(done_);
    }
    for (auto& r : res) {
        auto it = entries_.find(r.theme);
        Entry* e = it == entries_.end() ? nullptr : &it->second;
        if (e) (r.full ? e->fullQueued : e->thumbQueued) = false;
        const bool wantF = wantFull_.count(r.theme) != 0;
        const bool wantT = wantF || wantThumb_.count(r.theme) != 0;

        if (e && r.full && wantF) {
            freeImages(*e);
            for (SDL_Surface* s : r.images) {
                if (!s) continue;
                CachedImage ci = upload(s);
                if (ci.tex) e->images.push_back(ci);
            }
            e->fullReady = true;
        }
        if (e && wantT && !e->thumbReady) {
            if (r.thumb) e->thumb = upload(r.thumb);
            e->thumbReady = true;
        }
        freeResult(r.images, r.thumb);
    }
}

const std::vector<CachedImage>* ImageCache::images(size_t theme) const {
    auto it = entries_.find(theme);
    return (it != entries_.end() && it->second.fullReady) ? &it->second.images : nullptr;
}

const CachedImage* ImageCache::thumb(size_t theme) const {
    auto it = entries_.find(theme);
    return (it != entries_.end() && it->second.thumbReady && it->second.thumb.tex) ? &it->second.thumb : nullptr;
}

CachedImage ImageCache::upload(SDL_Surface* s) {
    CachedImage ci;
    ci.tex = SDL_CreateTextureFromSurface(ren_, s);
    if (!ci.tex) {
        Log::warn("image", std::string("upload: ") + SDL_GetError());
        return ci;
    }
    SDL_SetTextureBlendMode(ci.tex, SDL_BLENDMODE_BLEND);
    ci.w = s->w;
    ci.h = s->h;
    return ci;
}

void ImageCache::freeImages(Entry& e) {
    for (auto& ci : e.images)
        if (ci.tex) SDL_DestroyTexture(ci.tex);
    e.images.clear();
    e.fullReady = false;
}

void ImageCache::freeThumb(Entry& e) {
    if (e.thumb.tex) SDL_DestroyTexture(e.thumb.tex);
    e.thumb = CachedImage{};
    e.thumbReady = false;
}
