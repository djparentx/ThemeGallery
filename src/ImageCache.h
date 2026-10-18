#pragma once
#include <SDL.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "Config.h"

struct CachedImage {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

// Decodes + downscales on a worker thread; textures are created on the main thread in pump()
class ImageCache {
public:
    ImageCache(SDL_Renderer* ren, const Gallery& gallery, int bandH, int thumbH);
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // full: themes needing all images (priority order); thumbs: themes needing only the list thumbnail
    void want(const std::vector<size_t>& full, const std::vector<size_t>& thumbs);
    void pump();
    const std::vector<CachedImage>* images(size_t theme) const;  // nullptr while loading; empty = no images
    const CachedImage* thumb(size_t theme) const;                // nullptr while loading or unavailable

private:
    struct Job {
        size_t theme;
        bool full;
    };
    struct Result {
        size_t theme;
        bool full;
        std::vector<SDL_Surface*> images;
        SDL_Surface* thumb = nullptr;
    };
    struct Entry {
        std::vector<CachedImage> images;
        CachedImage thumb;
        bool fullReady = false, thumbReady = false;    // attempted (thumb.tex may still be null)
        bool fullQueued = false, thumbQueued = false;  // job queued or in flight
    };

    SDL_Renderer* ren_;
    const Gallery& gallery_;
    const int bandH_, thumbH_;

    std::map<size_t, Entry> entries_;        // main thread only
    std::set<size_t> wantFull_, wantThumb_;  // main thread only

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::vector<Result> done_;
    std::atomic<bool> quit_{false};
    std::thread worker_;

    void run();
    CachedImage upload(SDL_Surface* s);
    static void freeImages(Entry& e);
    static void freeThumb(Entry& e);
};
