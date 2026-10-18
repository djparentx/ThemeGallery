#pragma once
#include <SDL.h>

// Design base is 640x480; vertical sizes scale by h/480, horizontal regions follow screen width
struct Layout {
    int w = 0, h = 0;
    float scale = 1.0f;
    int margin = 0, gap = 0;
    SDL_Rect header{}, preview{}, title{}, info{}, footer{};  // preview = full-width band; images size to their own aspect
    int fontHeader = 0, fontTitle = 0, fontBody = 0, fontSmall = 0, fontTiny = 0;
    int listRowH = 0, thumbH = 0;

    void compute(int width, int height);
    int px(float base) const;
};
