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

    // bottom-up: 2-row info box above the footer, title above it, preview band takes the rest
    info = {margin, footer.y - px(6) - px(52), w - 2 * margin, px(52)};
    title = {margin, info.y - px(2) - px(30), w - 2 * margin, px(30)};
    int previewY = header.y + header.h + px(4);
    preview = {0, previewY, w, title.y - px(4) - previewY};

    fontHeader = px(22);
    fontTitle  = px(20);
    fontBody   = px(15);
    fontSmall  = px(12);
    fontTiny   = px(9);

    listRowH = px(52);
    thumbH   = px(40);
}
