#pragma once
#include <SDL.h>
#include <string>
#include "Screen.h"

class App;

class CarouselScreen : public Screen {
public:
    explicit CarouselScreen(App& app) : app_(app) {}
    void enter() override;
    void handle(Action a) override;
    void render(Uint32 now) override;

private:
    App& app_;
    int focus_ = 0;         // focused image within the current theme
    float hFrom_ = 0.0f;    // image slide start offset, 0 = idle
    Uint32 hStart_ = 0;
    size_t prevTheme_ = 0;  // theme sliding out during a theme change
    int prevFocus_ = 0;
    float vDir_ = 0.0f;     // +1 new theme enters from below, -1 from above, 0 = idle
    Uint32 vStart_ = 0;

    void requestImages();
    float hOffset(Uint32 now);
    void drawTheme(size_t theme, int focus, float hOff, int dy);
    void drawBadges(size_t theme, const SDL_Rect& r);
    void field(int x, int y, int maxW, const std::string& label, const std::string& value, SDL_Color valueColor);
};
