#include "Layout.h"
#include <algorithm>

int Layout::px(float base) const {
    return (int)(base * scale + 0.5f);
}

void Layout::compute(int width, int height) {
    w = width;
    h = height;
    scale = h / 480.0f;
    margin = px(16);

    header = {0, 0, w, px(40)};
    footer = {0, h - px(34), w, px(34)};

    gap = px(12);
    preview = {0, header.y + header.h + px(4), w, px(245)};

    title = {margin, preview.y + preview.h + px(4), w - 2 * margin, px(30)};

    int infoY = title.y + title.h + px(2);
    info = {margin, infoY, w - 2 * margin, footer.y - px(6) - infoY};

    fontHeader = px(22);
    fontTitle  = px(20);
    fontBody   = px(15);
    fontSmall  = px(12);
}
